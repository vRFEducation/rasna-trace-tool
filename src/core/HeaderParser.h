// core/HeaderParser.h
#pragma once
#include "FunctionInfo.h"
#include <QString>

class HeaderParser {
public:
    QVector<FunctionInfo> parseFile(const QString& headerPath);

private:
    enum class State { Normal, Signals, PublicSlots, ProtectedSlots, PrivateSlots };
    enum class Access { Public, Protected, Private };

    bool     m_inClass = false;
    QString  m_className;
    State    m_state = State::Normal;
    Access   m_access = Access::Public;
    QString  m_filePath;

    QString extractClassName(const QString& line) const;
    QString extractStructName(const QString& line) const;
    bool isVariableDeclaration(const QString& line) const;
    bool isFunctionStart(const QString& line) const;
    bool isCompleteDeclaration(const QString& decl) const;
    bool isContinuationLine(const QString& line) const;
    void tryParseFunction(const QString& decl, int startLine,
                          QVector<FunctionInfo>& out);
    void parseFunction(const QString& decl, int startLine,
                       QVector<FunctionInfo>& out);
    QVector<ParamInfo> parseParameters(const QString& paramsText);
    static QVector<QString> splitParams(const QString& text);
    static int findMatchingParen(const QString& text, int openPos);
    static QString removeTrailingQualifiers(const QString& decl);
    static QString removeQtQualifiers(const QString& decl);
};
