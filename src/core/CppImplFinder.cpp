// core/CppImplFinder.cpp
#include "CppImplFinder.h"
#include "BraceScanner.h"
#include <QFile>
#include <QTextStream>

QVector<FunctionInfo> CppImplFinder::findImplementations(const QString& cppFilePath,
                                                         const QVector<FunctionInfo>& headerFns) {
    QVector<FunctionInfo> found;
    QFile f(cppFilePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return found;

    QTextStream ts(&f);
    QVector<QString> lines;
    while (!ts.atEnd()) lines.push_back(ts.readLine());

    const QVector<QString> stripped = BraceScanner::stripLiteralsAndComments(lines);

    for (const FunctionInfo& fn : headerFns) {
        if (fn.isSignal) continue;
        if (fn.isPureVirtual) continue;
        if (fn.isDeleted) continue;

        // Construct search key: ClassName::name(
        QString key = fn.className.isEmpty()
                    ? fn.name + "("
                    : fn.className + "::" + fn.name + "(";

        int sigLine = -1;
        for (int i = 0; i < stripped.size(); ++i) {
            if (stripped[i].contains(key)) { sigLine = i; break; }
        }
        if (sigLine < 0) continue;

        int openBrace = BraceScanner::findOpenBrace(stripped, sigLine);
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
    return found;
}