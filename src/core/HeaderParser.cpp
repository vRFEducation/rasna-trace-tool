// core/HeaderParser.cpp
#include "HeaderParser.h"
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

QVector<FunctionInfo> HeaderParser::parseFile(const QString& headerPath) {
    QVector<FunctionInfo> out;
    QFile f(headerPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    QStringList rawLines;
    while (!ts.atEnd()) rawLines << ts.readLine();

    m_inClass = false; m_className.clear();
    m_state = State::Normal; m_access = Access::Public;
    m_filePath = headerPath;

    QString current;
    int startLine = 0;
    bool inBlockComment = false;

    for (int i = 0; i < rawLines.size(); ++i) {
        QString line = rawLines[i];

        // ---- strip comments ----
        {
            if (inBlockComment) {
                int end = line.indexOf("*/");
                if (end >= 0) { line = line.mid(end + 2); inBlockComment = false; }
                else continue;
            }
            int b = line.indexOf("/*");
            if (b >= 0) {
                int e = line.indexOf("*/", b);
                if (e >= 0) line.remove(b, e - b + 2);
                else { line = line.left(b); inBlockComment = true; }
            }
            int lc = line.indexOf("//");
            if (lc >= 0) line = line.left(lc);
        }

        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;

        // ---- class/struct ----
        if (trimmed.startsWith("class ") && (trimmed.contains('{') || trimmed.contains(':'))) {
            if (!current.isEmpty()) { tryParseFunction(current, startLine, out); current.clear(); }
            m_inClass = true; m_className = extractClassName(trimmed);
            m_state = State::Normal; m_access = Access::Public; continue;
        }
        if (trimmed.startsWith("struct ") && trimmed.contains('{')) {
            if (!current.isEmpty()) { tryParseFunction(current, startLine, out); current.clear(); }
            m_inClass = true; m_className = extractStructName(trimmed);
            m_state = State::Normal; m_access = Access::Public; continue;
        }
        if (trimmed.startsWith("};") && m_inClass) {
            if (!current.isEmpty()) { tryParseFunction(current, startLine, out); current.clear(); }
            m_inClass = false; m_className.clear();
            m_state = State::Normal; m_access = Access::Public; continue;
        }

        // ---- section markers ----
        if (m_inClass) {
            if (trimmed == "signals:" || trimmed == "Q_SIGNALS:") { m_state = State::Signals; m_access = Access::Public; continue; }
            if (trimmed == "public slots:"    || trimmed == "public Q_SLOTS:")    { m_state = State::PublicSlots;    m_access = Access::Public;    continue; }
            if (trimmed == "protected slots:" || trimmed == "protected Q_SLOTS:") { m_state = State::ProtectedSlots; m_access = Access::Protected; continue; }
            if (trimmed == "private slots:"   || trimmed == "private Q_SLOTS:")   { m_state = State::PrivateSlots;   m_access = Access::Private;   continue; }
            if (trimmed == "public:")    { m_state = State::Normal; m_access = Access::Public;    continue; }
            if (trimmed == "protected:") { m_state = State::Normal; m_access = Access::Protected; continue; }
            if (trimmed == "private:")   { m_state = State::Normal; m_access = Access::Private;   continue; }
        }

        if (isVariableDeclaration(trimmed)) continue;

        // ---- accumulate / flush ----
        // CRITICAL: only start a new declaration when `current` is empty.
        // Otherwise continuation lines like
        //     const double min_Humidity, const double max_Humidity);
        // would themselves look like function starts and flush the
        // still-incomplete previous declaration.
        if (current.isEmpty() && isFunctionStart(trimmed)) {
            current = line;
            startLine = i + 1;
        } else if (!current.isEmpty()) {
            const bool currentHasOpenParen =
                (current.count('(') > current.count(')'));

            if (currentHasOpenParen || isContinuationLine(trimmed)) {
                current += "\n" + line;
            } else {
                tryParseFunction(current, startLine, out);
                current.clear();
            }
        }

        if (!current.isEmpty() && isCompleteDeclaration(current)) {
            tryParseFunction(current, startLine, out);
            current.clear();
        }
    }
    if (!current.isEmpty()) tryParseFunction(current, startLine, out);
    return out;
}

// -----------------------------------------------------------------------------

QString HeaderParser::extractClassName(const QString& line) const {
    QString t = line; t.remove('{'); t = t.trimmed();
    if (t.startsWith("class ")) t = t.mid(6).trimmed();
    if (t.endsWith(" final")) t.chop(6);
    int c = t.indexOf(':'); if (c > 0) t = t.left(c).trimmed();
    return t;
}
QString HeaderParser::extractStructName(const QString& line) const {
    QString t = line; t.remove('{'); t = t.trimmed();
    if (t.startsWith("struct ")) t = t.mid(7).trimmed();
    return t;
}

bool HeaderParser::isVariableDeclaration(const QString& line) const {
    QString t = line.trimmed();
    if ((t.startsWith("static ") || t.startsWith("const ")) && t.contains('=') && !t.contains('(')) return true;
    if (m_inClass && t.endsWith(';') && !t.contains('(') && !t.contains(')')) return true;
    if (t.startsWith("const QString") && t.contains("= QString(") && t.endsWith(';')) return true;
    return false;
}

bool HeaderParser::isFunctionStart(const QString& line) const {
    QString t = line.trimmed();

    static const QStringList leadMacros = {
        "Q_INVOKABLE", "Q_SLOT", "Q_SIGNAL", "Q_REQUIRED_RESULT",
        "explicit", "inline", "virtual", "static"
    };
    bool changed = true;
    while (changed) {
        changed = false;
        for (const QString& m : leadMacros) {
            if (t.startsWith(m + " ") || t.startsWith(m + "\t")) {
                t = t.mid(m.size()).trimmed(); changed = true;
            }
        }
    }

    static const QStringList skip = {
        "#", "if", "for", "while", "switch", "catch", "class", "struct",
        "enum", "return", "else", "const", "volatile",
        "Q_PROPERTY", "Q_OBJECT", "Q_ENUM", "Q_FLAG",
        "Q_DECLARE", "Q_DISABLE_COPY", "QML_ELEMENT", "PROPERTY_PRI",
        "Q_SIGNALS", "Q_SLOTS",
        "signals:", "public:", "private:", "protected:",
        "public slots:", "private slots:", "protected slots:",
        "public Q_SLOTS:", "private Q_SLOTS:", "protected Q_SLOTS:"
    };
    for (const auto& s : skip)
        if (t.startsWith(s)) return false;
    if (!t.contains('(')) return false;
    return true;
}

bool HeaderParser::isCompleteDeclaration(const QString& decl) const {
    QString t = decl; t.replace('\n', ' '); t = t.trimmed();
    static QRegularExpression strRe("\".*?\""); t.replace(strRe, "\"\"");
    if (t.endsWith(';')) return true;
    const int open  = t.count('{');
    const int close = t.count('}');
    if (open > 0) return open == close;
    return false;
}

bool HeaderParser::isContinuationLine(const QString& line) const {
    QString t = line.trimmed();
    if (t.isEmpty()) return false;

    static const QStringList notCont = {
        "Q_PROPERTY", "Q_OBJECT", "Q_ENUM", "Q_FLAG", "Q_DECLARE",
        "Q_DISABLE_COPY", "QML_ELEMENT", "PROPERTY_PRI",
        "Q_SIGNALS", "Q_SLOTS",
        "signals:", "public:", "private:", "protected:",
        "public slots:", "private slots:", "protected slots:",
        "public Q_SLOTS:", "private Q_SLOTS:", "protected Q_SLOTS:",
        "#"
    };
    for (const auto& s : notCont)
        if (t.startsWith(s)) return false;

    if (t.count('(') > t.count(')')) return true;

    if (t.endsWith(',') || t.endsWith('+') || t.endsWith('&') ||
        t.endsWith("const") || t.endsWith("override") ||
        t.endsWith("noexcept") || t.endsWith("final"))
        return true;

    if (!t.contains('(') && !t.contains('=') && !t.contains(';'))
        return true;

    return false;
}

void HeaderParser::tryParseFunction(const QString& decl, int startLine,
                                    QVector<FunctionInfo>& out) {
    if (isVariableDeclaration(decl)) return;
    parseFunction(decl, startLine, out);
}

void HeaderParser::parseFunction(const QString& decl, int startLine,
                                 QVector<FunctionInfo>& out) {
    QString clean = decl; clean.replace('\n', ' '); clean.replace('\r', ' ');
    clean = clean.simplified();
    clean = removeTrailingQualifiers(clean);
    clean = removeQtQualifiers(clean);

    int open = clean.indexOf('(');
    if (open < 0) return;
    QString before = clean.left(open).trimmed();

    FunctionInfo fn;
    fn.className  = m_className;
    fn.filePath   = m_filePath;
    fn.signatureLine = startLine;
    fn.access     = (m_access == Access::Public)    ? QtAccess::Public    :
                    (m_access == Access::Protected) ? QtAccess::Protected :
                    QtAccess::Private;
    fn.isSignal   = (m_state == State::Signals);
    fn.isSlot     = (m_state == State::PublicSlots ||
                 m_state == State::ProtectedSlots ||
                 m_state == State::PrivateSlots);
    fn.isInlineInHeader = true;

    // Constructor / destructor: no return type before the name.
    static QRegularExpression ctorRe(R"(^(?<name>~?[A-Za-z_]\w*)$)");
    auto cm = ctorRe.match(before);
    if (cm.hasMatch() && !m_className.isEmpty() &&
        (cm.captured("name") == m_className ||
         cm.captured("name") == "~" + m_className)) {
        fn.name = cm.captured("name");
        fn.returnType.clear();
    } else {
        static QRegularExpression re(R"(^(?<ret>.+?)\s+(?<name>[~A-Za-z_]\w*)$)");
        auto m = re.match(before);
        if (!m.hasMatch()) return;
        fn.returnType = m.captured("ret").trimmed();
        fn.name       = m.captured("name").trimmed();
    }

    int close = findMatchingParen(clean, open);
    if (close < 0) return;
    QString paramsText = clean.mid(open + 1, close - open - 1).trimmed();
    fn.params = parseParameters(paramsText);

    QString after = clean.mid(close + 1).trimmed();
    fn.isConst       = after.contains("const");
    fn.isOverride    = after.contains("override");
    fn.isPureVirtual = after.contains("= 0");
    fn.isDeleted     = after.contains("= delete");
    fn.isDefaulted   = after.contains("= default");

    if (fn.returnType.contains("virtual")) fn.isVirtual = true;
    if (fn.returnType.contains("static"))  fn.isStatic  = true;

    out.push_back(fn);
}

QVector<ParamInfo> HeaderParser::parseParameters(const QString& text) {
    QVector<ParamInfo> out;
    if (text.isEmpty() || text == "void") return out;
    for (const QString& p : splitParams(text)) {
        QString t = p.trimmed();
        if (t.isEmpty()) continue;
        ParamInfo pi;
        int eq = t.indexOf('=');
        if (eq > 0) { pi.hasDefault = true; pi.defaultValue = t.mid(eq + 1).trimmed(); t = t.left(eq).trimmed(); }
        int lastSpace = t.lastIndexOf(' ');
        if (lastSpace > 0) {
            pi.name = t.mid(lastSpace + 1).trimmed();
            pi.type = t.left(lastSpace).trimmed();
        } else {
            pi.type = t;
        }
        while (pi.name.startsWith('&') || pi.name.startsWith('*')) pi.name.remove(0, 1);
        out.push_back(pi);
    }
    return out;
}

QVector<QString> HeaderParser::splitParams(const QString& text) {
    QVector<QString> out;
    int depth = 0, start = 0;
    for (int i = 0; i < text.size(); ++i) {
        QChar c = text[i];
        if (c == '<' || c == '(' || c == '[') depth++;
        else if (c == '>' || c == ')' || c == ']') depth--;
        else if (c == ',' && depth == 0) { out.push_back(text.mid(start, i - start)); start = i + 1; }
    }
    if (start < text.size()) out.push_back(text.mid(start));
    return out;
}

int HeaderParser::findMatchingParen(const QString& text, int openPos) {
    int depth = 0;
    for (int i = openPos; i < text.size(); ++i) {
        if (text[i] == '(') depth++;
        else if (text[i] == ')') { depth--; if (depth == 0) return i; }
    }
    return -1;
}

QString HeaderParser::removeTrailingQualifiers(const QString& decl) {
    static QRegularExpression re(R"(\)\s*(const|override|final|noexcept|=\s*0|=\s*delete|=\s*default)?\s*[;{]\s*$)",
                                 QRegularExpression::CaseInsensitiveOption);
    QString out = decl; out.replace(re, ")"); return out;
}

QString HeaderParser::removeQtQualifiers(const QString& decl) {
    static const QStringList words = { "Q_INVOKABLE", "Q_SLOT", "Q_SIGNAL", "Q_REQUIRED_RESULT" };
    QString out = decl;
    for (const auto& w : words) {
        static QRegularExpression re(QStringLiteral("\\b%1\\b").arg(w));
        out.replace(re, "");
    }
    return out.simplified();
}
