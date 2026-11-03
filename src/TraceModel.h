#pragma once
#include "TraceLoader.h"
#include <QAbstractItemModel>
#include <QVector>

namespace rt {



class TraceModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Column {
        ColName = 0,
        ColLayer,
        ColThread,
        ColTotal,
        ColSelf,
        ColFile,
        ColumnCount
    };

    struct Group {
        int firstEvent = -1;    // representative (first of the run)
        int count = 1;          // how many identical siblings
        uint64_t totalNs = 0;
        uint64_t maxNs   = 0;
        uint64_t minNs   = 0;
        bool collapsedRun = false;  // true if count > 1
    };

    explicit TraceModel(QObject* parent = nullptr);
    void setTrace(const Trace* trace);
    const Trace* trace() const { return trace_; }

    int indexOfEvent(int eventIndex) const;

    QModelIndex index(int row, int column,
                      const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    QModelIndex indexForEvent(int eventIndex, int column = 0) const;

    void setCollapseRuns(bool on);
    bool collapseRuns() const { return collapseRuns_; }
    // is this row a collapsed group (count > 1)?
    bool isGroupRow(const QModelIndex& idx) const;
    int  groupEventIndex(const QModelIndex& idx) const;   // representative event
    int  groupCount(const QModelIndex& idx) const;

private:
    const Trace* trace_ = nullptr;
    mutable QHash<int, QVector<int>> childrenCache_;
    QVector<int> childrenOf(int eventIndex) const;

    bool collapseRuns_ = true;
    mutable QHash<int, QVector<Group>> groupCache_;   // parentEventIndex -> groups
    QVector<Group> groupsOf(int parentEventIndex) const;

};

} // namespace rt
