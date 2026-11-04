// ui/MainWindow.cpp
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "core/ProjectLoader.h"
#include "core/HeaderParser.h"
#include "core/CppImplFinder.h"
#include "FunctionTableModel.h"

#include <QFileDialog>
#include <QStandardItemModel>
#include <QRegularExpression>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QHeaderView>
#include <QFileInfo>
#include <QTextEdit>
#include <QTextBlock>

#include <core/EditApplier.h>
#include <core/EditPlanner.h>
#include <core/QmlFunctionFinder.h>

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
    if (suffix == "qml") {
        QmlFunctionFinder finder;
        result = finder.findFunctions(m_currentFile);
    } else if (suffix == "h" || suffix == "hpp" || suffix == "hxx") {
        HeaderParser parser;
        result = parser.parseFile(m_currentFile);
    } else if (suffix == "cpp" || suffix == "cc" || suffix == "cxx") {
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
    // ---- TEMP: export detected functions for inspection ----
    {
        QFile dump(QFileInfo(m_currentFile).absolutePath() + "/_detected.txt");
        if (dump.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream os(&dump);
            os << "File: " << m_currentFile << "\n";
            os << "Total: " << result.size() << " functions\n\n";
            os << "idx | kind | bodyKind | sig | open | close | bodyStart | bodyEnd | name\n";
            os << "----+------+----------+-----+------+-------+-----------+---------+-----\n";
            for (int i = 0; i < result.size(); ++i) {
                const FunctionInfo& fn = result[i];
                os << QString("%1 | %2 | %3 | %4 | %5 | %6 | %7 | %8 | %9\n")
                          .arg(i, 3)
                          .arg(int(fn.kind), 4)
                          .arg(int(fn.bodyKind), 8)
                          .arg(fn.signatureLine, 4)
                          .arg(fn.openBraceLine, 4)
                          .arg(fn.closeBraceLine, 5)
                          .arg(fn.bodyStartLine, 9)
                          .arg(fn.bodyEndLine, 7)
                          .arg(fn.qmlHandlerName);
            }
            dump.close();
        }
    }
    populateMethodTable(result);
    ui->btnApplyTrace->setEnabled(!result.isEmpty());

    setStatus(QStringLiteral("Found %1 functions in %2")
                  .arg(result.size()).arg(QFileInfo(m_currentFile).fileName()));
}

void MainWindow::populateMethodTable(const QVector<FunctionInfo>& fns) {
    m_fnModel->setFunctions(fns);
    ui->tblMethods->resizeColumnsToContents();
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


void MainWindow::onApplyTrace() {
    if (m_currentFile.isEmpty()) return;

    const QString suffix = QFileInfo(m_currentFile).suffix().toLower();

    // Phase-1: we plan for the current file (headers or cpp) and, if it's
    // a cpp, also the paired header for in-header bodies.
    QStringList filesToPlan;
    filesToPlan << m_currentFile;
    if (suffix == "cpp" || suffix == "cc" || suffix == "cxx") {
        QString h = m_currentFile;
        h.replace(QRegularExpression("\\.(cpp|cc|cxx)$"), ".h");
        if (QFile::exists(h)) filesToPlan << h;
    }

    QVector<DiffPreviewDialog::FilePlan> plans;
    EditPlanner::Options opts;   // defaults are fine

    for (const QString& file : filesToPlan) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream ts(&f);
        QStringList lines;
        while (!ts.atEnd()) lines << ts.readLine();
        f.close();

        // parse
        QVector<FunctionInfo> fns;
        const QString sfx = QFileInfo(file).suffix().toLower();
        if (sfx == "qml") {
                QmlFunctionFinder finder;
                fns = finder.findFunctions(file);
        } else if (sfx == "h" || sfx == "hpp" || sfx == "hxx") {
            HeaderParser parser;
            fns = parser.parseFile(file);
        } else {
            QString h = file;
            h.replace(QRegularExpression("\\.(cpp|cc|cxx)$"), ".h");
            if (QFile::exists(h)) {
                HeaderParser parser;
                const auto decls = parser.parseFile(h);
                CppImplFinder finder;
                fns = finder.findImplementations(file, decls);
            }
        }

        const auto edits = EditPlanner::plan(file, lines, fns, opts);
        if (edits.isEmpty()) continue;

        DiffPreviewDialog::FilePlan p;
        p.filePath      = file;
        p.originalLines = lines;
        p.edits         = edits;
        plans.push_back(p);
    }

    if (plans.isEmpty()) {
        QMessageBox::information(this, "Nothing to do",
            "No functions require instrumentation in this file.");
        return;
    }

    DiffPreviewDialog dlg(this);
    dlg.setPlans(plans);
    if (dlg.exec() != QDialog::Accepted) return;

    const auto selected = dlg.selectedPlans();
    int okCount = 0, failCount = 0;
    QStringList errors;
    for (const auto& p : selected) {
        QString err;
        if (EditApplier::applyToFile(p.filePath, p.edits, &err)) {
            ++okCount;
        } else {
            ++failCount;
            errors << QString("%1: %2").arg(QFileInfo(p.filePath).fileName(), err);
        }
    }

    if (failCount == 0) {
        QMessageBox::information(this, "Done",
            QString("Instrumented %1 file(s).").arg(okCount));
    } else {
        QMessageBox::warning(this, "Partially applied",
            QString("Succeeded: %1\nFailed: %2\n\n%3")
                .arg(okCount).arg(failCount).arg(errors.join('\n')));
    }

    // Refresh preview of the currently open file
    showFileInPreview(m_currentFile);
    onDetectMethods();
}


