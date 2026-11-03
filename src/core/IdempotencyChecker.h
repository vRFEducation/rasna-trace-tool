// core/IdempotencyChecker.h
#pragma once
#include <QStringList>

class IdempotencyChecker {
public:
    // True if a TRACE_FN() call already appears within the first
    // `lookahead` lines after openBraceLine (0-based, exclusive).
    static bool hasTraceFn(const QStringList& lines, int openBraceLine, int lookahead = 3);

    // True if the file already includes our trace header.
    static bool hasTraceInclude(const QStringList& lines);

    static bool hasTraceEnter(const QStringList& lines, int openBraceLine,
        int lookahead = 3);

};