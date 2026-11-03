// include/trace/TraceBridge.h
#pragma once
#include <QObject>
#include <QString>
#include "Trace.h"

// Exposed to QML as `Trace` singleton.
// Usage from QML:
//     Trace.enter("saveReading", "Main.qml", 42)
//     Trace.exit("saveReading")
//     Trace.origin("User", "mouseArea")
class TraceBridge : public QObject {
    Q_OBJECT
public:
    explicit TraceBridge(QObject* parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void enter(const QString& fn,
                           const QString& file = QString(),
                           int line = 0)
    {
        const uint64_t id = trace::nextId();
        if (trace::t_ctx.trace_id == 0)
            trace::t_ctx.trace_id = id;
        const uint64_t parent = trace::t_ctx.parent_id;

        trace::emitLog("enter", id, parent, trace::t_ctx.trace_id,
                       trace::t_ctx,
                       nullptr,          // cls
                       qPrintable(fn),   // fn
                       qPrintable(file), line);

        trace::t_ctx.parent_id = id;
        m_enterIds.append(id);
    }

    Q_INVOKABLE void exit()
    {
        if (m_enterIds.isEmpty()) return;
        const uint64_t id = m_enterIds.takeLast();
        trace::emitLog("exit", id,
                       trace::t_ctx.parent_id,
                       trace::t_ctx.trace_id,
                       trace::t_ctx,
                       nullptr, nullptr, nullptr, 0);
        // Restore previous parent (emitLog doesn't touch t_ctx; do it here).
        trace::t_ctx.parent_id = 0;
    }

    Q_INVOKABLE void origin(const QString& kind, const QString& source)
    {
        trace::Origin o = trace::Origin::Internal;
        if (kind == QLatin1String("User"))       o = trace::Origin::User;
        else if (kind == QLatin1String("Device"))o = trace::Origin::Device;
        else if (kind == QLatin1String("Signal"))o = trace::Origin::Signal;
        else if (kind == QLatin1String("Timer")) o = trace::Origin::Timer;
        else if (kind == QLatin1String("Network"))o = trace::Origin::Network;

        trace::t_ctx.origin    = o;
        trace::t_ctx.source    = qPrintable(source);
        trace::t_ctx.trace_id  = trace::nextId();
        trace::t_ctx.parent_id = 0;
    }

private:
    QList<uint64_t> m_enterIds;
};