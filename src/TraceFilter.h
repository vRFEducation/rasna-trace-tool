#pragma once
#include <QSortFilterProxyModel>

namespace rt {

class TraceFilter : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit TraceFilter(QObject* parent = nullptr);
    void setNeedle(const QString& s);
    QString needle() const { return needle_; }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
    bool nodeMatches(const QModelIndex& idx) const;
    QString needle_;
};

} // namespace rt