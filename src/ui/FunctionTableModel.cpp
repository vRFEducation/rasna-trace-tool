// ui/FunctionTableModel.cpp
#include "FunctionTableModel.h"
#include <QColor>
#include <QFileInfo>

FunctionTableModel::FunctionTableModel(QObject* p) : QAbstractTableModel(p) {}

void FunctionTableModel::setFunctions(const QVector<FunctionInfo>& fns) {
    beginResetModel();
    m_fns = fns;
    endResetModel();
}

int FunctionTableModel::rowCount(const QModelIndex&) const { return m_fns.size(); }
int FunctionTableModel::columnCount(const QModelIndex&) const { return 6; }

QVariant FunctionTableModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_fns.size()) return {};
    const FunctionInfo& f = m_fns[idx.row()];

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
        case 0: return f.className.isEmpty() ? QStringLiteral("(free)") : f.className;
        case 1: return f.name;
        case 2: return f.returnType;
        case 3: return QString::number(f.params.size());
        case 4: {
            QStringList tags;
            if (f.isSignal) tags << "signal";
            if (f.isSlot) tags << "slot";
            if (f.isOverride) tags << "override";
            if (f.isStatic) tags << "static";
            if (f.isConst) tags << "const";
            if (f.isDeleted) tags << "deleted";
            if (f.isDefaulted) tags << "default";
            if (f.isPureVirtual) tags << "pure";
            return tags.join(", ");
        }
        case 5: return QString("%1:%2-%3")
                    .arg(QFileInfo(f.filePath).fileName())
                    .arg(f.openBraceLine).arg(f.closeBraceLine);
        }
    }
    if (role == Qt::ForegroundRole) {
        if (f.isSignal) return QColor("#8e44ad");
        if (f.isDeleted || f.isPureVirtual) return QColor("#95a5a6");
    }
    return {};
}

QVariant FunctionTableModel::headerData(int s, Qt::Orientation o, int role) const {
    if (role != Qt::DisplayRole || o != Qt::Horizontal) return {};
    static const QStringList h = { "Class", "Method", "Return", "Params", "Tags", "Body" };
    return h.value(s);
}
