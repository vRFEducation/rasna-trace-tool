// core/EditPlanner.cpp
#include "EditPlanner.h"
#include "IdempotencyChecker.h"
#include <QRegularExpression>
#include <QFileInfo>

OriginHint EditPlanner::classifyOrigin(const FunctionInfo& fn) {
    // Phase 1 heuristic: event handler overrides -> User
    static QRegularExpression eventName(R"(^(event|.*Event)$)");
    static QRegularExpression widgetEvents(
        R"(^(paintEvent|mouse\w*Event|key\w*Event|resizeEvent|moveEvent|"
        R"(showEvent|hideEvent|closeEvent|focus\w*Event|wheelEvent|"
        R"(drag\w*Event|dropEvent|enterEvent|leaveEvent|contextMenuEvent|"
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

QVector<TextEdit> EditPlanner::plan(const QString& filePath,
                                    const QStringList& lines,
                                    const QVector<FunctionInfo>& functions,
                                    const Options& opts) {
    QVector<TextEdit> edits;

    const bool needInclude = opts.addInclude && !IdempotencyChecker::hasTraceInclude(lines);
    if (needInclude) {
        // Insert after the last top-of-file #include, or at very top if none.
        int insertAt = 0;
        for (int i = 0; i < qMin(lines.size(), 40); ++i) {
            const QString t = lines[i].trimmed();
            if (t.startsWith("#include") || t.startsWith("#pragma once") ||
                t.startsWith("#ifndef") || t.startsWith("#define"))
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

    for (const FunctionInfo& fn : functions) {
        if (fn.openBraceLine <= 0) continue;
        if (fn.isSignal && !opts.instrumentSignals) continue;
        if (fn.isPureVirtual) continue;
        if (fn.isDeleted && !opts.instrumentDeleted) continue;
        if (fn.isDefaulted) continue;

        const int braceIdx = fn.openBraceLine - 1;  // 0-based

        if (IdempotencyChecker::hasTraceFn(lines, braceIdx)) continue;

        // Insert origin scope first (if any), then TRACE_FN below it,
        // so the final order in the body is:
        //   { OriginScope ...; TRACE_FN(); <body> }
        const OriginHint hint = opts.addOriginScope
                                    ? classifyOrigin(fn)
                                    : OriginHint::Internal;

        int insertLine = braceIdx + 1;   // right after the '{' line
        QString block;

        if (hint != OriginHint::Internal) {
            QString originName =
                hint == OriginHint::User   ? "User"   :
                hint == OriginHint::Device ? "Device" :
                hint == OriginHint::Timer  ? "Timer"  : "Signal";
            QString src = fn.name;   // good enough hint for the analyzer
            block += QStringLiteral("    trace::OriginScope _os(trace::Origin::%1, \"%2\");\n")
                         .arg(originName, src);
        }
        if (opts.addTraceFn) {
            block += QStringLiteral("    TRACE_FN();\n");
        }

        if (block.endsWith('\n')) block.chop(1);

        TextEdit e;
        e.filePath = filePath;
        e.line     = insertLine;
        e.text     = block;
        e.kind     = hint == OriginHint::Internal ? EditKind::TraceFn : EditKind::OriginScope;
        e.reason   = fn.className.isEmpty()
                        ? fn.name
                        : fn.className + "::" + fn.name;
        edits.push_back(e);
    }

    return edits;
}