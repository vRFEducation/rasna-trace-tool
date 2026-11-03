// ui/CppSyntaxHighlighter.cpp
#include "CppSyntaxHighlighter.h"
#include <QTextDocument>

CppSyntaxHighlighter::CppSyntaxHighlighter(QTextDocument* doc, Mode mode)
    : QSyntaxHighlighter(doc) {
    if (mode == Mode::Cpp) setupCpp();
    else                   setupQml();
}

static QTextCharFormat fmt(const QColor& c, bool bold = false, bool italic = false) {
    QTextCharFormat f;
    f.setForeground(c);
    if (bold)   f.setFontWeight(QFont::Bold);
    if (italic) f.setFontItalic(true);
    return f;
}

void CppSyntaxHighlighter::setupCpp() {
    // Palette tuned for a dark editor background
    const QColor kw      = QColor("#c586c0");
    const QColor type    = QColor("#4ec9b0");
    const QColor number  = QColor("#b5cea8");
    const QColor string  = QColor("#ce9178");
    const QColor comment = QColor("#6a9955");
    const QColor preproc = QColor("#9b9b9b");
    const QColor fnName  = QColor("#dcdcaa");

    // Keywords
    const QStringList keywords = {
        "\\bclass\\b","\\bstruct\\b","\\benum\\b","\\bnamespace\\b","\\btemplate\\b",
        "\\btypename\\b","\\busing\\b","\\bpublic\\b","\\bprotected\\b","\\bprivate\\b",
        "\\bvirtual\\b","\\boverride\\b","\\bfinal\\b","\\bstatic\\b","\\bconst\\b",
        "\\bconstexpr\\b","\\bnoexcept\\b","\\binline\\b","\\breturn\\b","\\bif\\b",
        "\\belse\\b","\\bfor\\b","\\bwhile\\b","\\bswitch\\b","\\bcase\\b","\\bbreak\\b",
        "\\bcontinue\\b","\\bnew\\b","\\bdelete\\b","\\bthis\\b","\\bnullptr\\b",
        "\\btrue\\b","\\bfalse\\b","\\bsignals\\b","\\bslots\\b","\\bemit\\b"
    };
    for (const auto& k : keywords)
        m_rules.push_back({ QRegularExpression(k), fmt(kw, true) });

    // Common Qt / std types
    const QStringList types = {
        "\\bQString\\b","\\bQWidget\\b","\\bQObject\\b","\\bQVector\\b","\\bQList\\b",
        "\\bQMap\\b","\\bQHash\\b","\\bQSet\\b","\\bQPair\\b","\\bQVariant\\b",
        "\\bQByteArray\\b","\\bQFile\\b","\\bQDir\\b","\\bQTimer\\b","\\bQThread\\b",
        "\\bint\\b","\\bvoid\\b","\\bbool\\b","\\bchar\\b","\\bfloat\\b","\\bdouble\\b",
        "\\blong\\b","\\bshort\\b","\\bunsigned\\b","\\bsigned\\b","\\bauto\\b",
        "\\bsize_t\\b","\\bstd::\\w+\\b","\\bQ_\\w+\\b"
    };
    for (const auto& t : types)
        m_rules.push_back({ QRegularExpression(t), fmt(type) });

    // Preprocessor
    m_rules.push_back({ QRegularExpression("^\\s*#\\s*\\w+"), fmt(preproc, true) });

    // Numbers
    m_rules.push_back({ QRegularExpression("\\b[0-9]+(\\.[0-9]+)?([eE][+-]?[0-9]+)?[fFuUlL]?\\b"),
                        fmt(number) });

    // Function-name heuristic: word right before '('
    m_rules.push_back({ QRegularExpression("\\b([A-Za-z_]\\w*)\\s*(?=\\()"), fmt(fnName) });

    // Single-line string
    m_rules.push_back({ QRegularExpression("\"([^\"\\\\]|\\\\.)*\""), fmt(string) });
    m_rules.push_back({ QRegularExpression("'([^'\\\\]|\\\\.)*'"),     fmt(string) });

    // Multi-line /* */ — handled statefully in highlightBlock
    m_multilineCommentFmt = fmt(comment, false, true);
    m_blockStart = QRegularExpression("/\\*");
    m_blockEnd   = QRegularExpression("\\*/");

    // Single-line // comment (last so it wins)
    m_rules.push_back({ QRegularExpression("//[^\\n]*"), fmt(comment, false, true) });
}

void CppSyntaxHighlighter::setupQml() {
    // Small QML grammar for preview purposes only
    const QColor kw   = QColor("#c586c0");
    const QColor type = QColor("#4ec9b0");
    const QColor str  = QColor("#ce9178");
    const QColor num  = QColor("#b5cea8");
    const QColor id   = QColor("#9cdcfe");

    const QStringList keywords = {
        "\\bimport\\b","\\bproperty\\b","\\bsignal\\b","\\bfunction\\b","\\bon\\w+\\b",
        "\\bif\\b","\\belse\\b","\\breturn\\b","\\bvar\\b","\\btrue\\b","\\bfalse\\b",
        "\\bnull\\b","\\bparent\\b","\\bid\\b"
    };
    for (const auto& k : keywords) m_rules.push_back({ QRegularExpression(k), fmt(kw, true) });

    const QStringList types = {
        "\\bItem\\b","\\bRectangle\\b","\\bText\\b","\\bMouseArea\\b","\\bColumn\\b",
        "\\bRow\\b","\\bLoader\\b","\\bComponent\\b","\\bConnections\\b","\\bTimer\\b",
        "\\bint\\b","\\breal\\b","\\bstring\\b","\\bbool\\b","\\bvar\\b"
    };
    for (const auto& t : types) m_rules.push_back({ QRegularExpression(t), fmt(type) });

    m_rules.push_back({ QRegularExpression("\\b[0-9]+(\\.[0-9]+)?\\b"), fmt(num) });
    m_rules.push_back({ QRegularExpression("\"([^\"\\\\]|\\\\.)*\""), fmt(str) });
    m_rules.push_back({ QRegularExpression("'([^'\\\\]|\\\\.)*'"),   fmt(str) });
    m_rules.push_back({ QRegularExpression("//[^\\n]*"),             fmt(QColor("#6a9955"), false, true) });
}

void CppSyntaxHighlighter::highlightBlock(const QString& text) {
    // ---- Multi-line comments ----
    setCurrentBlockState(0);

    int start = 0;
    if (previousBlockState() != 1) {
        auto firstStart = m_blockStart.match(text);
        if (!firstStart.hasMatch()) start = -1;
        else start = firstStart.capturedStart();
    }

    int guard = 0;
    while (start >= 0 && start < text.size()) {
        if (++guard > 10000) {
            // Something is pathological. Give up cleanly rather than hang.
            setCurrentBlockState(0);
            break;
        }

        auto endMatch = m_blockEnd.match(text, start);
        if (endMatch.hasMatch()) {
            int end = endMatch.capturedEnd();
            if (end <= start) {
                // Zero-length match — advance one char to avoid infinite loop.
                start++;
                continue;
            }
            setFormat(start, end - start, m_multilineCommentFmt);
            start = text.indexOf(m_blockStart, end);
        } else {
            // No end marker on this block — rest of block is comment,
            // and the state carries over to the next block.
            setFormat(start, text.length() - start, m_multilineCommentFmt);
            setCurrentBlockState(1);
            break;
        }
    }

    // ---- Single-line rules ----
    for (const Rule& r : m_rules) {
        auto it = r.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            if (m.capturedLength() == 0) {
                // Defensive: empty matches would loop globalMatch forever.
                continue;
            }
            setFormat(m.capturedStart(), m.capturedLength(), r.format);
        }
    }
}
