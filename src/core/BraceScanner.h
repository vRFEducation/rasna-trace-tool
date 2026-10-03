// core/BraceScanner.h
#pragma once
#include <QString>
#include <QVector>

class BraceScanner {
public:
    // Returns a version of `lines` where string/char literals and comments
    // are replaced by spaces of equal length. Brace counting is safe on it.
    static QVector<QString> stripLiteralsAndComments(const QVector<QString>& lines);

    // Finds the line index (0-based) of the matching '}' starting at
    // openBraceLine (0-based). Returns -1 if not found.
    // Operates on stripped lines so strings/comments can't confuse it.
    static int findMatchingClose(const QVector<QString>& stripped, int openBraceLine);

    // Finds the first line >= startLine containing '{' on the *stripped* text.
    static int findOpenBrace(const QVector<QString>& stripped, int startLine);
};