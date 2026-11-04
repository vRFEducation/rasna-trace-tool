// core/CppImplFinder.h
#pragma once
#include "FunctionInfo.h"
#include <QString>

class CppImplFinder {
public:
    // Scans the .cpp directly. Ignores headerFns (kept in the signature so
    // existing callers don't need to change). Returns one FunctionInfo per
    // detected function definition.
    QVector<FunctionInfo> findImplementations(const QString& cppFilePath,
                                              const QVector<FunctionInfo>& headerFns = {});
};
