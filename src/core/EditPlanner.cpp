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
        const bool emptyBody = afterBrace.startsWith('}');
        if (emptyBody) continue;    // nothing to instrument

        const OriginHint hint = opts.addOriginScope
                                    ? classifyOrigin(fn)
                                    : OriginHint::Internal;

        const QString lineIndent = leadingWhitespace(braceLine);
        const QString bodyIndent = lineIndent + "    ";

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