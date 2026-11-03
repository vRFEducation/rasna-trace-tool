#pragma once
#include <QMainWindow>
#include "Analyzer.h"
#include "TraceLoader.h"

class QTreeView;
class QPlainTextEdit;
class QLabel;
class QLineEdit;

namespace rt {
class TraceModel;
class AnalysisTabs;
class TimelineView;
class TraceFilter;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openFile();
    void reloadFile();
    void onSelectionChanged();
    void onEventRequested(int eventIndex);
    void exportReport();

private:
    void applyTrace();

    QString  currentPath_;
    Trace    trace_;
Analysis analysis_;

    TraceModel*   model_       = nullptr;
    TraceFilter*  filterProxy_ = nullptr;
    AnalysisTabs* tabs_        = nullptr;
    TimelineView* timeline_    = nullptr;

    QTreeView*      tree_   = nullptr;
    QPlainTextEdit* detail_ = nullptr;
    QLabel*         status_ = nullptr;
    QLineEdit*      filter_ = nullptr;
    QString         editorCommand_ = "code -g {file}:{line}";
};

} // namespace rt
