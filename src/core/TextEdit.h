// core/TextEdit.h
#pragma once
#include <QString>

enum class EditKind {
    TraceFn,        // insert TRACE_FN();
    OriginScope,    // insert trace::OriginScope ...
    IncludeHeader,  // insert #include "trace/Trace.h"
    Other
};

struct TextEdit {
    QString filePath;
    int     line = 0;         // 0-based; insert BEFORE this line
    QString text;             // text to insert (may contain \n)
    EditKind kind = EditKind::Other;
    QString reason;           // "Foo::bar", "onClicked handler", etc.

    // for diff preview
    int sortKey() const { return line; }
};
