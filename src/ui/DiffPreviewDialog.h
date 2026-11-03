// ui/DiffPreviewDialog.h
#pragma once
#include <QDialog>
#include "core/TextEdit.h"

class QPlainTextEdit;
class QTreeWidget;
class QPushButton;

class DiffPreviewDialog : public QDialog {
    Q_OBJECT
public:
    explicit DiffPreviewDialog(QWidget* parent = nullptr);

    // Feed it the plan: file -> (originalLines, edits).
    struct FilePlan {
        QString filePath;
        QStringList originalLines;
        QVector<TextEdit> edits;
        bool    enabled = true;
    };

    void setPlans(QVector<FilePlan> plans);

    QVector<FilePlan> selectedPlans() const;  // user may deselect files

private slots:
    void onFileSelected();
    void onToggleAll(bool checked);

private:
    QVector<FilePlan> m_plans;
    QVector<bool>     m_fileEnabled;

    QTreeWidget*   m_tree = nullptr;
    QPlainTextEdit* m_preview = nullptr;
    QPushButton*   m_btnApply = nullptr;
    QPushButton*   m_btnCancel = nullptr;
    QPushButton*   m_btnToggleAll = nullptr;
};