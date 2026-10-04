#pragma once
#include "TextEdit.h"
#include "FunctionInfo.h"
#include <QStringList>

class EditPlanner {
public:
    struct Options {
        enum class IncludePolicy { AddIfMissing, Never, Force };

        bool          addTraceFn        = true;
        bool          addOriginScope    = true;
        IncludePolicy includePolicy     = IncludePolicy::AddIfMissing;
        bool          instrumentSignals = false;
        bool          reformatOneLiners = true;   // rewrite "{ x(); }" to multi-line
    };

    static QVector<TextEdit> plan(const QString& filePath,
                                  const QStringList& lines,
                                  const QVector<FunctionInfo>& functions,
                                  const Options& opts = {});

private:
    static OriginHint classifyOrigin(const FunctionInfo& fn);
};