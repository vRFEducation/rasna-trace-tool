// core/CppImplFinder.cpp
#include "CppImplFinder.h"
#include "BraceScanner.h"
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace {

bool looksLikeDefinition(const QString& line, const QString& key) {
    int k = line.indexOf(key);
    if (k < 0) return false;
    int semi  = line.indexOf(';', k);
    int brace = line.indexOf('{', k);
    if (semi >= 0 && (brace < 0 || semi < brace)) return false;
    return true;
}

int findBodyOpenBrace(const QVector<QString>& stripped, int sigLine, int maxLookahead = 40) {
    const int last = qMin(sigLine + maxLookahead, stripped.size() - 1);
    for (int i = sigLine; i <= last; ++i) {
        const QString& l = stripped[i];
        for (int c = 0; c < l.size(); ++c) {
            const QChar ch = l[c];
            if (ch == QLatin1Char(';') || ch == QLatin1Char('}')) return -1;
            if (ch == QLatin1Char('{')) return i;
        }
    }
    return -1;
}

} // namespace

QVector<FunctionInfo> CppImplFinder::findImplementations(const QString& cppFilePath,
                                                         const QVector<FunctionInfo>& headerFns) {
    QVector<FunctionInfo> found;
    QFile f(cppFilePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return found;

    QTextStream ts(&f);
    QVector<QString> lines;
    while (!ts.atEnd()) lines.push_back(ts.readLine());

    const QVector<QString> stripped = BraceScanner::stripLiteralsAndComments(lines);

    // ---- 1. Class member implementations ----
    for (const FunctionInfo& fn : headerFns) {
        if (fn.isSignal) continue;
        if (fn.isPureVirtual) continue;
        if (fn.isDeleted) continue;

        QString key = fn.className.isEmpty()
                          ? fn.name + "("
                          : fn.className + "::" + fn.name + "(";

        int sigLine = -1;
        for (int i = 0; i < stripped.size(); ++i) {
            if (looksLikeDefinition(stripped[i], key)) { sigLine = i; break; }
        }
        if (sigLine < 0) continue;

        int openBrace = findBodyOpenBrace(stripped, sigLine);
        if (openBrace < 0) continue;

        int closeBrace = BraceScanner::findMatchingClose(stripped, openBrace);
        if (closeBrace < 0) continue;

        FunctionInfo impl = fn;
        impl.filePath       = cppFilePath;
        impl.signatureLine  = sigLine + 1;
        impl.openBraceLine  = openBrace + 1;
        impl.closeBraceLine = closeBrace + 1;
        impl.isInlineInHeader = false;
        found.push_back(impl);
    }

    // ---- 2. Free-function definitions ----
    static const QRegularExpression freeFnRe(
        R"(^\s*(?:(?:inline|static|constexpr)\s+)*[A-Za-z_][\w:<>,\s\*&]*?\s+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{?\s*$)");

    static const QStringList kw = {
        "if", "for", "while", "switch", "return", "catch", "else",
        "do", "case", "sizeof"
    };

    QVector<bool> covered(stripped.size(), false);
    for (const auto& fi : found) {
        for (int l = fi.openBraceLine - 1; l <= fi.closeBraceLine - 1 && l < covered.size(); ++l)
            covered[l] = true;
    }

    for (int i = 0; i < stripped.size(); ++i) {
        if (covered[i]) continue;
        const QString& line = stripped[i];
        if (line.contains("::")) continue;

        auto m = freeFnRe.match(line);
        if (!m.hasMatch()) continue;

        const QString name = m.captured(1);
        if (kw.contains(name)) continue;
        if (line.contains('~')) continue;

        int openBrace = findBodyOpenBrace(stripped, i);
        if (openBrace < 0) continue;
        int closeBrace = BraceScanner::findMatchingClose(stripped, openBrace);
        if (closeBrace < 0) continue;

        FunctionInfo impl;
        impl.name             = name;
        impl.filePath         = cppFilePath;
        impl.signatureLine    = i + 1;
        impl.openBraceLine    = openBrace + 1;
        impl.closeBraceLine   = closeBrace + 1;
        impl.isInlineInHeader = false;
        found.push_back(impl);

        for (int l = openBrace; l <= closeBrace && l < covered.size(); ++l)
            covered[l] = true;
    }

    return found;
}
