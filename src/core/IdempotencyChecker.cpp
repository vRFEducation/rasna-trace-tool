// core/IdempotencyChecker.cpp
#include "IdempotencyChecker.h"

bool IdempotencyChecker::hasTraceFn(const QStringList& lines, int openBraceLine, int lookahead) {
    const int end = qMin(openBraceLine + 1 + lookahead, lines.size());
    for (int i = openBraceLine + 1; i < end; ++i) {
        if (lines[i].contains("TRACE_FN(")) return true;
    }
    return false;
}

bool IdempotencyChecker::hasTraceInclude(const QStringList& lines) {
    for (const QString& l : lines) {
        if (l.contains("trace/Trace.h") || l.contains("trace/Trace.hpp"))
            return true;
        // stop at first non-include, non-comment, non-blank line
        const QString t = l.trimmed();
        if (!t.isEmpty() && !t.startsWith('#') && !t.startsWith("//") && !t.startsWith("/*"))
            break;
    }
    return false;
}