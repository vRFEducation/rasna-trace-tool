#include "EditApplier.h"
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>

QStringList EditApplier::apply(const QStringList& originalLines,
                               QVector<TextEdit> edits,
                               bool* ok) {
    if (ok) *ok = true;

    // Apply descending by line so earlier edits aren't shifted.
    std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) {
        if (a.line != b.line) return a.line > b.line;
        return a.kind == EditKind::ReplaceLine && b.kind != EditKind::ReplaceLine;
    });

    QStringList out = originalLines;
    for (const TextEdit& e : edits) {
        if (e.line < 0 || e.line > out.size()) {
            if (ok) *ok = false;
            continue;
        }

        if (e.kind == EditKind::ReplaceLine) {
            const int removeCount = qBound(0, e.replaceCount, out.size() - e.line);
            for (int i = 0; i < removeCount; ++i) out.removeAt(e.line);
        }

        const QStringList add = e.text.split('\n');
        for (int i = add.size() - 1; i >= 0; --i)
            out.insert(e.line, add[i]);
    }
    return out;
}

bool EditApplier::applyToFile(const QString& filePath,
                              QVector<TextEdit> edits,
                              QString* errorOut) {
    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = in.errorString();
        return false;
    }
    QTextStream ts(&in);
    QStringList lines;
    while (!ts.atEnd()) lines << ts.readLine();
    in.close();

    bool ok = true;
    const QStringList updated = apply(lines, edits, &ok);
    if (!ok) {
        if (errorOut) *errorOut = "Line index out of range in edit plan.";
        return false;
    }

    QSaveFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = out.errorString();
        return false;
    }
    QTextStream os(&out);
    for (const QString& l : updated) os << l << '\n';
    return out.commit();
}

QString EditApplier::makeDiff(const QStringList& original,
                              const QStringList& modified,
                              const QString& fileLabel) {
    QString out;
    out += QStringLiteral("--- a/%1\n+++ b/%1\n").arg(fileLabel);
    int i = 0, j = 0;
    while (i < original.size() || j < modified.size()) {
        if (i < original.size() && j < modified.size() &&
            original[i] == modified[j]) {
            out += " " + original[i] + "\n";
            ++i; ++j;
        } else if (j < modified.size() &&
                   (i >= original.size() || !modified.contains(original[i]))) {
            out += "+" + modified[j] + "\n";
            ++j;
        } else if (i < original.size()) {
            out += "-" + original[i] + "\n";
            ++i;
        }
    }
    return out;
}