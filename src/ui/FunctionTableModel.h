// ui/FunctionTableModel.h
#pragma once
#include <QAbstractTableModel>
#include "core/FunctionInfo.h"

class FunctionTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit FunctionTableModel(QObject* parent = nullptr);
    void setFunctions(const QVector<FunctionInfo>& fns);
    const FunctionInfo& at(int row) const { return m_fns[row]; }

    int rowCount(const QModelIndex& = {}) const override;
    int columnCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& idx, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
    QVector<FunctionInfo> m_fns;
};