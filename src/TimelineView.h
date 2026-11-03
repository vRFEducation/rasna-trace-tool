#pragma once
#include <QWidget>
#include "TraceLoader.h"

namespace rt {

class TimelineView : public QWidget {
    Q_OBJECT
public:
    explicit TimelineView(QWidget* parent = nullptr);
    void setTrace(const Trace* t);
    void setSelectedEvent(int eventIndex);
    void setThreadFilter(const QString& thread);  // empty = all

signals:
    void eventClicked(int eventIndex);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    QSize sizeHint() const override { return QSize(800, 260); }

    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    struct Row { QString thread; QVector<int> spans; uint64_t t0 = 0, t1 = 0, minDepth = 0;};
    void rebuildRows();
    double xForTime(uint64_t ts, uint64_t t0, uint64_t t1, double w) const;
    int  rowAtY(int y) const;
    int  spanAt(int row, int x) const;

    const Trace* trace_ = nullptr;
    QVector<Row> rows_;
    QString      threadFilter_;
    uint64_t     t0_ = 0, t1_ = 0;
    int          selected_ = -1;
    double       zoom_ = 1.0;
    double       panNs_ = 0.0;   // offset in ns from t0_
    bool         dragging_ = false;
    QPoint       dragStart_;
    double       dragStartPan_ = 0.0;
};

} // namespace rt
