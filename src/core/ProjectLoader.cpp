// core/ProjectLoader.cpp
#include "ProjectLoader.h"
#include <QDirIterator>
#include <QRegularExpression>

bool ProjectLoader::isGeneratedOrThirdParty(const QString& path) {
    static QRegularExpression gen(R"((^|/)(moc_|ui_|qrc_|.*\.moc$))");
    static QRegularExpression third(R"((^|/)(3rdparty|third_party|vendor|external|build|\.git)(/|$))",
                                    QRegularExpression::CaseInsensitiveOption);
    return gen.match(path).hasMatch() || third.match(path).hasMatch();
}

ProjectLoader::Result ProjectLoader::scan(const QString& root) {
    Result r;
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        if (isGeneratedOrThirdParty(p)) continue;
        const QString suf = QFileInfo(p).suffix().toLower();
        if (suf == "h" || suf == "hpp" || suf == "hxx") r.headers << p;
        else if (suf == "cpp" || suf == "cc" || suf == "cxx") r.sources << p;
        else if (suf == "qml") r.qml << p;
    }
    r.headers.sort(); r.sources.sort(); r.qml.sort();
    return r;
}