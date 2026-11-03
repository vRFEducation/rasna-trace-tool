// src/core/QmlFunctionFinder.h
#pragma once
#include "FunctionInfo.h"
#include <QString>

class QmlFunctionFinder {
public:
    QVector<FunctionInfo> findFunctions(const QString& qmlPath);
};