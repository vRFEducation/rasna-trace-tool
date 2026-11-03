#include "TraceFilter.h"

namespace rt {

TraceFilter::TraceFilter(QObject* parent) : QSortFilterProxyModel(parent) {
    setRecursiveFilteringEnabled(true);   // Qt 5.10+
}

void TraceFilter::setNeedle(const QString& s) {
    if (needle_ == s) return;
    needle_ = s;
    invalidateFilter();
}

bool TraceFilter::nodeMatches(const QModelIndex& idx) const {
    const QString fn = idx.data(Qt::DisplayRole).toString();
    return fn.contains(needle_, Qt::CaseInsensitive);
}

bool TraceFilter::filterAcceptsRow(int row, const QModelIndex& parent) const {
    if (needle_.isEmpty()) return true;
    QModelIndex idx = sourceModel()->index(row, 0, parent);
    if (!idx.isValid()) return false;
    if (nodeMatches(idx)) return true;
    // keep if any descendant matches (walk down)
    const int kids = sourceModel()->rowCount(idx);
    for (int r = 0; r < kids; ++r)
        if (filterAcceptsRow(r, idx)) return true;
    return false;
}

} // namespace rt