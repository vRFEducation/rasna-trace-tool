// src/core/QmlFunctionFinder.cpp
#include "QmlFunctionFinder.h"
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace {

// Strips comments and replaces string/template contents with spaces of the
// same length, so brace scanning is safe. Preserves line breaks.
QVector<QString> stripQml(const QVector<QString>& lines) {
    QVector<QString> out;
    out.reserve(lines.size());

    enum State { Normal, LineComment, BlockComment, Str, TemplateStr, TemplateExpr };
    State state = Normal;
    QVector<State> templateStack;

    for (const QString& raw : lines) {
        QString s = raw;
        for (int i = 0; i < s.size(); ++i) {
            QChar c = s[i];
            QChar n = (i + 1 < s.size()) ? s[i + 1] : QChar();

            switch (state) {
            case Normal:
                if (c == '/' && n == '/') { s[i] = ' '; s[i+1] = ' '; i++; state = LineComment; }
                else if (c == '/' && n == '*') { s[i] = ' '; s[i+1] = ' '; i++; state = BlockComment; }
                else if (c == '"')            { s[i] = ' '; state = Str; }
                else if (c == '\'')           { s[i] = ' '; state = Str; }
                else if (c == '`')            { s[i] = ' '; state = TemplateStr; }
                break;
            case LineComment:
                s[i] = ' ';
                break;
            case BlockComment:
                if (c == '*' && n == '/') { s[i] = ' '; s[i+1] = ' '; i++; state = Normal; }
                else s[i] = ' ';
                break;
            case Str:
                if (c == '\\') { s[i] = ' '; if (i+1 < s.size()) s[i+1] = ' '; i++; }
                else if (c == '"' || c == '\'') { s[i] = ' '; state = Normal; }
                else s[i] = ' ';
                break;
            case TemplateStr:
                if (c == '\\') { s[i] = ' '; if (i+1 < s.size()) s[i+1] = ' '; i++; }
                else if (c == '`')  { s[i] = ' '; state = Normal; }
                else if (c == '$' && n == '{') { s[i] = ' '; s[i+1] = ' '; i++; templateStack.append(TemplateStr); state = TemplateExpr; }
                else s[i] = ' ';
                break;
            case TemplateExpr:
                if (c == '{') { s[i] = ' '; templateStack.append(TemplateExpr); }
                else if (c == '}') {
                    s[i] = ' ';
                    if (!templateStack.isEmpty()) templateStack.removeLast();
                    state = templateStack.isEmpty() ? Normal : templateStack.last();
                }
                else if (c == '`') { s[i] = ' '; templateStack.append(TemplateExpr); state = TemplateStr; }
                else if (c == '"') { s[i] = ' '; state = Str; }
                break;
            }
        }
        if (state == LineComment) state = Normal;
        out.push_back(s);
    }
    return out;
}

// Matches braces to find the closing line. Returns -1 if unterminated.
int findCloseBrace(const QVector<QString>& stripped, int openBraceLine) {
    int depth = 0;
    bool started = false;
    for (int i = openBraceLine; i < stripped.size(); ++i) {
        for (QChar c : stripped[i]) {
            if (c == '{') { depth++; started = true; }
            else if (c == '}') {
                depth--;
                if (started && depth == 0) return i;
                if (depth < 0) return -1;
            }
        }
    }
    return -1;
}

// Starting from a line, find the first '{' — but ONLY if it appears before
// the end of the *expression body* on that line. We use it to detect whether
// a handler's body is a block (has `{`) or a plain expression.
//
// Returns the line index of the opening brace, or -1 if no brace is found
// before the handler value looks like an expression body.
int findBodyOpenBrace(const QVector<QString>& stripped, int startLine, int colonPos) {
    // Scan from `colonPos` on the start line, then continue onto subsequent
    // lines *only* if the first non-space char is `{`.
    const QString& l0 = stripped[startLine];
    // Look at the text after the colon on the start line.
    for (int i = colonPos; i < l0.size(); ++i) {
        if (l0[i] == '{') return startLine;
        // Semicolon terminates the expression body on this line.
        if (l0[i] == ';') return -1;
    }
    // No brace on the start line. Peek next line: a lone `{` (or a line
    // starting with `{`) still counts as a block body for that handler.
    for (int j = startLine + 1; j < stripped.size() && j <= startLine + 4; ++j) {
        QString t = stripped[j].trimmed();
        if (t.isEmpty()) continue;
        if (t.startsWith('{')) return j;
        // Anything else means expression body ended on the previous line.
        return -1;
    }
    return -1;
}

// Classifies a handler by name → OriginHint
OriginHint classifyHandler(const QString& handler) {
    if (handler.startsWith("Component.on")) return OriginHint::User;

    static const QRegularExpression mouseRe(R"(^on(Clicked|Pressed|Released|DoubleClicked|Wheel)$)");
    static const QRegularExpression keyRe  (R"(^on(KeyPressed|KeyReleased)$)");
    static const QRegularExpression timerRe(R"(^on(Triggered|Timeout)$)");
    static const QRegularExpression deviceRe(R"(^on(DataReady|ReadyRead|ValueChanged|ReadingChanged)$)");

    if (mouseRe.match(handler).hasMatch() || keyRe.match(handler).hasMatch())
        return OriginHint::User;
    if (timerRe.match(handler).hasMatch())
        return OriginHint::Timer;
    if (deviceRe.match(handler).hasMatch())
        return OriginHint::Device;
    return OriginHint::Signal;
}

// True if a `function ...` declaration has no block body on this line
// (e.g. `function f()` followed by `{` on next line, or expression-bodied
// is invalid for JS function declarations, so we only support block).
bool functionHasBlockBody(const QVector<QString>& stripped, int line, int parenEnd) {
    const QString& l = stripped[line];
    for (int i = parenEnd; i < l.size(); ++i) {
        if (l[i] == '{') return true;
        if (l[i] == ';') return false;
    }
    // Look ahead one or two lines for the `{`.
    for (int j = line + 1; j < stripped.size() && j <= line + 2; ++j) {
        QString t = stripped[j].trimmed();
        if (t.isEmpty()) continue;
        return t.startsWith('{');
    }
    return false;
}

} // namespace

QVector<FunctionInfo> QmlFunctionFinder::findFunctions(const QString& qmlPath) {
    QVector<FunctionInfo> out;
    QFile f(qmlPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;

    QTextStream ts(&f);
    QVector<QString> lines;
    while (!ts.atEnd()) lines.push_back(ts.readLine());

    const QVector<QString> stripped = stripQml(lines);

    // ---- Regex 1: function name(params) — body kind determined afterwards.
    static const QRegularExpression funcRe(
        R"(^\s*function\s+([A-Za-z_$][\w$]*)\s*\(([^)]*)\))");

    // ---- Regex 2: on<Signal>:   or   Component.on<X>:
    // No `{` required in the regex — the body may be a block or an expression.
    // We capture the property name and the rest-of-line after the colon.
    static const QRegularExpression handlerRe(
        R"(^\s*(Component\.on[A-Za-z]\w*|on[A-Z]\w*)\s*:)");

    for (int i = 0; i < stripped.size(); ++i) {
        const QString& line = stripped[i];

        // ================= JS function =================
        auto funcMatch = funcRe.match(line);
        if (funcMatch.hasMatch()) {
            int parenEnd = funcMatch.capturedEnd(2) + 1; // position after ')'

            // A function declaration must have a block body.
            if (!functionHasBlockBody(stripped, i, parenEnd)) {
                // Not a real function declaration (probably a call). Skip.
                continue;
            }

            FunctionInfo info;
            info.kind           = SourceKind::QmlFunction;
            info.filePath       = qmlPath;
            info.qmlHandlerName = funcMatch.captured(1);
            info.name           = funcMatch.captured(1);
            info.signatureLine  = i + 1;
            info.originHint     = OriginHint::Internal;
            info.bodyKind       = BodyKind::Block;

            // Parse parameters — supports `a`, `a = 1`, `...rest`
            const QString params = funcMatch.captured(2).trimmed();
            if (!params.isEmpty()) {
                for (const QString& p : params.split(',')) {
                    QString name = p.trimmed();
                    if (name.startsWith("...")) name = name.mid(3).trimmed();
                    int eq = name.indexOf('=');
                    if (eq >= 0) name = name.left(eq).trimmed();
                    if (!name.isEmpty()) {
                        ParamInfo pi;
                        pi.name = name;
                        info.params.append(pi);
                    }
                }
            }

            int open = i;
            // The `{` may be on the same line or one/two lines down.
            // Find it via findCloseBrace starting from i; it is tolerant.
            while (open < stripped.size() && !stripped[open].contains('{')) ++open;
            if (open >= stripped.size()) continue;

            int close = findCloseBrace(stripped, open);
            if (close < 0) continue;

            info.openBraceLine  = open + 1;
            info.closeBraceLine = close + 1;
            out.push_back(info);
            i = close;
            continue;
        }

        // ================= Signal handler / Component lifecycle =================
        auto handlerMatch = handlerRe.match(line);
        if (handlerMatch.hasMatch()) {
            const QString name = handlerMatch.captured(1);
            const int colonPos = handlerMatch.capturedEnd(1); // position right after name, before ':'

            FunctionInfo info;
            info.kind           = SourceKind::QmlHandler;
            info.filePath       = qmlPath;
            info.qmlHandlerName = name;
            info.name           = name;
            info.signatureLine  = i + 1;
            info.originHint     = classifyHandler(name);

            const int openBraceLine = findBodyOpenBrace(stripped, i, colonPos);
            if (openBraceLine < 0) {
                // ---- Expression body: onX: <expr> on one (or more) lines ----
                // End of body: the first `;` at depth 0, or end of line if no `;`.
                info.bodyKind = BodyKind::Expression;
                info.openBraceLine  = 0;
                info.closeBraceLine = 0;

                // Find expression-end on the same line (semicolon) or EOL.
                int bodyEndLine = i;
                bool foundSemi  = false;
                const QString& l = stripped[i];
                for (int k = colonPos; k < l.size(); ++k) {
                    if (l[k] == ';') { foundSemi = true; break; }
                }
                // If the line ends with ',' or '+' or similar continuation,
                // keep going for a few lines. Conservative but useful.
                if (!foundSemi) {
                    const QString trimmed = l.trimmed();
                    bool continues = trimmed.endsWith(QLatin1Char(','))  ||
                                     trimmed.endsWith(QLatin1Char('+'))  ||
                                     trimmed.endsWith(QLatin1Char('-'))  ||
                                     trimmed.endsWith(QLatin1Char('*'))  ||
                                     trimmed.endsWith(QLatin1Char('/'))  ||
                                     trimmed.endsWith(QLatin1Char('&'))  ||
                                     trimmed.endsWith(QLatin1Char('|'))  ||
                                     trimmed.endsWith(QLatin1Char('?'))  ||
                                     trimmed.endsWith(QLatin1Char(':'))  ||
                                     trimmed.endsWith(QLatin1String("&&")) ||
                                     trimmed.endsWith(QLatin1String("||")) ||
                                     trimmed.endsWith(QLatin1String("=>")) ||
                                     trimmed.endsWith(QLatin1Char('\\'));

                    // Also treat "still inside an open paren/bracket" as a continuation.
                    if (!continues) {
                        int depth = 0;
                        for (QChar c : trimmed) {
                            if (c == '(' || c == '[') ++depth;
                            else if (c == ')' || c == ']') --depth;
                        }
                        continues = (depth > 0);
                    }

                    if (continues) {
                        for (int j = i + 1; j < stripped.size() && j <= i + 8; ++j) {
                            const QString& lj = stripped[j];
                            for (int k = 0; k < lj.size(); ++k) {
                                if (lj[k] == QLatin1Char(';')) { bodyEndLine = j; foundSemi = true; break; }
                            }
                            if (foundSemi) break;
                            bodyEndLine = j;
                            if (!lj.trimmed().isEmpty()) {
                                // stop if next line clearly starts a new binding/handler
                                QString t = lj.trimmed();
                                static const QRegularExpression newDecl(
                                    R"(^(Component\.on[A-Za-z]\w*|on[A-Z]\w*|function\s+[A-Za-z_$]|[A-Za-z_$][\w$]*\s*:))");
                                if (newDecl.match(t).hasMatch()) break;
                            }
                        }
                    }
                }
                info.bodyStartLine = i + 1;
                info.bodyEndLine   = bodyEndLine + 1;
                out.push_back(info);
                // Do NOT skip past expression bodies — multiple handlers can
                // share a line, e.g. `onX: a(); onY: b()` (rare but legal).
                continue;
            }

            // ---- Block body ----
            info.bodyKind = BodyKind::Block;
            int close = findCloseBrace(stripped, openBraceLine);
            if (close < 0) continue;
            info.openBraceLine  = openBraceLine + 1;
            info.closeBraceLine = close + 1;
            info.bodyStartLine  = info.openBraceLine;
            info.bodyEndLine    = info.closeBraceLine;
            out.push_back(info);
            i = close;
            continue;
        }
    }

    return out;
}
