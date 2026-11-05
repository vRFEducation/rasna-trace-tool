// ui/MainWindow.cpp
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "core/ProjectLoader.h"
#include "core/HeaderParser.h"
#include "core/CppImplFinder.h"
#include "FunctionTableModel.h"

#include <QFileDialog>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QRegularExpression>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QHeaderView>
#include <QFileInfo>
#include <QTextEdit>
#include <QTextBlock>
#include <QShortcut>            // NEW

#include <core/EditApplier.h>
#include <core/EditPlanner.h>
#include <core/QmlFunctionFinder.h>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    m_highlighter = new CppSyntaxHighlighter(ui->txtPreview->document(),
                                             CppSyntaxHighlighter::Mode::Cpp);

    // ------------------------------------------------------------------
    // Project tree: keep QStandardItemModel as source, wrap in a proxy
    // so the filter box can hide non-matching rows.
    // ------------------------------------------------------------------
    m_projectModel = new QStandardItemModel(this);
    m_projectModel->setHorizontalHeaderLabels({ "File" });

    m_projectProxy = new QSortFilterProxyModel(this);
    m_projectProxy->setSourceModel(m_projectModel);
    m_projectProxy->setRecursiveFilteringEnabled(true);
    m_projectProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_projectProxy->setFilterKeyColumn(0);

    ui->treeProject->setModel(m_projectProxy);
    ui->treeProject->header()->setStretchLastSection(true);

    // ------------------------------------------------------------------
    // Methods table: wrap FunctionTableModel in a proxy.
    // ------------------------------------------------------------------
    m_fnModel = new FunctionTableModel(this);

    m_fnProxy = new QSortFilterProxyModel(this);
    m_fnProxy->setSourceModel(m_fnModel);
    m_fnProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
m_fnProxy->setFilterKeyColumn(1);    // If your model doesn't expose a column enum, use the numeric index of
    // the "name" column instead (check FunctionTableModel::headerData).

    ui->tblMethods->setModel(m_fnProxy);
    ui->tblMethods->horizontalHeader()->setStretchLastSection(true);
    ui->tblMethods->setSelectionBehavior(QAbstractItemView::SelectRows);

    // ------------------------------------------------------------------
    // Connections
    // ------------------------------------------------------------------
    connect(ui->btnOpenProject,   &QPushButton::clicked, this, &MainWindow::onOpenProject);
    connect(ui->btnDetectMethods, &QPushButton::clicked, this, &MainWindow::onDetectMethods);
    connect(ui->btnApplyTrace,    &QPushButton::clicked, this, &MainWindow::onApplyTrace);

    connect(ui->treeProject->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onFileSelected);

    // Row selection in the (proxy) table -> map back to source model.
    connect(ui->tblMethods->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& cur, const QModelIndex&) {
                if (!cur.isValid()) return;
                const QModelIndex src = m_fnProxy->mapToSource(cur);
                if (!src.isValid()) return;
                highlightMethodRange(m_fnModel->at(src.row()));
            });

    // ---------------- Search box wiring (NEW) ----------------
    connect(ui->txtFilterProject, &QLineEdit::textChanged,
            this, [this](const QString& t){
                m_projectProxy->setFilterFixedString(t);
                ui->treeProject->expandAll();   // keep groups visible while filtering
            });

    connect(ui->txtFilterMethods, &QLineEdit::textChanged,
            this, [this](const QString& t){
                m_fnProxy->setFilterFixedString(t);
            });

    connect(ui->txtFilterPreview, &QLineEdit::textChanged,
            this, [this](const QString& t){
                highlightAllMatches(t, ui->chkCaseSensitive->isChecked());
            });

    connect(ui->chkCaseSensitive, &QCheckBox::toggled,
            this, [this](bool){
                highlightAllMatches(ui->txtFilterPreview->text(),
                                    ui->chkCaseSensitive->isChecked());
            });

    connect(ui->btnFindNext, &QToolButton::clicked,
            this, [this]{ findNext(false); });
    connect(ui->btnFindPrev, &QToolButton::clicked,
            this, [this]{ findNext(true); });
    connect(ui->txtFilterPreview, &QLineEdit::returnPressed,
            this, [this]{ findNext(false); });

    auto* nextSc = new QShortcut(QKeySequence::FindNext,     this);
    auto* prevSc = new QShortcut(QKeySequence::FindPrevious, this);
    connect(nextSc, &QShortcut::activated, this, [this]{ findNext(false); });
    connect(prevSc, &QShortcut::activated, this, [this]{ findNext(true); });
    // ---------------------------------------------------------

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

    // Clear any active filter so the freshly loaded project is fully visible.
    ui->txtFilterProject->clear();

    setStatus(QStringLiteral("Loaded %1 headers, %2 sources, %3 QML files.")
                  .arg(r.headers.size()).arg(r.sources.size()).arg(r.qml.size()));
}

void MainWindow::onFileSelected(const QModelIndex& proxyIdx) {
    if (!proxyIdx.isValid()) return;
    // Map from proxy to source before reading UserRole data.
    const QModelIndex idx = m_projectProxy->mapToSource(proxyIdx);
    const QString path = idx.data(Qt::UserRole).toString();
    if (path.isEmpty()) return;   // group header row
    m_currentFile = path;
    showFileInPreview(path);
    ui->btnDetectMethods->setEnabled(true);
    m_currentFunctions.clear();
    m_fnModel->setFunctions({});
    ui->btnApplyTrace->setEnabled(false);

    // Clear stale matches from the previous file
    m_matches.clear();
    m_currentMatch = -1;
    ui->txtFilterPreview->clear();
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

    // Reset search state for the new file
    m_matches.clear();
    m_currentMatch = -1;
    if (!ui->txtFilterPreview->text().isEmpty())
        highlightAllMatches(ui->txtFilterPreview->text(),
                            ui->chkCaseSensitive->isChecked());
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

        QTextBlock openBlock = ui->txtPreview->document()->findBlockByNumber(fn.openBraceLine - 1);
        QTextBlock closeBlock = ui->txtPreview->document()->findBlockByNumber(fn.closeBraceLine - 1);

        if (openBlock.isValid() && closeBlock.isValid()) {
            cur.setPosition(openBlock.position());
            cur.setPosition(closeBlock.position() + closeBlock.length() - 1,
                            QTextCursor::KeepAnchor);

            QTextEdit::ExtraSelection sel;
            sel.cursor = cur;
            sel.format.setBackground(QColor(58, 110, 165, 60));
            sel.format.setProperty(QTextFormat::FullWidthSelection, false);
            sels.append(sel);

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

            ui->txtPreview->setTextCursor(sigCur);
            ui->txtPreview->centerCursor();
        }
    }

    // Preserve any preview search highlights that were active, by merging
    // them in *before* the method range, so method selection stays on top.
    // The search highlight list is rebuilt below from m_matches.
    if (!m_matches.isEmpty()) {
        for (int i = 0; i < m_matches.size(); ++i) {
            QTextEdit::ExtraSelection sel;
            sel.cursor  = m_matches[i];
            sel.format.setBackground(i == m_currentMatch ? m_currentColor
                                                         : m_highlightColor);
            sels.prepend(sel);   // keep method highlight above search hits
        }
    }

    ui->txtPreview->setExtraSelections(sels);
}

// ====================================================================
// NEW: find-in-preview implementation
// ====================================================================
void MainWindow::highlightAllMatches(const QString& text, bool caseSensitive)
{
    m_matches.clear();
    m_currentMatch = -1;

    auto* doc = ui->txtPreview->document();
    QTextDocument::FindFlags flags;
    if (caseSensitive) flags |= QTextDocument::FindCaseSensitively;

    if (!text.isEmpty()) {
        QTextCursor cursor(doc);
        while (true) {
            cursor = doc->find(text, cursor, flags);
            if (cursor.isNull()) break;
            m_matches.append(cursor);
        }
    }

    // Apply the highlight layer (no scrolling yet)
    QList<QTextEdit::ExtraSelection> selections;
    for (const auto& c : m_matches) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = c;
        sel.format.setBackground(m_highlightColor);
        selections.append(sel);
    }
    ui->txtPreview->setExtraSelections(selections);

    if (!m_matches.isEmpty())
        findNext(false);
}

void MainWindow::findNext(bool backward)
{
    if (m_matches.isEmpty()) return;

    if (m_currentMatch < 0) {
        m_currentMatch = backward ? m_matches.size() - 1 : 0;
    } else {
        m_currentMatch += backward ? -1 : 1;
        if (m_currentMatch >= m_matches.size()) m_currentMatch = 0;
        if (m_currentMatch < 0) m_currentMatch = m_matches.size() - 1;
    }

    QList<QTextEdit::ExtraSelection> selections;
    for (int i = 0; i < m_matches.size(); ++i) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = m_matches[i];
        sel.format.setBackground(i == m_currentMatch ? m_currentColor
                                                     : m_highlightColor);
        selections.append(sel);
    }
    ui->txtPreview->setExtraSelections(selections);

    QTextCursor cur = m_matches[m_currentMatch];
    ui->txtPreview->setTextCursor(cur);
    ui->txtPreview->ensureCursorVisible();
}

void MainWindow::onApplyTrace() {
    if (m_currentFile.isEmpty()) return;

    const QString suffix = QFileInfo(m_currentFile).suffix().toLower();

    QStringList filesToPlan;
    filesToPlan << m_currentFile;
    if (suffix == "cpp" || suffix == "cc" || suffix == "cxx") {
        QString h = m_currentFile;
        h.replace(QRegularExpression("\\.(cpp|cc|cxx)$"), ".h");
        if (QFile::exists(h)) filesToPlan << h;
    }

    QVector<DiffPreviewDialog::FilePlan> plans;
    EditPlanner::Options opts;

    for (const QString& file : filesToPlan) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream ts(&f);
        QStringList lines;
        while (!ts.atEnd()) lines << ts.readLine();
        f.close();

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

    showFileInPreview(m_currentFile);
    onDetectMethods();
}
