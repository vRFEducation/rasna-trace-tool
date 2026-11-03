#include "TraceModel.h"
#include <QColor>
#include <QFileInfo>
#include <QFont>

namespace rt {

static QString shortFn(const std::string& fn) {
    return QString::fromStdString(fn);
}

static QString layerOf(const Event& e) {
    QString f = QString::fromStdString(e.file).toLower();
    if (f.contains("/ui/") || f.contains("\\ui\\")) return "ui";
    if (f.contains("/app/") || f.contains("\\app\\")) return "app";
    if (f.contains("/services/") || f.contains("\\services\\")) return "services";
    if (f.contains("/infra/") || f.contains("\\infra\\")) return "infra";
    if (f.contains("/domain/") || f.contains("\\domain\\")) return "domain";
    return "?";
}

static QColor layerColor(const QString& l) {
    if (l == "ui")       return QColor(0x4a, 0x90, 0xd9);
    if (l == "app")      return QColor(0x50, 0xb0, 0x6a);
    if (l == "services") return QColor(0xd9, 0xa8, 0x4a);
    if (l == "infra")    return QColor(0xd9, 0x6a, 0x4a);
    if (l == "domain")   return QColor(0x9a, 0x9a, 0x9a);
    return QColor(0x77, 0x77, 0x77);
}

TraceModel::TraceModel(QObject* parent) : QAbstractItemModel(parent) {}

void TraceModel::setTrace(const Trace* trace) {
    beginResetModel();
    trace_ = trace;
    childrenCache_.clear();
    endResetModel();
}

QVector<int> TraceModel::childrenOf(int eventIndex) const {
    QVector<int> kids;
    if (!trace_ || eventIndex < 0 || eventIndex >= (int)trace_->events.size())
        return kids;
    int c = trace_->events[eventIndex].first_child;
    while (c >= 0) {
        kids.push_back(c);
        c = trace_->events[c].next_sibling;
    }
    return kids;
}

// QModelIndex TraceModel::index(int row, int column, const QModelIndex& parent) const {
//     if (!trace_ || row < 0 || column < 0 || column >= ColumnCount)
//         return QModelIndex();

//     if (!parent.isValid()) {
//         if (row >= (int)trace_->roots.size()) return QModelIndex();
//         return createIndex(row, column, (quintptr)trace_->roots[row]);
//     }

//     int pIdx = (int)parent.internalId();
//     QVector<int> kids = childrenOf(pIdx);
//     if (row >= kids.size()) return QModelIndex();
//     return createIndex(row, column, (quintptr)kids[row]);
// }

// QModelIndex TraceModel::parent(const QModelIndex& child) const {
//     if (!child.isValid() || !trace_) return QModelIndex();
//     int eIdx = (int)child.internalId();
//     int pIdx = trace_->events[eIdx].parent_index;
//     if (pIdx < 0) return QModelIndex();
//     // row of pIdx among its own siblings (or roots)
//     int gp = trace_->events[pIdx].parent_index;
//     QVector<int> peers = (gp < 0) ? QVector<int>(trace_->roots.begin(), trace_->roots.end())
//                                   : childrenOf(gp);
//     int row = peers.indexOf(pIdx);
//     if (row < 0) row = 0;
//     return createIndex(row, 0, (quintptr)pIdx);
// }

// int TraceModel::rowCount(const QModelIndex& parent) const {
//     if (!trace_) return 0;
//     if (!parent.isValid()) return (int)trace_->roots.size();
//     if (parent.column() != 0) return 0;
//     int eIdx = (int)parent.internalId();
//     int n = 0;
//     int c = trace_->events[eIdx].first_child;
//     while (c >= 0) { ++n; c = trace_->events[c].next_sibling; }
//     return n;
// }

int TraceModel::rowCount(const QModelIndex& parent) const {
    if (!trace_) return 0;
    if (parent.isValid() && parent.column() != 0) return 0;
    int parentIdx = parent.isValid() ? (int)parent.internalId() : -1;
    return groupsOf(parentIdx).size();
}

int TraceModel::columnCount(const QModelIndex&) const {
    return ColumnCount;
}

// QVariant TraceModel::data(const QModelIndex& index, int role) const {
//     if (!index.isValid() || !trace_) return QVariant();
//     const Event& e = trace_->events[(int)index.internalId()];

//     if (role == Qt::DisplayRole) {
//         switch (index.column()) {
//             case ColName:   return shortFn(e.fn);
//             case ColLayer:  return layerOf(e);
//             case ColThread: return QString::fromStdString(e.thread);
//             case ColTotal:  return QString::number(e.dur_ns / 1e6, 'f', 3) + " ms";
//             case ColSelf: {
//                 uint64_t childSum = 0;
//                 int c = e.first_child;
//                 while (c >= 0) { childSum += trace_->events[c].dur_ns;
//                                  c = trace_->events[c].next_sibling; }
//                 uint64_t self = (e.dur_ns > childSum) ? e.dur_ns - childSum : 0;
//                 return QString::number(self / 1e6, 'f', 3) + " ms";
//             }
//             case ColFile:   return QString("%1:%2")
//                                 .arg(QFileInfo(QString::fromStdString(e.file)).fileName())
//                                 .arg(e.line);
//         }
//     }

//     if (role == Qt::ForegroundRole && index.column() == ColLayer)
//         return layerColor(layerOf(e));

//     if (role == Qt::ToolTipRole) {
//         return QString("%1\n%2\n%3:%4\nthread %5\ntrace %6\nid %7  parent %8")
//             .arg(QString::fromStdString(e.cls))
//             .arg(QString::fromStdString(e.fn))
//             .arg(QString::fromStdString(e.file))
//             .arg(e.line)
//             .arg(QString::fromStdString(e.thread))
//             .arg((qulonglong)e.trace)
//             .arg((qulonglong)e.id)
//             .arg((qulonglong)e.parent);
//     }

//     return QVariant();
// }

QVariant TraceModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    switch (section) {
        case ColName:   return "Function";
        case ColLayer:  return "Layer";
        case ColThread: return "Thread";
        case ColTotal:  return "Total";
        case ColSelf:   return "Self";
        case ColFile:   return "File";
    }
    return QVariant();
}

int TraceModel::indexOfEvent(int eventIndex) const {
    return eventIndex;
}

QModelIndex TraceModel::indexForEvent(int eventIndex, int column) const {
    if (!trace_ || eventIndex < 0) return QModelIndex();
    int pIdx = trace_->events[eventIndex].parent_index;
    QVector<int> peers = (pIdx < 0)
        ? QVector<int>(trace_->roots.begin(), trace_->roots.end())
        : childrenOf(pIdx);
    int row = peers.indexOf(eventIndex);
    if (row < 0) return QModelIndex();
    QModelIndex par = (pIdx < 0) ? QModelIndex()
                                 : indexForEvent(pIdx, 0);
    return index(row, column, par);
}

QVector<TraceModel::Group> TraceModel::groupsOf(int parentIdx) const {
    if (!trace_) return {};
    if (groupCache_.contains(parentIdx)) return groupCache_.value(parentIdx);

    QVector<Group> groups;

    int firstChild = (parentIdx < 0) ? -1 : trace_->events[parentIdx].first_child;
    QVector<int> kids;
    if (parentIdx < 0) {
        kids = QVector<int>(trace_->roots.begin(), trace_->roots.end());
    } else {
        int c = firstChild;
        while (c >= 0) { kids.push_back(c); c = trace_->events[c].next_sibling; }
    }

    if (!collapseRuns_) {
        for (int k : kids) {
            Group g; g.firstEvent = k; g.count = 1;
            g.totalNs = trace_->events[k].dur_ns;
            g.maxNs   = g.totalNs;
            g.minNs   = g.totalNs;
            groups.push_back(g);
        }
        groupCache_.insert(parentIdx, groups);
        return groups;
    }

    int i = 0;
    while (i < kids.size()) {
        const Event& e0 = trace_->events[kids[i]];
        int j = i + 1;
        while (j < kids.size()) {
            const Event& ej = trace_->events[kids[j]];
            if (ej.fn != e0.fn || ej.cls != e0.cls) break;
            ++j;
        }
        Group g;
        g.firstEvent = kids[i];
        g.count      = j - i;
        g.collapsedRun = g.count > 1;
        g.minNs = UINT64_MAX;
        for (int k = i; k < j; ++k) {
            uint64_t d = trace_->events[kids[k]].dur_ns;
            g.totalNs += d;
            if (d > g.maxNs) g.maxNs = d;
            if (d < g.minNs) g.minNs = d;
        }
        if (g.minNs == UINT64_MAX) g.minNs = 0;
        groups.push_back(g);
        i = j;
    }
    groupCache_.insert(parentIdx, groups);
    return groups;
}



QModelIndex TraceModel::index(int row, int column, const QModelIndex& parent) const {
    if (!trace_ || row < 0 || column < 0 || column >= ColumnCount) return {};
    int parentIdx = parent.isValid() ? (int)parent.internalId() : -1;
    const auto gs = groupsOf(parentIdx);
    if (row >= gs.size()) return {};
    return createIndex(row, column, (quintptr)gs[row].firstEvent);
}

QModelIndex TraceModel::parent(const QModelIndex& child) const {
    if (!child.isValid() || !trace_) return {};
    int eIdx = (int)child.internalId();
    int pIdx = trace_->events[eIdx].parent_index;
    if (pIdx < 0) return {};
    int gpIdx = trace_->events[pIdx].parent_index;
    const auto peers = groupsOf(gpIdx);
    int row = -1;
    for (int i = 0; i < peers.size(); ++i)
        if (peers[i].firstEvent == pIdx) { row = i; break; }
    if (row < 0) return {};
    return createIndex(row, 0, (quintptr)pIdx);
}

QVariant TraceModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !trace_) return {};
    const int eIdx = (int)index.internalId();
    const Event& e = trace_->events[eIdx];

    // find the group this row belongs to
    int parentIdx = e.parent_index;
    const auto gs = groupsOf(parentIdx);
    Group g;
    for (const auto& x : gs) if (x.firstEvent == eIdx) { g = x; break; }
    if (g.firstEvent < 0) { g.firstEvent = eIdx; g.count = 1;
        g.totalNs = e.dur_ns; g.maxNs = e.dur_ns; g.minNs = e.dur_ns; }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColName: {
            QString n = QString::fromStdString(e.fn);
            if (g.collapsedRun)
                n += QString("  ×%1  [avg %2, max %3]")
                         .arg(g.count)
                         .arg(g.totalNs / 1e6 / g.count, 0, 'f', 2)
                         .arg(g.maxNs / 1e6, 0, 'f', 2);
            return n;
        }
        case ColLayer:  return layerOf(e);
        case ColThread: return QString::fromStdString(e.thread);
        case ColTotal:  return QString::number(g.totalNs / 1e6, 'f', 3) + " ms";
        case ColSelf: {
            // self time summed across the run, minus children of each instance
            uint64_t childSum = 0;
            // walk actual instances of this run
            if (g.collapsedRun) {
                // find the run in the sibling chain
                int c = (e.parent_index < 0) ? -1 : trace_->events[e.parent_index].first_child;
                QVector<int> chain;
                if (e.parent_index < 0) chain = QVector<int>(trace_->roots.begin(), trace_->roots.end());
                else { while (c >= 0) { chain.push_back(c); c = trace_->events[c].next_sibling; } }
                int k = chain.indexOf(eIdx);
                for (int n = k; n >= 0 && n < chain.size() && n < k + g.count; ++n) {
                    int cc = trace_->events[chain[n]].first_child;
                    while (cc >= 0) { childSum += trace_->events[cc].dur_ns;
                        cc = trace_->events[cc].next_sibling; }
                }
            } else {
                int c = e.first_child;
                while (c >= 0) { childSum += trace_->events[c].dur_ns;
                    c = trace_->events[c].next_sibling; }
            }
            uint64_t self = (g.totalNs > childSum) ? g.totalNs - childSum : 0;
            return QString::number(self / 1e6, 'f', 3) + " ms";
        }
        case ColFile:   return QString("%1:%2")
                .arg(QFileInfo(QString::fromStdString(e.file)).fileName())
                .arg(e.line);
        }
    }
    // ... foreground/tooltip as before, optionally add "(×N)" to tooltip ...
    return {};
}

void TraceModel::setCollapseRuns(bool on) {
    if (collapseRuns_ == on) return;
    beginResetModel();
    collapseRuns_ = on;
    groupCache_.clear();
    endResetModel();
}

bool TraceModel::isGroupRow(const QModelIndex& idx) const {
    if (!idx.isValid() || !trace_) return false;
    int eIdx = (int)idx.internalId();
    int parentIdx = trace_->events[eIdx].parent_index;
    for (const auto& g : groupsOf(parentIdx))
        if (g.firstEvent == eIdx) return g.collapsedRun;
    return false;
}

int TraceModel::groupEventIndex(const QModelIndex& idx) const {
    if (!idx.isValid()) return -1;
    return (int)idx.internalId();
}

int TraceModel::groupCount(const QModelIndex& idx) const {
    if (!idx.isValid() || !trace_) return 1;
    int eIdx = (int)idx.internalId();
    int parentIdx = trace_->events[eIdx].parent_index;
    for (const auto& g : groupsOf(parentIdx))
        if (g.firstEvent == eIdx) return g.count;
    return 1;
}



} // namespace rt
