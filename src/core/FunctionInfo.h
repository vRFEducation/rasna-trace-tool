#pragma once
#include <QString>
#include <QVector>

enum class QtAccess { Public, Protected, Private, Unknown };

enum class OriginHint {
    Internal,
    User,       // *Event override
    Device,     // readData / readyRead overrides
    Timer,      // *timeout slots (phase 3)
    Signal
};

struct ParamInfo {
    QString type;
    QString name;
    QString defaultValue;
    bool    hasDefault = false;
};

struct FunctionInfo {
    QString filePath;
    QString className;
    QString name;
    QString returnType;
    QVector<ParamInfo> params;

    bool isConst       = false;
    bool isStatic      = false;
    bool isVirtual     = false;
    bool isOverride    = false;
    bool isConstructor = false;
    bool isDestructor  = false;
    bool isSignal      = false;
    bool isSlot        = false;
    bool isInlineInHeader = false;
    bool isDeleted     = false;
    bool isDefaulted   = false;
    bool isPureVirtual = false;

    QtAccess access = QtAccess::Unknown;
    OriginHint originHint = OriginHint::Internal;

    // 1-based line numbers, -1 if not applicable
    int signatureLine  = -1;
    int openBraceLine  = -1;
    int closeBraceLine = -1;

    QString signature() const {
        QString cls = className.isEmpty() ? QString() : className + QStringLiteral("::");
        return QStringLiteral("%1%2%3").arg(cls, name, QStringLiteral("(...)"));
    }
};