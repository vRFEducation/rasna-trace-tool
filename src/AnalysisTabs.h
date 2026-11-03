#pragma once
#include <QWidget>
#include "Analyzer.h"

class QTableWidget;
class QTabWidget;

namespace rt {
class TraceModel;

class AnalysisTabs : public QWidget {
    Q_OBJECT
public:
    explicit AnalysisTabs(QWidget* parent = nullptr);

    void setAnalysis(const Analysis& a);
    void setTrace(const Trace* t) { trace_ = t; }

signals:
    void eventSelected(int eventIndex);

private:
    void rebuildTopFunctions();
    void rebuildLayers();
    void rebuildBlocking();
    void rebuildFanIn();
    void rebuildCoupling();
    void rebuildEntries();

    void wireRow(QTableWidget* t, int col);

    const Trace* trace_ = nullptr;
    Analysis     a_;

    QTabWidget*  tabs_     = nullptr;
    QTableWidget* top_     = nullptr;
    QTableWidget* layers_  = nullptr;
    QTableWidget* blocking_= nullptr;
    QTableWidget* fanIn_   = nullptr;
    QTableWidget* coupling_= nullptr;
    QTableWidget* entries_ = nullptr;
};

} // namespace rt
