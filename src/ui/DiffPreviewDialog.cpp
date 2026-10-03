// ui/DiffPreviewDialog.cpp
#include "DiffPreviewDialog.h"
#include "core/EditApplier.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QSplitter>
#include <QFileInfo>
#include <QFontDatabase>
#include <QTextBlock>

DiffPreviewDialog::DiffPreviewDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Preview Changes");
    resize(1100, 700);

    auto* root = new QVBoxLayout(this);

    auto* topRow = new QHBoxLayout;
    m_btnToggleAll = new QPushButton("Deselect all");
    topRow->addWidget(m_btnToggleAll);
    topRow->addStretch();
    auto* summary = new QLabel("Only checked files will be modified.");
    summary->setStyleSheet("color:#9aa0a6;");
    topRow->addWidget(summary);
    root->addLayout(topRow);

    auto* split = new QSplitter(Qt::Horizontal, this);
    m_tree = new QTreeWidget(split);
    m_tree->setHeaderLabels({ "File", "Edits" });
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    m_preview = new QPlainTextEdit(split);
    m_preview->setReadOnly(true);
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_preview->setFont(f);
    m_preview->setLineWrapMode(QPlainTextEdit::NoWrap);

    split->addWidget(m_tree);
    split->addWidget(m_preview);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    root->addWidget(split, 1);

    auto* bottom = new QHBoxLayout;
    bottom->addStretch();
    m_btnCancel = new QPushButton("Cancel");
    m_btnApply  = new QPushButton("Apply");
    m_btnApply->setDefault(true);
    bottom->addWidget(m_btnCancel);
    bottom->addWidget(m_btnApply);
    root->addLayout(bottom);

    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnApply,  &QPushButton::clicked, this, &QDialog::accept);
    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &DiffPreviewDialog::onFileSelected);
    connect(m_btnToggleAll, &QPushButton::clicked,
            this, &DiffPreviewDialog::onToggleAll);
}

void DiffPreviewDialog::setPlans(QVector<FilePlan> plans) {
    m_plans = std::move(plans);
    m_fileEnabled.assign(m_plans.size(), true);

    m_tree->clear();
    for (int i = 0; i < m_plans.size(); ++i) {
        const auto& p = m_plans[i];
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, QFileInfo(p.filePath).fileName());
        item->setText(1, QString::number(p.edits.size()));
        item->setToolTip(0, p.filePath);
        item->setData(0, Qt::UserRole, i);
        item->setCheckState(0, Qt::Checked);
    }
    if (m_tree->topLevelItemCount() > 0)
        m_tree->setCurrentItem(m_tree->topLevelItem(0));

    // Update preview once on open
    onFileSelected();
}

void DiffPreviewDialog::onFileSelected() {
    auto items = m_tree->selectedItems();
    if (items.isEmpty()) return;
    const int idx = items.first()->data(0, Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_plans.size()) return;

    const auto& p = m_plans[idx];
    bool ok = true;
    const QStringList modified = EditApplier::apply(p.originalLines, p.edits, &ok);
    const QString diff = EditApplier::makeDiff(p.originalLines, modified,
                                               QFileInfo(p.filePath).fileName());

    m_preview->setPlainText(diff);

    // Simple colored rendering: green for +, red for -, gray for context.
    // (Implemented via extra selections so we don't fight syntax highlighting.)
    QList<QTextEdit::ExtraSelection> sels;
    QTextBlock block = m_preview->document()->firstBlock();
    while (block.isValid()) {
        const QString t = block.text();
        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(block);
        sel.cursor.select(QTextCursor::LineUnderCursor);
        if (t.startsWith('+'))      sel.format.setForeground(QColor("#4ec9b0"));
        else if (t.startsWith('-')) sel.format.setForeground(QColor("#f48771"));
        else if (t.startsWith("@@")||t.startsWith("---")||t.startsWith("+++"))
            sel.format.setForeground(QColor("#569cd6"));
        else                        sel.format.setForeground(QColor("#9aa0a6"));
        sels.append(sel);
        block = block.next();
    }
    m_preview->setExtraSelections(sels);
}

void DiffPreviewDialog::onToggleAll(bool /*checked*/) {
    // Toggle based on the current button label so it acts as a flip-flop.
    const bool select = (m_btnToggleAll->text() == "Select all");
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        item->setCheckState(0, select ? Qt::Checked : Qt::Unchecked);
    }
    m_btnToggleAll->setText(select ? "Deselect all" : "Select all");
}

QVector<DiffPreviewDialog::FilePlan> DiffPreviewDialog::selectedPlans() const {
    QVector<FilePlan> out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        if (item->checkState(0) != Qt::Checked) continue;
        const int idx = item->data(0, Qt::UserRole).toInt();
        out.push_back(m_plans[idx]);
    }
    return out;
}
