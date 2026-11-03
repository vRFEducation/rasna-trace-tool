#pragma once
#include <QString>

enum class EditKind {
    InsertBeforeLine,   // insert `text` before `line`; `replaceCount` ignored
    ReplaceLine,        // replace `replaceCount` lines starting at `line`
    IncludeHeader,      // sugar for InsertBeforeLine; same semantics
    TraceFn,        // insert TRACE_FN() / Trace.enter(...)
    OriginScope,    // insert trace::OriginScope / Trace.origin(...)
    Other
};

struct TextEdit {
    QString  filePath;
    int      line         = 0;              // 0-based
    int      replaceCount = 0;              // only for ReplaceLine
    QString  text;
    EditKind kind         = EditKind::InsertBeforeLine;
    QString  reason;                        // e.g. "DataStore::save"

    // Stable ordering for the applier: process higher lines first.
    // For equal lines, ReplaceLine goes before InsertBeforeLine.
    bool operator<(const TextEdit& o) const {
        if (line != o.line) return line < o.line;
        return kind == EditKind::ReplaceLine &&
               o.kind != EditKind::ReplaceLine;
    }
};
