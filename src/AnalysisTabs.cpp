#include "AnalysisTabs.h"
#include "TraceLoader.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QHash>
#include <algorithm>

namespace rt {

static QTableWidget* makeTable(const QStringList& headers) {
    auto* t = new QTableWidget;
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setSortingEnabled(false);
    return t;
}

AnalysisTabs::AnalysisTabs(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    tabs_ = new QTabWidget;
    layout->addWidget(tabs_);

    top_      = makeTable({"Function", "Calls", "Total ms", "Avg ms", "Max ms"});
    layers_   = makeTable({"Count", "Caller layer", "Callee layer", "Via"});
    blocking_ = makeTable({"Duration ms", "Layer", "Function", "Thread", "File"});
    fanIn_    = makeTable({"Function", "Distinct parents", "Calls"});
    coupling_ = makeTable({"First", "Second", "Occurrences", "Groups", "Ratio"});
    entries_  = makeTable({"Function", "Count"});

    tabs_->addTab(top_,      "Top functions");
    tabs_->addTab(layers_,   "Layer violations");
    tabs_->addTab(blocking_, "Blocking");
    tabs_->addTab(fanIn_,    "Fan-in");
    tabs_->addTab(coupling_, "Coupling");
    tabs_->addTab(entries_,  "Entry points");

    // wire rows -> eventSelected
    auto wire = [this](QTableWidget* t, int col){
        connect(t, &QTableWidget::cellDoubleClicked,
                this, [this, t, col](int row, int){
            auto* item = t->item(row, col);
            if (!item) return;
            int ev = item->data(Qt::UserRole).toInt();
            if (ev >= 0) emit eventSelected(ev);
        });
    };
    wire(top_,      0);
    wire(layers_,   0);
    wire(blocking_, 0);
    wire(fanIn_,    0);
    wire(coupling_, 0);
    wire(entries_,  0);
}

void AnalysisTabs::setAnalysis(const Analysis& a) {
    a_ = a;
    rebuildTopFunctions();
    rebuildLayers();
    rebuildBlocking();
    rebuildFanIn();
    rebuildCoupling();
    rebuildEntries();
}

void AnalysisTabs::rebuildTopFunctions() {
    top_->setRowCount(0);
    if (!trace_) return;

    struct Agg { QString fn; int calls = 0; uint64_t total = 0; uint64_t maxv = 0; int ev = -1; };
    QHash<QString, Agg> agg;
    for (size_t i = 0; i < trace_->events.size(); ++i) {
        const auto& e = trace_->events[i];
        if (!e.isEnter()) continue;
        QString fn = QString::fromStdString(e.fn);
        auto& x = agg[fn];
        if (x.calls == 0) { x.fn = fn; x.ev = (int)i; }
        x.calls++;
        x.total += e.dur_ns;
        if (e.dur_ns > x.maxv) x.maxv = e.dur_ns;
    }
    QVector<Agg> rows;
    rows.reserve(agg.size());
    for (auto it = agg.begin(); it != agg.end(); ++it) rows.push_back(it.value());
    std::sort(rows.begin(), rows.end(),
              [](const Agg& a, const Agg& b){ return a.total > b.total; });

    top_->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const auto& a = rows[r];
        auto set = [&](int c, const QString& s){
            auto* it = new QTableWidgetItem(s);
            it->setData(Qt::UserRole, a.ev);
            top_->setItem(r, c, it);
        };
        set(0, a.fn);
        set(1, QString::number(a.calls));
        set(2, QString::number(a.total / 1e6, 'f', 3));
        set(3, QString::number(a.total / 1e6 / a.calls, 'f', 3));
        set(4, QString::number(a.maxv / 1e6, 'f', 3));
    }
    top_->resizeColumnsToContents();
}

void AnalysisTabs::rebuildLayers() {
    layers_->setRowCount(a_.violations.size());
    for (int r = 0; r < (int)a_.violations.size(); ++r) {
        const auto& v = a_.violations[r];
        auto set = [&](int c, const QString& s){
            auto* it = new QTableWidgetItem(s);
            it->setData(Qt::UserRole, v.sampleEvent);
            layers_->setItem(r, c, it);
        };
        set(0, QString::number(v.count) + "x");
        set(1, v.callerLayer);
        set(2, v.calleeLayer);
        set(3, v.via);
    }
    layers_->resizeColumnsToContents();
}

void AnalysisTabs::rebuildBlocking() {
    blocking_->setRowCount(a_.blocking.size());
    for (int r = 0; r < (int)a_.blocking.size(); ++r) {
        const auto& b = a_.blocking[r];
        auto set = [&](int c, const QString& s){
            auto* it = new QTableWidgetItem(s);
            it->setData(Qt::UserRole, b.eventIndex);
            blocking_->setItem(r, c, it);
        };
        set(0, QString::number(b.dur_ns / 1e6, 'f', 2));
        set(1, b.layer);
        set(2, b.fn);
        set(3, b.thread);
        set(4, QFileInfo(b.file).fileName() + ":" + QString::number(b.line));
    }
    blocking_->resizeColumnsToContents();
}

void AnalysisTabs::rebuildFanIn() {
    fanIn_->setRowCount(a_.fanIn.size());
    for (int r = 0; r < (int)a_.fanIn.size(); ++r) {
        const auto& f = a_.fanIn[r];
        auto set = [&](int c, const QString& s){
            auto* it = new QTableWidgetItem(s);
            it->setData(Qt::UserRole, -1);
            fanIn_->setItem(r, c, it);
        };
        set(0, f.fn);
        set(1, QString::number(f.distinctParents));
        set(2, QString::number(f.calls));
    }
    fanIn_->resizeColumnsToContents();
}

void AnalysisTabs::rebuildCoupling() {
    coupling_->setRowCount(a_.coupling.size());
    for (int r = 0; r < (int)a_.coupling.size(); ++r) {
        const auto& c = a_.coupling[r];
        auto set = [&](int col, const QString& s){
            auto* it = new QTableWidgetItem(s);
            it->setData(Qt::UserRole, -1);
            coupling_->setItem(r, col, it);
        };
        set(0, c.first);
        set(1, c.second);
        set(2, QString::number(c.occurrences));
        set(3, QString::number(c.totalGroups));
        set(4, QString::number(c.ratio * 100, 'f', 1) + "%");
    }
    coupling_->resizeColumnsToContents();
}

void AnalysisTabs::rebuildEntries() {
    entries_->setRowCount(a_.entries.size());
    for (int r = 0; r < (int)a_.entries.size(); ++r) {
        const auto& e = a_.entries[r];
        auto* a = new QTableWidgetItem(e.fn);
        auto* b = new QTableWidgetItem(QString::number(e.count));
        a->setData(Qt::UserRole, -1);
        b->setData(Qt::UserRole, -1);
        entries_->setItem(r, 0, a);
        entries_->setItem(r, 1, b);
    }
    entries_->resizeColumnsToContents();
}

void AnalysisTabs::wireRow(QTableWidget*, int) {}

} // namespace rt