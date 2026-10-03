// ui/MainWindow.cpp
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "core/ProjectLoader.h"
#include "core/HeaderParser.h"
#include "core/CppImplFinder.h"
#include "FunctionTableModel.h"

#include <QFileDialog>
#include <QStandardItemModel>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QHeaderView>
#include <QFileInfo>
#include <QTextEdit>
#include <QTextBlock>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    m_highlighter = new CppSyntaxHighlighter(ui->txtPreview->document(),
                                         CppSyntaxHighlighter::Mode::Cpp);
    m_projectModel = new QStandardItemModel(this);
    m_projectModel->setHorizontalHeaderLabels({ "File" });
    ui->treeProject->setModel(m_projectModel);
    ui->treeProject->header()->setStretchLastSection(true);

    m_fnModel = new FunctionTableModel(this);
    ui->tblMethods->setModel(m_fnModel);
    ui->tblMethods->horizontalHeader()->setStretchLastSection(true);
    ui->tblMethods->setSelectionBehavior(QAbstractItemView::SelectRows);

    connect(ui->btnOpenProject,   &QPushButton::clicked, this, &MainWindow::onOpenProject);
    connect(ui->btnDetectMethods, &QPushButton::clicked, this, &MainWindow::onDetectMethods);
    connect(ui->btnApplyTrace,    &QPushButton::clicked, this, &MainWindow::onApplyTrace);
    connect(ui->treeProject->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onFileSelected);
    connect(ui->tblMethods->selectionModel(), &QItemSelectionModel::currentRowChanged,
        this, [this](const QModelIndex& cur, const QModelIndex&) {
            if (!cur.isValid()) return;
            highlightMethodRange(m_fnModel->at(cur.row()));
        });


    setStatus(QStringLiteral("Ready. Open a project to begin."));
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::onOpenProject() {
    const QString root = QFileDialog::getExistingDirectory(this, "Select Project Root");
    if (root.isEmpty()) return;
    loadProject(root);
}

void MainWindow::loadProject(const QString& root) {
    m_projectRoot = root;
    m_projectModel->clear();
    m_projectModel->setHorizontalHeaderLabels({ "File" });

    const auto r = ProjectLoader::scan(root);

    auto addGroup = [&](const QString& title, const QStringList& files) {
        if (files.isEmpty()) return;
        auto* group = new QStandardItem(title);
        group->setEditable(false);
        for (const QString& f : files) {
            auto* item = new QStandardItem(QFileInfo(f).fileName());
            item->setData(f, Qt::UserRole);
            item->setToolTip(f);
            group->appendRow(item);
        }
        m_projectModel->appendRow(group);
    };
    addGroup("Headers (.h/.hpp)", r.headers);
    addGroup("Sources (.cpp)", r.sources);
    addGroup("QML", r.qml);

    ui->treeProject->expandAll();
    setStatus(QStringLiteral("Loaded %1 headers, %2 sources, %3 QML files.")
                  .arg(r.headers.size()).arg(r.sources.size()).arg(r.qml.size()));
}

void MainWindow::onFileSelected(const QModelIndex& idx) {
    if (!idx.isValid()) return;
    const QString path = idx.data(Qt::UserRole).toString();
    if (path.isEmpty()) return;
    m_currentFile = path;
    showFileInPreview(path);
    ui->btnDetectMethods->setEnabled(true);
    m_currentFunctions.clear();
    m_fnModel->setFunctions({});
    ui->btnApplyTrace->setEnabled(false);
}

void MainWindow::showFileInPreview(const QString& path) {
    delete m_highlighter;
const bool qml = path.endsWith(".qml", Qt::CaseInsensitive);
m_highlighter = new CppSyntaxHighlighter(
    ui->txtPreview->document(),
    qml ? CppSyntaxHighlighter::Mode::Qml : CppSyntaxHighlighter::Mode::Cpp);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    ui->txtPreview->setPlainText(ts.readAll());
    ui->txtPreview->setExtraSelections({});

}

void MainWindow::onDetectMethods() {
    if (m_currentFile.isEmpty()) return;
    const QString suffix = QFileInfo(m_currentFile).suffix().toLower();

    QVector<FunctionInfo> result;
    if (suffix == "h" || suffix == "hpp" || suffix == "hxx") {
        HeaderParser parser;
        result = parser.parseFile(m_currentFile);
    } else if (suffix == "cpp" || suffix == "cc" || suffix == "cxx") {
        // Find the paired header, parse it, then locate bodies in this cpp.
        QString hPath = m_currentFile;
        hPath.replace(QRegularExpression("\\.(cpp|cc|cxx)$"), ".h");
        if (QFile::exists(hPath)) {
            HeaderParser parser;
            const auto decls = parser.parseFile(hPath);
            CppImplFinder finder;
            result = finder.findImplementations(m_currentFile, decls);
        }
    }

    m_currentFunctions = result;
    populateMethodTable(result);
    ui->btnApplyTrace->setEnabled(!result.isEmpty());

    setStatus(QStringLiteral("Found %1 functions in %2")
                  .arg(result.size()).arg(QFileInfo(m_currentFile).fileName()));
}

void MainWindow::populateMethodTable(const QVector<FunctionInfo>& fns) {
    m_fnModel->setFunctions(fns);
    ui->tblMethods->resizeColumnsToContents();
}

void MainWindow::onApplyTrace() {
    // Placeholder — EditPlanner/Applier arrives in the next step.
    QMessageBox::information(this, "Next step",
        QString("Would insert TRACE_FN() into %1 method bodies.\n"
                "EditPlan + diff preview + apply will be implemented next.")
            .arg(m_currentFunctions.size()));
}

void MainWindow::setStatus(const QString& text) {
    statusBar()->showMessage(text);
}

void MainWindow::highlightMethodRange(const FunctionInfo& fn) {
    QList<QTextEdit::ExtraSelection> sels;

    if (fn.openBraceLine > 0 && fn.closeBraceLine >= fn.openBraceLine) {
        QTextCursor cur(ui->txtPreview->document());

        // Move to start of openBraceLine (1-based -> 0-based block)
        QTextBlock openBlock = ui->txtPreview->document()->findBlockByNumber(fn.openBraceLine - 1);
        QTextBlock closeBlock = ui->txtPreview->document()->findBlockByNumber(fn.closeBraceLine - 1);

        if (openBlock.isValid() && closeBlock.isValid()) {
            cur.setPosition(openBlock.position());
            cur.setPosition(closeBlock.position() + closeBlock.length() - 1,
                            QTextCursor::KeepAnchor);

            QTextEdit::ExtraSelection sel;
            sel.cursor = cur;
            sel.format.setBackground(QColor(58, 110, 165, 60));   // light blue, translucent
            sel.format.setProperty(QTextFormat::FullWidthSelection, false);
            sels.append(sel);

            // Bonus: keep a second selection on the signature line so it's easy to see the header
            QTextCursor sigCur(ui->txtPreview->document());
            QTextBlock sigBlock = ui->txtPreview->document()->findBlockByNumber(fn.signatureLine - 1);
            if (sigBlock.isValid()) {
                sigCur.setPosition(sigBlock.position());
                sigCur.setPosition(sigBlock.position() + sigBlock.length() - 1,
                                   QTextCursor::KeepAnchor);
                QTextEdit::ExtraSelection sigSel;
                sigSel.cursor = sigCur;
                sigSel.format.setBackground(QColor(58, 110, 165, 110));
                sels.append(sigSel);
            }

            // Scroll to the signature
            ui->txtPreview->setTextCursor(sigCur);
            ui->txtPreview->centerCursor();
        }
    }

    ui->txtPreview->setExtraSelections(sels);
}
