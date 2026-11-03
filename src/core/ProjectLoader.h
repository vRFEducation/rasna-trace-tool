// core/ProjectLoader.h
#pragma once
#include <QString>
#include <QStringList>

class ProjectLoader {
public:
    struct Result {
        QStringList headers;   // .h .hpp
        QStringList sources;   // .cpp .cc .cxx
        QStringList qml;
    };
    static Result scan(const QString& root);
    static bool isGeneratedOrThirdParty(const QString& path);
};