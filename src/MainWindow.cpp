#include "MainWindow.h"
#include "TraceModel.h"
#include "TraceFilter.h"
#include "TimelineView.h"
#include "AnalysisTabs.h"
#include "Analyzer.h"
#include "ReportWriter.h"

#include <QAction>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeView>

namespace rt {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Ronia Trace Viewer");
    resize(1500, 950);

    QSettings settings;
    editorCommand_ = settings.value("editorCommand", editorCommand_).toString();

    // ---- menu ----
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Open trace...", QKeySequence::Open,
                        this, &MainWindow::openFile);
    fileMenu->addAction("&Reload", QKeySequence::Refresh,
                        this, &MainWindow::reloadFile);
    fileMenu->addSeparator();
    fileMenu->addAction("&Export HTML report...", QKeySequence("Ctrl+E"),
                    this, &MainWindow::exportReport);

    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", QKeySequence::Quit, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* collapseAction = viewMenu->addAction("Collapse consecutive calls");
    collapseAction->setCheckable(true);
    collapseAction->setChecked(true);

    // ---- toolbar ----
    auto* tb = addToolBar("main");
    tb->setMovable(false);
    tb->addAction("Open",   this, &MainWindow::openFile);
    tb->addAction("Reload", this, &MainWindow::reloadFile);
    tb->addSeparator();
    filter_ = new QLineEdit;
    filter_->setPlaceholderText("Filter by function name...");
    filter_->setClearButtonEnabled(true);
    filter_->setMaximumWidth(280);
    tb->addWidget(filter_);

    // ---- splitters ----
    auto* mainSplit = new QSplitter(Qt::Vertical, this);
    auto* topSplit  = new QSplitter(Qt::Horizontal, mainSplit);

    // ---- tree + proxy ----
    model_ = new TraceModel(this);

    filterProxy_ = new TraceFilter(this);
    filterProxy_->setSourceModel(model_);
    filterProxy_->setFilterKeyColumn(TraceModel::ColName);

    tree_ = new QTreeView;
    tree_->setModel(filterProxy_);
    tree_->setUniformRowHeights(true);
    tree_->setAlternatingRowColors(true);
    tree_->setSortingEnabled(false);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->header()->setStretchLastSection(true);
    tree_->header()->resizeSection(TraceModel::ColName,   340);
    tree_->header()->resizeSection(TraceModel::ColLayer,   70);
    tree_->header()->resizeSection(TraceModel::ColThread,  80);
    tree_->header()->resizeSection(TraceModel::ColTotal,   90);
    tree_->header()->resizeSection(TraceModel::ColSelf,    90);

    // ---- detail ----
    detail_ = new QPlainTextEdit;
    detail_->setReadOnly(true);
    detail_->setPlaceholderText("Select a call in the tree.");
    detail_->setStyleSheet("font-family: Consolas, monospace;");

    topSplit->addWidget(tree_);
    topSplit->addWidget(detail_);
    topSplit->setStretchFactor(0, 3);
    topSplit->setStretchFactor(1, 2);

    // ---- tabs ----
    tabs_ = new AnalysisTabs;
    connect(tabs_, &AnalysisTabs::eventSelected,
            this,  &MainWindow::onEventRequested);

    // ---- timeline ----
    timeline_ = new TimelineView;
    connect(timeline_, &TimelineView::eventClicked,
            this,      &MainWindow::onEventRequested);

    // ---- assemble ----
    mainSplit->addWidget(topSplit);
    mainSplit->addWidget(timeline_);
    mainSplit->addWidget(tabs_);
    mainSplit->setStretchFactor(0, 3);
    mainSplit->setStretchFactor(1, 2);
    mainSplit->setStretchFactor(2, 2);
    setCentralWidget(mainSplit);

    status_ = new QLabel("No trace loaded. File → Open trace...");
    statusBar()->addPermanentWidget(status_, 1);

    // ---- connections ----
    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onSelectionChanged);

    connect(filter_, &QLineEdit::textChanged, this, [this](const QString& s) {
        filterProxy_->setNeedle(s);
        if (!s.isEmpty()) tree_->expandAll();
    });

    connect(collapseAction, &QAction::toggled, this, [this](bool on) {
        model_->setCollapseRuns(on);
        tree_->expandToDepth(1);
    });

    connect(tree_, &QTreeView::customContextMenuRequested,
            this, [this](const QPoint& pos) {
                QModelIndex idx = tree_->indexAt(pos);
                if (!idx.isValid()) return;
                QModelIndex src = filterProxy_->mapToSource(idx);
                int eIdx = (int)src.internalId();
                if (eIdx < 0 || eIdx >= (int)trace_.events.size()) return;
                const Event& e = trace_.events[eIdx];

                QMenu m;
                auto* openAct = m.addAction("Open in editor");
                auto* copyAct = m.addAction("Copy file:line");
                QAction* chosen = m.exec(tree_->viewport()->mapToGlobal(pos));
                if (chosen == openAct) {
                    QString cmd = editorCommand_;
                    cmd.replace("{file}", QString::fromStdString(e.file));
                    cmd.replace("{line}", QString::number(e.line));
                    QProcess::startDetached(cmd);
                } else if (chosen == copyAct) {
                    QGuiApplication::clipboard()->setText(
                        QString("%1:%2").arg(QString::fromStdString(e.file)).arg(e.line));
                }
            });
}

void MainWindow::openFile() {
    const QString start = currentPath_.isEmpty()
    ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
    : QFileInfo(currentPath_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, "Open trace log", start,
        "Trace logs (*.log *.jsonl *.json *.txt);;All files (*)");
    if (path.isEmpty()) return;
    currentPath_ = path;
    applyTrace();
}

void MainWindow::reloadFile() {
    if (currentPath_.isEmpty()) { openFile(); return; }
    applyTrace();
}

void MainWindow::applyTrace() {
    std::string err;
    Trace fresh;
    if (!loadTrace(currentPath_.toStdString(), fresh, err)) {
        QMessageBox::warning(this, "Load failed",
                             QString::fromStdString(err) + "\n\n" + currentPath_);
        return;
    }
    trace_ = std::move(fresh);

    model_->setTrace(&trace_);
    tree_->expandToDepth(1);
    tree_->resizeColumnToContents(TraceModel::ColName);

    tabs_->setTrace(&trace_);
    analysis_ = analyze(trace_);
    tabs_->setAnalysis(analysis_);

    timeline_->setTrace(&trace_);

    const auto& s = trace_.stats;
    double span = (s.last_ts > s.first_ts) ? (s.last_ts - s.first_ts) / 1e9 : 0.0;
    status_->setText(QString(
                         "%1 events | %2 enters / %3 exits | %4 orphan | %5 dropped | "
                         "%6 threads | %7 traces | span %8 s")
                         .arg(s.total).arg(s.enters).arg(s.exits).arg(s.orphanExits)
                         .arg(s.dropped).arg(s.threads).arg(s.traces).arg(span, 0, 'f', 3));

    setWindowTitle(QString("Ronia Trace Viewer — %1")
                       .arg(QFileInfo(currentPath_).fileName()));
}

void MainWindow::onSelectionChanged() {
    QModelIndex proxyIdx = tree_->currentIndex();
    if (!proxyIdx.isValid()) { detail_->clear(); return; }

    QModelIndex srcIdx = filterProxy_->mapToSource(proxyIdx);
    int eIdx = (int)srcIdx.internalId();
    if (eIdx < 0 || eIdx >= (int)trace_.events.size()) return;

    const Event& e = trace_.events[eIdx];

    QString t;
    t += QString("ev:     %1\n").arg(QString::fromStdString(e.ev));
    t += QString("fn:     %1\n").arg(QString::fromStdString(e.fn));
    t += QString("cls:    %1\n").arg(QString::fromStdString(e.cls));
    t += QString("file:   %1\n").arg(QString::fromStdString(e.file));
    t += QString("line:   %1\n").arg(e.line);
    t += QString("thread: %1\n").arg(QString::fromStdString(e.thread));
    t += QString("id:     %1\n").arg((qulonglong)e.id);
    t += QString("parent: %1\n").arg((qulonglong)e.parent);
    t += QString("trace:  %1\n").arg((qulonglong)e.trace);
    t += QString("origin: %1\n").arg(e.origin);
    t += QString("ts:     %1\n").arg((qulonglong)e.ts);
    t += QString("dur:    %1 ms\n").arg(e.dur_ns / 1e6, 0, 'f', 3);

    int n = model_->groupCount(srcIdx);
    if (n > 1)
        t += QString("group:  ×%1 collapsed\n").arg(n);

    detail_->setPlainText(t);
    timeline_->setSelectedEvent(eIdx);
}

void MainWindow::onEventRequested(int eventIndex) {
    if (eventIndex < 0 || eventIndex >= (int)trace_.events.size()) return;

    // map source-model index -> proxy index, so the view finds it
    QModelIndex srcIdx = model_->indexForEvent(eventIndex, 0);
    if (!srcIdx.isValid()) return;
    QModelIndex proxyIdx = filterProxy_->mapFromSource(srcIdx);
    if (!proxyIdx.isValid()) return;   // hidden by filter — nothing to do

    tree_->setCurrentIndex(proxyIdx);
    tree_->scrollTo(proxyIdx, QAbstractItemView::PositionAtCenter);

    QModelIndex parent = proxyIdx.parent();
    while (parent.isValid()) {
        tree_->expand(parent);
        parent = parent.parent();
    }

    timeline_->setSelectedEvent(eventIndex);
}

void MainWindow::exportReport() {
    if (trace_.events.empty()) {
        QMessageBox::information(this, "Export report",
                                 "No trace loaded. Open a trace first.");
        return;
    }

    const QString start = currentPath_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(currentPath_).absolutePath();
    const QString suggested = currentPath_.isEmpty()
        ? QStringLiteral("trace-report.html")
        : QFileInfo(currentPath_).baseName() + "-report.html";
    const QString path = QFileDialog::getSaveFileName(
        this, "Save HTML report",
        start + "/" + suggested,
        "HTML (*.html *.htm);;All files (*)");
    if (path.isEmpty()) return;

    ReportOptions opts;
    opts.title = QFileInfo(currentPath_).fileName() + " — trace report";

    // We already computed an Analysis on load; reuse it.
    // (If you don't cache it, call analyze(trace_) here — it's fast.)
    const Analysis a = analyze(trace_);

    QString err;
    if (!writeHtmlReport(path, trace_, a, opts, &err)) {
        QMessageBox::warning(this, "Export failed", err + "\n\n" + path);
        return;
    }

    // Offer to open it
    const auto r = QMessageBox::question(
        this, "Export complete",
        QString("Report written to:\n%1\n\nOpen in browser?").arg(path),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (r == QMessageBox::Yes) {
#if defined(Q_OS_WIN)
        QProcess::startDetached("cmd", {"/c", "start", "", path});
#elif defined(Q_OS_MAC)
        QProcess::startDetached("open", {path});
#else
        QProcess::startDetached("xdg-open", {path});
#endif
    }
}

} // namespace rt
