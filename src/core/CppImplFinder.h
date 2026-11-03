// core/CppImplFinder.h
#pragma once
#include "FunctionInfo.h"
#include <QString>

class CppImplFinder {
public:
    // For each header FunctionInfo, find its definition in cppFilePath.
    // Returns a filtered list: only functions with a located body.
    QVector<FunctionInfo> findImplementations(const QString& cppFilePath,
                                              const QVector<FunctionInfo>& headerFns);
};