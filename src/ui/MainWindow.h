// ui/MainWindow.h
#pragma once
#include <QMainWindow>
#include <QFileSystemModel>
#include <QTextEdit>
#include "CppSyntaxHighlighter.h"
#include "core/FunctionInfo.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QStandardItemModel;
class FunctionTableModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onOpenProject();
    void onDetectMethods();
    void onApplyTrace();
    void onFileSelected(const QModelIndex& idx);

private:
    void loadProject(const QString& root);
    void showFileInPreview(const QString& path);
    void populateMethodTable(const QVector<FunctionInfo>& fns);
    void setStatus(const QString& text);

    Ui::MainWindow* ui;
    QStandardItemModel* m_projectModel = nullptr;
    FunctionTableModel* m_fnModel = nullptr;
    QString m_projectRoot;
    QString m_currentFile;
    QVector<FunctionInfo> m_currentFunctions;

    CppSyntaxHighlighter* m_highlighter = nullptr;
    QVector<QTextEdit::ExtraSelection> m_selections; // not strictly needed
    void highlightMethodRange(const FunctionInfo& fn);

};
