// core/EditApplier.cpp
#include "EditApplier.h"
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>

QStringList EditApplier::apply(const QStringList& originalLines,
                               QVector<TextEdit> edits,
                               bool* ok) {
    if (ok) *ok = true;

    // Sort descending by line, so earlier inserts don't shift later ones.
    std::sort(edits.begin(), edits.end(),
              [](const TextEdit& a, const TextEdit& b) {
                  if (a.line != b.line) return a.line > b.line;
                  return a.kind == EditKind::IncludeHeader ? true : false;
              });

    QStringList out = originalLines;
    for (const TextEdit& e : edits) {
        if (e.line < 0 || e.line > out.size()) {
            if (ok) *ok = false;
            continue;
        }
        // Split multiline inserts into individual lines.
        const QStringList add = e.text.split('\n');
        for (int i = add.size() - 1; i >= 0; --i) {
            out.insert(e.line, add[i]);
        }
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
    // Very small, dependency-free diff: emit the whole file with -/+ markers
    // for changed lines only. Good enough for a preview dialog.
    QString out;
    out += QStringLiteral("--- a/%1\n+++ b/%1\n").arg(fileLabel);

    // Simple LCS-free heuristic: walk both, mark inserted lines.
    int i = 0, j = 0;
    while (i < original.size() || j < modified.size()) {
        if (i < original.size() && j < modified.size() &&
            original[i] == modified[j]) {
            out += " " + original[i] + "\n";
            ++i; ++j;
        } else if (j < modified.size() &&
                   (i >= original.size() || !modified.contains(original[i]))) {
            // insertion
            out += "+" + modified[j] + "\n";
            ++j;
        } else {
            out += "-" + original[i] + "\n";
            ++i;
        }
    }
    return out;
}