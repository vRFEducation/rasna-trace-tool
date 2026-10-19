#include "TimelineView.h"
#include "TraceModel.h"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QToolTip>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rt {

static inline void visibleRange(uint64_t t0, uint64_t t1, double zoom, double panNs,
                                uint64_t& vt0, uint64_t& vt1) {
    const double span = double(t1 - t0) / std::max(1.0, zoom);
    vt0 = t0 + (uint64_t)std::max(0.0, panNs);
    vt1 = vt0 + (uint64_t)span;
}

static QColor layerColor(const QString& file) {
    QString f = file.toLower(); f.replace('\\', '/');
    if (f.contains("/ui/"))       return QColor(0x4a, 0x90, 0xd9);
    if (f.contains("/app/"))      return QColor(0x50, 0xb0, 0x6a);
    if (f.contains("/services/")) return QColor(0xd9, 0xa8, 0x4a);
    if (f.contains("/infra/"))    return QColor(0xd9, 0x6a, 0x4a);
    if (f.contains("/domain/"))   return QColor(0x9a, 0x9a, 0x9a);
    return QColor(0x88, 0x88, 0x88);
}

TimelineView::TimelineView(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(180);
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, QColor(0x1e, 0x1e, 0x1e));
    p.setColor(QPalette::WindowText, Qt::white);
    setPalette(p);
}

void TimelineView::setTrace(const Trace* t) {
    trace_ = t;
    selected_ = -1;
    rebuildRows();
    update();
}

void TimelineView::setSelectedEvent(int eventIndex) {
    selected_ = eventIndex;
    update();
}

void TimelineView::setThreadFilter(const QString& thread) {
    threadFilter_ = thread;
    rebuildRows();
    update();
}

void TimelineView::rebuildRows() {
    rows_.clear();
    if (!trace_) return;

    t0_ = trace_->stats.first_ts;
    t1_ = trace_->stats.last_ts;

    QHash<QString, Row> byThread;
    for (size_t i = 0; i < trace_->events.size(); ++i) {
        const Event& e = trace_->events[i];
        if (!e.isEnter()) continue;
        if (!threadFilter_.isEmpty() &&
            QString::fromStdString(e.thread) != threadFilter_) continue;
        Row& r = byThread[QString::fromStdString(e.thread)];
        r.thread = QString::fromStdString(e.thread);
        r.spans.push_back((int)i);
    }

    for (auto it = byThread.begin(); it != byThread.end(); ++it) {
        std::sort(it->spans.begin(), it->spans.end(), [&](int a, int b){
            return trace_->events[a].ts < trace_->events[b].ts;
        });
        rows_.push_back(it.value());
    }
    std::sort(rows_.begin(), rows_.end(), [](const Row& a, const Row& b){
        return a.thread < b.thread;
    });
}

double TimelineView::xForTime(uint64_t ts, uint64_t t0, uint64_t t1, double w) const {
    if (t1 == t0) return 0;
    return (double(ts - t0) / double(t1 - t0)) * w;
}

int TimelineView::rowAtY(int y) const {
    const int rowH = 22;
    int idx = (y - 20) / rowH;
    if (idx < 0 || idx >= rows_.size()) return -1;
    return idx;
}

int TimelineView::spanAt(int row, int x) const {
    if (row < 0 || row >= rows_.size() || !trace_) return -1;
    uint64_t vt0, vt1;
    visibleRange(t0_, t1_, zoom_, panNs_, vt0, vt1);

    const auto& r = rows_[row];
    for (int i = r.spans.size() - 1; i >= 0; --i) {
        const Event& e = trace_->events[r.spans[i]];
        double x0 = xForTime(e.ts,          vt0, vt1, width());
        double x1 = xForTime(e.ts + e.dur_ns, vt0, vt1, width());
        if (x1 - x0 < 2) x1 = x0 + 2;   // tiny bars still clickable
        if (x >= x0 && x <= x1) return r.spans[i];
    }
    return -1;
}

void TimelineView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());

    if (!trace_ || rows_.isEmpty()) {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, "Timeline (load a trace)");
        return;
    }

    const int rowH = 22;
    const int headerH = 20;
    double visibleSpan = (t1_ - t0_) / zoom_;
    uint64_t vt0, vt1;
    visibleRange(t0_, t1_, zoom_, panNs_, vt0, vt1);

    for (int ri = 0; ri < rows_.size(); ++ri) {
        const Row& r = rows_[ri];
        int y = headerH + ri * rowH;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x2a, 0x2a, 0x2a));
        p.drawRect(0, y, width(), rowH - 1);

        p.setPen(Qt::white);
        p.drawText(QRect(4, y, 80, rowH), Qt::AlignVCenter, "t=" + r.thread);

        for (int eIdx : r.spans) {
            const Event& e = trace_->events[eIdx];
            if (e.ts + e.dur_ns < vt0 || e.ts > vt1) continue;

            double x0 = xForTime(e.ts,          vt0, vt1, width());
            double x1 = xForTime(e.ts + e.dur_ns, vt0, vt1, width());
            if (x1 - x0 < 0.5) x1 = x0 + 0.5;

            // per-thread depth, so nesting is visible
            int d = e.depth - r.minDepth;         // add minDepth to Row struct
            double yTop = y + 2 + d * 4;          // 4 px indent per level
            double h    = rowH - 6 - d * 4;
            if (h < 3) h = 3;

            QColor c = layerColor(QString::fromStdString(e.file));
            if (eIdx == selected_) c = c.lighter(140);
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawRect(QRectF(x0, yTop, x1 - x0, h));
        }
    }

    // time ruler
    p.setPen(Qt::gray);
    for (int i = 0; i <= 10; ++i) {
        int x = width() * i / 10;
        p.drawLine(x, 0, x, headerH - 4);
        double offsetMs = (double(vt0 - t0_) + (double(vt1 - vt0) * i / 10.0)) / 1e6;
        p.drawText(x + 3, headerH - 6, QString::number(offsetMs, 'f', 1));
    }

}

void TimelineView::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton) {
        int row = rowAtY(ev->pos().y());
        int e = spanAt(row, ev->pos().x());
        if (e >= 0) emit eventClicked(e);
        return;   // don't start a drag
    }
    if (ev->button() == Qt::MiddleButton) {
        dragging_ = true;
        dragStart_ = ev->pos();
        dragStartPan_ = panNs_;
        setCursor(Qt::ClosedHandCursor);
    }
}

// void TimelineView::wheelEvent(QWheelEvent* ev) {
//     const double factor = (ev->angleDelta().y() > 0) ? 1.2 : 1.0 / 1.2;
//     zoom_ = std::clamp(zoom_ * factor, 0.1, 2000.0);
//     update();
// }

void TimelineView::wheelEvent(QWheelEvent* ev) {
    if (!trace_) return;

    const double factor = (ev->angleDelta().y() > 0) ? 1.2 : 1.0 / 1.2;
    const double oldZoom = zoom_;
    zoom_ = std::clamp(zoom_ * factor, 1.0, 10000.0);

    const double w = std::max(1.0, double(width()));
    const double mouseX = ev->position().x();
    const double visibleSpanOld = double(t1_ - t0_) / oldZoom;
    const double visibleSpanNew = double(t1_ - t0_) / zoom_;

    // Absolute timestamp under the cursor (relative to t0_).
    const double anchorOffset = panNs_ + (mouseX / w) * visibleSpanOld;

    // After zoom, place the same timestamp at the same screen fraction.
    panNs_ = anchorOffset - (mouseX / w) * visibleSpanNew;

    // Clamp so we never scroll past either end.
    const double maxPan = double(t1_ - t0_) - visibleSpanNew;
    panNs_ = std::clamp(panNs_, 0.0, std::max(0.0, maxPan));

    update();
}
void TimelineView::mouseDoubleClickEvent(QMouseEvent*) {
    zoom_ = 1.0;
    panNs_ = 0.0;
    update();
}

void TimelineView::mouseMoveEvent(QMouseEvent* ev) {
    if (dragging_) {
        const double dx = ev->pos().x() - dragStart_.x();
        const double visibleSpan = double(t1_ - t0_) / zoom_;
        const double w = std::max(1.0, double(width()));
        double newPan = dragStartPan_ - dx / w * visibleSpan;
        const double maxPan = double(t1_ - t0_) - visibleSpan;
        panNs_ = std::clamp(newPan, 0.0, std::max(0.0, maxPan));
        update();
    }

    // tooltip
    int row = rowAtY(ev->pos().y());
    int e = spanAt(row, ev->pos().x());
    if (e >= 0) {
        const Event& evt = trace_->events[e];
        QToolTip::showText(ev->globalPosition().toPoint(),
                           QString("%1\n%2 ms\n%3:%4")
                               .arg(QString::fromStdString(evt.fn))
                               .arg(evt.dur_ns / 1e6, 0, 'f', 3)
                               .arg(QString::fromStdString(evt.file))
                               .arg(evt.line),
                           this);
    } else {
        QToolTip::hideText();
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent*) {
    if (dragging_) {
        dragging_ = false;
        unsetCursor();
    }
}

} // namespace rt
