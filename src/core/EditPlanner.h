// core/EditPlanner.h
#pragma once
#include "TextEdit.h"
#include "FunctionInfo.h"
#include <QStringList>

class EditPlanner {
public:
    struct Options {
        bool addTraceFn        = true;
        bool addOriginScope    = true;
        bool addInclude        = true;
        bool instrumentSignals = false;   // phase 3
        bool instrumentDeleted = false;   // never, but exposed
    };

    // Produces all edits for a single file. The file's lines are needed
    // for idempotency checks. Returns edits keyed to that file.
    static QVector<TextEdit> plan(const QString& filePath,
                                  const QStringList& lines,
                                  const QVector<FunctionInfo>& functions,
                                  const Options& opts = {});
private:
    static OriginHint classifyOrigin(const FunctionInfo& fn);
};