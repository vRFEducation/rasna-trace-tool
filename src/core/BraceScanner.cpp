// core/BraceScanner.cpp
#include "BraceScanner.h"

namespace {
enum class S { Normal, Line, Block, Str, Chr, Raw };
}

QVector<QString> BraceScanner::stripLiteralsAndComments(const QVector<QString>& lines) {
    QVector<QString> out;
    out.reserve(lines.size());

    S state = S::Normal;
    for (const QString& line : lines) {
        QString s = line;
        int i = 0;
        while (i < s.size()) {
            QChar c = s[i];
            QChar n = (i + 1 < s.size()) ? s[i + 1] : QChar();

            switch (state) {
            case S::Normal:
                if (c == '/' && n == '/') { s[i] = ' '; s[i+1] = ' '; i += 2; state = S::Line; continue; }
                if (c == '/' && n == '*') { s[i] = ' '; s[i+1] = ' '; i += 2; state = S::Block; continue; }
                if (c == '"') { s[i] = ' '; i++; state = S::Str; continue; }
                if (c == '\'') { s[i] = ' '; i++; state = S::Chr; continue; }
                i++;
                break;
            case S::Line:
                if (c == '\\' && i + 1 == s.size()) { /* line continuation */ }
                s[i] = ' ';
                i++;
                break;
            case S::Block:
                if (c == '*' && n == '/') { s[i] = ' '; s[i+1] = ' '; i += 2; state = S::Normal; continue; }
                s[i] = ' ';
                i++;
                break;
            case S::Str:
                if (c == '\\') { s[i] = ' '; if (i+1 < s.size()) s[i+1] = ' '; i += 2; continue; }
                if (c == '"') { s[i] = ' '; i++; state = S::Normal; continue; }
                s[i] = ' ';
                i++;
                break;
            case S::Chr:
                if (c == '\\') { s[i] = ' '; if (i+1 < s.size()) s[i+1] = ' '; i += 2; continue; }
                if (c == '\'') { s[i] = ' '; i++; state = S::Normal; continue; }
                s[i] = ' ';
                i++;
                break;
            case S::Raw:
                // Kept simple for MVP — raw strings are rare in Qt code.
                s[i] = ' ';
                i++;
                break;
            }
        }
        if (state == S::Line) state = S::Normal; // line comments end at newline
        out.push_back(s);
    }
    return out;
}

int BraceScanner::findOpenBrace(const QVector<QString>& stripped, int startLine) {
    for (int i = startLine; i < stripped.size(); ++i) {
        if (stripped[i].contains('{')) return i;
        // If we see a ';' before any '{', this is a declaration, not a body.
        if (stripped[i].contains(';')) return -1;
    }
    return -1;
}

int BraceScanner::findMatchingClose(const QVector<QString>& stripped, int openBraceLine) {
    int depth = 0;
    bool started = false;
    for (int i = openBraceLine; i < stripped.size(); ++i) {
        const QString& line = stripped[i];
        for (QChar c : line) {
            if (c == '{') { depth++; started = true; }
            else if (c == '}') {
                depth--;
                if (started && depth == 0) return i;
                if (depth < 0) return -1;
            }
        }
    }
    return -1;
}