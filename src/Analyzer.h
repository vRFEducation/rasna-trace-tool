#pragma once
#include "TraceLoader.h"
#include <QString>
#include <vector>

namespace rt {

struct LayerViolation {
    int         count = 0;
    QString     callerLayer;
    QString     calleeLayer;
    QString     via;          // callee function name
    int         sampleEvent = -1;   // index into trace.events
};

struct BlockingFinding {
    QString     layer;
    QString     fn;
    QString     cls;
    QString     file;
    int         line = 0;
    QString     thread;
    uint64_t    dur_ns = 0;
    int         eventIndex = -1;
};

struct FanInFinding {
    QString     fn;
    int         distinctParents = 0;
    int         calls = 0;
};

struct CouplingFinding {
    QString     first;
    QString     second;
    int         occurrences = 0;
    int         totalGroups = 0;
    double      ratio = 0.0;
};

struct EntryPointFinding {
    QString     fn;
    int         count = 0;
};

struct Analysis {
    std::vector<LayerViolation>  violations;
    std::vector<BlockingFinding> blocking;
    std::vector<FanInFinding>    fanIn;
    std::vector<CouplingFinding> coupling;
    std::vector<EntryPointFinding> entries;

    uint64_t totalBlockedNs = 0;
};

struct AnalyzeOptions {
    uint64_t blockingThresholdNs = 10'000'000ULL;  // 10 ms
    QString  guiThread;                            // auto-detected if empty
};

Analysis analyze(const Trace& trace, const AnalyzeOptions& opts = {});

} // namespace rt