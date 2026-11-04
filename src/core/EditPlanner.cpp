#include "EditPlanner.h"
#include "IdempotencyChecker.h"
#include <QRegularExpression>
#include <QFileInfo>

OriginHint EditPlanner::classifyOrigin(const FunctionInfo& fn) {
    static QRegularExpression widgetEvents(
        R"(^(paintEvent|mouse\w*Event|key\w*Event|resizeEvent|moveEvent|)"
        R"(showEvent|hideEvent|closeEvent|focus\w*Event|wheelEvent|)"
        R"(drag\w*Event|dropEvent|enterEvent|leaveEvent|contextMenuEvent|)"
        R"(inputMethodEvent|changeEvent|timerEvent|childEvent|customEvent)$)");

    if (fn.isOverride) {
        if (widgetEvents.match(fn.name).hasMatch()) return OriginHint::User;
        if (fn.name == "event")                    return OriginHint::User;
    }
    if (fn.name == "readyRead" || fn.name == "readData" ||
        fn.name == "writeData" || fn.name == "bytesWritten")
        return OriginHint::Device;
    return OriginHint::Internal;
}

namespace {

QString leadingWhitespace(const QString& s) {
    int i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.left(i);
}

QString originScopeName(OriginHint h) {
    switch (h) {
    case OriginHint::User:   return "User";
    case OriginHint::Device: return "Device";
    case OriginHint::Timer:  return "Timer";
    case OriginHint::Signal: return "Signal";
    default:                 return "Internal";
    }
}

QString qualifiedName(const FunctionInfo& fn) {
    return fn.className.isEmpty() ? fn.name : fn.className + "::" + fn.name;
}

// Returns the two lines that should be inserted inside a body.
// `indent` is the indentation for the inserted lines.
QString buildGuardLines(const EditPlanner::Options& opts,
                        OriginHint hint,
                        const FunctionInfo& fn,
                        const QString& indent) {
    QStringList out;
    if (opts.addOriginScope && hint != OriginHint::Internal) {
        out << indent + QStringLiteral("trace::OriginScope _os(trace::Origin::%1, \"%2\");")
                            .arg(originScopeName(hint), fn.name);
    }
    if (opts.addTraceFn) {
        out << indent + QStringLiteral("TRACE_FN();");
    }
    return out.join('\n');
}

} // namespace

QVector<TextEdit> EditPlanner::plan(const QString& filePath,
                                    const QStringList& lines,
                                    const QVector<FunctionInfo>& functions,
                                    const Options& opts) {
    QVector<TextEdit> edits;

    // ---- include ----
    const bool needInclude =
        opts.includePolicy == Options::IncludePolicy::Force ||
        (opts.includePolicy == Options::IncludePolicy::AddIfMissing &&
         !IdempotencyChecker::hasTraceInclude(lines));

    if (needInclude) {
        int insertAt = 0;
        for (int i = 0; i < qMin(lines.size(), 40); ++i) {
            const QString t = lines[i].trimmed();
            if (t.startsWith("#include") || t.startsWith("#pragma once") ||
                t.startsWith("#ifndef")  || t.startsWith("#define"))
                insertAt = i + 1;
            else if (!t.isEmpty() && !t.startsWith("//") && !t.startsWith("/*"))
                break;
        }
        TextEdit e;
        e.filePath = filePath;
        e.line     = insertAt;
        e.text     = QStringLiteral("#include \"trace/Trace.h\"");
        e.kind     = EditKind::IncludeHeader;
        e.reason   = "trace runtime";
        edits.push_back(e);
    }

    // ---- QML files take a different path ----
    if (filePath.endsWith(".qml", Qt::CaseInsensitive)) {
        if (opts.includePolicy != Options::IncludePolicy::Never) {
            TextEdit inc;
            inc.filePath = filePath;
            inc.line     = 0;
            inc.text     = QStringLiteral("import Trace 1.0");
            inc.kind     = EditKind::IncludeHeader;
            inc.reason   = "trace runtime (QML)";
            // Skip if already imported — but the existing include-check is
            // C++-specific, so do a quick inline check.
            bool alreadyImported = false;
            for (int i = 0; i < qMin(lines.size(), 20); ++i) {
                if (lines[i].contains("import Trace")) { alreadyImported = true; break; }
            }
            if (!alreadyImported) edits.push_back(inc);
        }
        for (const FunctionInfo& fn : functions) {
            // ---- Expression body: onX: <expr> ----
            if (fn.bodyKind == BodyKind::Expression) {
                // bodyStartLine / bodyEndLine are 1-based, valid for expression bodies.
                const int startIdx = fn.bodyStartLine - 1;
                const int endIdx   = fn.bodyEndLine   - 1;
                if (startIdx < 0 || endIdx >= lines.size()) continue;
                if (IdempotencyChecker::hasTraceEnter(lines, startIdx)) continue;

                // Rewrite `onX: <expr>` (possibly spanning lines) into:
                //     onX: {
                //         Trace.origin(...)
                //         Trace.enter(...)
                //         <expr>
                //         Trace.exit()
                //     }
                // Preserve the leading indentation of the handler line.
                const QString firstLine = lines[startIdx];
                const QString lineIndent = leadingWhitespace(firstLine);
                const QString bodyIndent = lineIndent + "    ";

                // Everything before the colon on the first line is `onX` (+ possible attrs like `first.`).
                // Find the colon that separates the handler name from the expression.
                const int colonPos = firstLine.indexOf(QLatin1Char(':'));
                if (colonPos < 0) continue;
                const QString head = firstLine.left(colonPos + 1);  // "    onX:" (incl. indent)

                // Collect the expression text across bodyStartLine..bodyEndLine.
                // For single-line bodies this is just the text after the colon.
                QString exprText;
                for (int ln = startIdx; ln <= endIdx; ++ln) {
                    QString piece = lines[ln];
                    if (ln == startIdx) piece = piece.mid(colonPos + 1);
                    exprText += piece;
                    if (ln < endIdx) exprText += QLatin1Char('\n');
                }
                exprText = exprText.trimmed();
                if (exprText.isEmpty()) continue;
                if (!exprText.endsWith(QLatin1Char(';'))) exprText += QLatin1Char(';');

                QStringList newLines;
                newLines << head + QLatin1Char('{');
                if (fn.originHint != OriginHint::Internal) {
                    const QString originName =
                        fn.originHint == OriginHint::User   ? "User"   :
                            fn.originHint == OriginHint::Device ? "Device" :
                            fn.originHint == OriginHint::Timer  ? "Timer"  : "Signal";
                    newLines << bodyIndent + QStringLiteral("Trace.origin(\"%1\", \"%2\");")
                                                 .arg(originName, fn.qmlHandlerName);
                }
                newLines << bodyIndent + QStringLiteral("Trace.enter(\"%1\", \"%2\", %3);")
                                             .arg(fn.name, QFileInfo(filePath).fileName())
                                             .arg(fn.signatureLine);
                newLines << bodyIndent + exprText;
                newLines << bodyIndent + QStringLiteral("Trace.exit();");
                newLines << lineIndent + QLatin1Char('}');

                TextEdit e;
                e.filePath     = filePath;
                e.line         = startIdx;
                e.replaceCount = endIdx - startIdx + 1;
                e.kind         = EditKind::ReplaceLine;
                e.text         = newLines.join(QLatin1Char('\n'));
                e.reason       = fn.qmlHandlerName + " (expression body wrapped)";
                edits.push_back(e);
                continue;
            }

            // ---- Block body (existing code) ----
            if (fn.openBraceLine <= 0 || fn.closeBraceLine < fn.openBraceLine) continue;

            const int braceIdx = fn.openBraceLine - 1;
            const int closeIdx = fn.closeBraceLine - 1;
            if (braceIdx >= lines.size() || closeIdx >= lines.size()) continue;

            if (IdempotencyChecker::hasTraceEnter(lines, braceIdx)) continue;

            const QString lineIndent = leadingWhitespace(lines[braceIdx]);
            const QString bodyIndent = lineIndent + "    ";


            QString enterBlock;
            if (opts.addOriginScope && fn.originHint != OriginHint::Internal) {
                QString originName =
                    fn.originHint == OriginHint::User   ? "User"   :
                        fn.originHint == OriginHint::Device ? "Device" :
                        fn.originHint == OriginHint::Timer  ? "Timer"  : "Signal";
                enterBlock += bodyIndent + QStringLiteral("Trace.origin(\"%1\", \"%2\");\n")
                                               .arg(originName, fn.qmlHandlerName);
            }
            enterBlock += bodyIndent + QStringLiteral("Trace.enter(\"%1\", \"%2\", %3);")
                                           .arg(fn.name,
                                                QFileInfo(filePath).fileName())
                                           .arg(fn.openBraceLine);

            TextEdit enterEdit;
            enterEdit.filePath = filePath;
            enterEdit.line     = braceIdx + 1;
            enterEdit.text     = enterBlock;
            enterEdit.kind     = fn.originHint == OriginHint::Internal
                                 ? EditKind::TraceFn
                                 : EditKind::OriginScope;
            enterEdit.reason   = fn.qmlHandlerName + " [enter]";
            edits.push_back(enterEdit);

            TextEdit exitEdit;
            exitEdit.filePath = filePath;
            exitEdit.line     = closeIdx;
            exitEdit.text     = bodyIndent + QStringLiteral("Trace.exit();");
            exitEdit.kind     = EditKind::TraceFn;
            exitEdit.reason   = fn.qmlHandlerName + " [exit]";
            edits.push_back(exitEdit);
        }
        return edits;
    }

    // ---- per function ----
    for (const FunctionInfo& fn : functions) {
        if (fn.openBraceLine <= 0) continue;
        if (fn.isSignal && !opts.instrumentSignals) continue;
        if (fn.isPureVirtual || fn.isDeleted || fn.isDefaulted) continue;

        const int braceIdx = fn.openBraceLine - 1;       // 0-based
        if (braceIdx < 0 || braceIdx >= lines.size()) continue;

        const QString& braceLine = lines[braceIdx];

        // Already instrumented?
        if (braceLine.contains("TRACE_FN()")) continue;
        if (IdempotencyChecker::hasTraceFn(lines, braceIdx)) continue;

        const int braceCol = braceLine.indexOf('{');
        if (braceCol < 0) continue;

        const QString afterBrace = braceLine.mid(braceCol + 1).trimmed();

        const OriginHint hint = opts.addOriginScope
                                    ? classifyOrigin(fn)
                                    : OriginHint::Internal;

        const QString lineIndent = leadingWhitespace(braceLine);
        const QString bodyIndent = lineIndent + "    ";

        // ---- Case C: empty body on the same line: "void f() {}" ----
        // (Destructor, trivial getters, trivial slots, etc.)
        if (afterBrace.startsWith('}')) {
            if (!opts.reformatOneLiners) continue;   // user asked not to reformat

            // Everything before the '{' is the signature line
            QString signaturePart = braceLine.left(braceCol).trimmed();

            // Preserve any trailing comment after the '}'
            QString trailingComment;
            const int closePos = braceLine.indexOf('}', braceCol + 1);
            if (closePos >= 0) {
                QString tail = braceLine.mid(closePos + 1);
                int commentPos = tail.indexOf("//");
                if (commentPos >= 0) trailingComment = "  " + tail.mid(commentPos).trimmed();
                int blockPos = tail.indexOf("/*");
                if (blockPos >= 0)   trailingComment = "  " + tail.mid(blockPos).trimmed();
            }

            QStringList newLines;
            newLines << signaturePart + " {";

            const QString guard = buildGuardLines(opts, hint, fn, bodyIndent);
            if (!guard.isEmpty()) newLines << guard;

            newLines << lineIndent + "}" + trailingComment;

            TextEdit e;
            e.filePath     = filePath;
            e.line         = braceIdx;
            e.replaceCount = 1;
            e.kind         = EditKind::ReplaceLine;
            e.text         = newLines.join('\n');
            e.reason       = qualifiedName(fn) + " (empty body expanded)";
            edits.push_back(e);
            continue;
        }

        // ---- Case A: multi-line body. '{' is last meaningful token. ----
        const bool multilineBody =
            afterBrace.isEmpty() || afterBrace.startsWith("//");

        if (multilineBody) {
            TextEdit e;
            e.filePath = filePath;
            e.line     = braceIdx + 1;
            e.kind     = EditKind::InsertBeforeLine;
            e.text     = buildGuardLines(opts, hint, fn, bodyIndent);
            e.reason   = qualifiedName(fn);
            edits.push_back(e);
            continue;
        }

        // ---- Case B: same-line body "{ x(); }" ----
        if (!opts.reformatOneLiners) continue;

        // Only reformat if the body is a single simple statement. Refuse
        // anything with more than one ';', or with a nested '{' or '}'.
        QString bodyPart = afterBrace;
        if (bodyPart.endsWith('}')) bodyPart.chop(1);
        bodyPart = bodyPart.trimmed();

        // Preserve a trailing // comment
        QString trailingComment;
        const int commentPos = bodyPart.indexOf("//");
        if (commentPos >= 0) {
            trailingComment = "  " + bodyPart.mid(commentPos);
            bodyPart = bodyPart.left(commentPos).trimmed();
        }

        if (bodyPart.count(';') != 1) continue;       // too risky
        if (bodyPart.contains('{') || bodyPart.contains('}')) continue;

        // Split signature from body
        QString signaturePart = braceLine.left(braceCol).trimmed();

        QStringList newLines;
        newLines << signaturePart + " {";

        const QString guard = buildGuardLines(opts, hint, fn, bodyIndent);
        if (!guard.isEmpty()) newLines << guard;

        newLines << bodyIndent + bodyPart;
        newLines << lineIndent + "}" + trailingComment;

        TextEdit e;
        e.filePath     = filePath;
        e.line         = braceIdx;
        e.replaceCount = 1;
        e.kind         = EditKind::ReplaceLine;
        e.text         = newLines.join('\n');
        e.reason       = qualifiedName(fn) + " (reformatted)";
        edits.push_back(e);
    }

    return edits;
}
