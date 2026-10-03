// core/EditApplier.h
#pragma once
#include "TextEdit.h"
#include <QString>
#include <QStringList>

class EditApplier {
public:
    // Applies edits in memory and returns the new content.
    // Throws nothing; on error returns false via outOk.
    static QStringList apply(const QStringList& originalLines,
                             QVector<TextEdit> edits,
                             bool* ok = nullptr);

    // Convenience: apply + write file atomically (temp + rename).
    static bool applyToFile(const QString& filePath,
                            QVector<TextEdit> edits,
                            QString* errorOut = nullptr);

    // Produces a unified-ish diff for preview. Not git-grade, but readable.
    static QString makeDiff(const QStringList& original,
                            const QStringList& modified,
                            const QString& fileLabel);
};