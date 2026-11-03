#pragma once
#include "TraceLoader.h"
#include "Analyzer.h"
#include <QString>

namespace rt {

struct ReportOptions {
    QString title = "Ronia Trace Report";
    int     topFunctionLimit = 50;
    bool    includeTree = true;
    bool    includeTimeline = true;
};

bool writeHtmlReport(const QString& path,
                     const Trace& trace,
                     const Analysis& analysis,
                     const ReportOptions& opts,
                     QString* errOut = nullptr);

} // namespace rt