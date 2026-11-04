// src/core/QmlFunctionFinder.cpp
#include "QmlFunctionFinder.h"
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace {

// One-pass scanner over the full file text.
// Returns a vector<QString> where every comment/string/template-literal
// character has been replaced by a space (line structure is preserved).
// The scanner NEVER has persistent "state" across lines — it walks the
// entire file as a single string, so it cannot get stuck.
QVector<QString> stripQml(const QVector<QString>& lines) {
    // Reassemble into one string, keeping track of line boundaries.
    // (We rebuild it again at the end so nothing is lost.)
    QString all;
    all.reserve(1 << 20);
    QVector<int> lineStarts;
    lineStarts.reserve(lines.size());
    for (int i = 0; i < lines.size(); ++i) {
        lineStarts.push_back(all.size());
        all += lines[i];
        all += QLatin1Char('\n');
    }

    const int n = all.size();
    enum Mode { Normal, LineComment, BlockComment, Str, TemplateStr, TemplateExpr };
    QVector<Mode> modeStack;
    Mode mode = Normal;

    for (int i = 0; i < n; ++i) {
        const QChar c = all[i];
        const QChar next = (i + 1 < n) ? all[i + 1] : QChar();

        switch (mode) {
        case Normal:
            if (c == '/' && next == '/') { all[i] = ' '; all[i+1] = ' '; ++i; mode = LineComment; }
            else if (c == '/' && next == '*') { all[i] = ' '; all[i+1] = ' '; ++i; mode = BlockComment; }
            else if (c == '"' || c == '\'') { all[i] = ' '; modeStack.append(Normal); mode = Str; }
            else if (c == '`') { all[i] = ' '; modeStack.append(Normal); mode = TemplateStr; }
            break;

        case LineComment:
            if (c == '\n') { mode = Normal; }   // the newline is kept
            else all[i] = ' ';
            break;

        case BlockComment:
            if (c == '*' && next == '/') { all[i] = ' '; all[i+1] = ' '; ++i; mode = Normal; }
            else if (c != '\n') all[i] = ' ';
            break;

        case Str:
            if (c == '\\') { all[i] = ' '; if (i+1 < n) { all[i+1] = ' '; ++i; } }
            else if (c == '"' || c == '\'') {
                all[i] = ' ';
                mode = modeStack.isEmpty() ? Normal : modeStack.takeLast();
            }
            else if (c == '\n') {
                // Unterminated single-line string: bail to Normal so we can't
                // stay stuck in Str for the rest of the file.
                mode = modeStack.isEmpty() ? Normal : modeStack.last();
                mode = modeStack.isEmpty() ? Normal : modeStack.takeLast();
            }
            else all[i] = ' ';
            break;

        case TemplateStr:
            if (c == '\\') { all[i] = ' '; if (i+1 < n) { all[i+1] = ' '; ++i; } }
            else if (c == '`') {
                all[i] = ' ';
                mode = modeStack.isEmpty() ? Normal : modeStack.takeLast();
            }
            else if (c == '$' && next == '{') { all[i] = ' '; all[i+1] = ' '; ++i; modeStack.append(TemplateStr); mode = TemplateExpr; }
            else if (c != '\n') all[i] = ' ';
            break;

        case TemplateExpr:
            if (c == '{') { all[i] = ' '; modeStack.append(TemplateExpr); }
            else if (c == '}') {
                all[i] = ' ';
                mode = modeStack.isEmpty() ? Normal : modeStack.takeLast();
            }
            else if (c == '`') { all[i] = ' '; modeStack.append(TemplateExpr); mode = TemplateStr; }
            else if (c == '"' || c == '\'') { all[i] = ' '; modeStack.append(TemplateExpr); mode = Str; }
            else if (c == '/' && next == '/') { all[i] = ' '; all[i+1] = ' '; ++i; mode = LineComment; }
            // Everything else (real expression code) is left intact.
            break;
        }
    }

    // Re-split into lines.
    QVector<QString> out;
    out.reserve(lines.size());
    int pos = 0;
    for (int i = 0; i < lines.size(); ++i) {
        int end = all.indexOf(QLatin1Char('\n'), pos);
        if (end < 0) end = all.size();
        out.push_back(all.mid(pos, end - pos));
        pos = end + 1;
    }
    return out;
}

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

int findBodyOpenBrace(const QVector<QString>& stripped, int startLine, int colonPos) {
    const QString& l0 = stripped[startLine];
    for (int i = colonPos; i < l0.size(); ++i) {
        if (l0[i] == '{') return startLine;
        if (l0[i] == ';') return -1;
    }
    for (int j = startLine + 1; j < stripped.size() && j <= startLine + 2; ++j) {
        QString t = stripped[j].trimmed();
        if (t.isEmpty()) continue;
        if (t.startsWith('{')) return j;
        return -1;
    }
    return -1;
}

OriginHint classifyHandler(const QString& handler) {
    if (handler.startsWith("Component.on")) return OriginHint::User;

    static const QRegularExpression mouseRe(R"(^on(Clicked|Pressed|Released|DoubleClicked|Wheel)$)");
    static const QRegularExpression keyRe  (R"(^on(KeyPressed|KeyReleased)$)");
    static const QRegularExpression timerRe(R"(^on(Triggered|Timeout)$)");
    static const QRegularExpression deviceRe(R"(^on(DataReady|ReadyRead|ValueChanged|ReadingChanged)$)");

    if (mouseRe.match(handler).hasMatch() || keyRe.match(handler).hasMatch()) return OriginHint::User;
    if (timerRe.match(handler).hasMatch())  return OriginHint::Timer;
    if (deviceRe.match(handler).hasMatch()) return OriginHint::Device;
    return OriginHint::Signal;
}

bool looksLikeHandlerName(const QString& name) {
    return name.size() > 2
           && name[0] == 'o' && name[1] == 'n'
           && name[2].isUpper();
}

// --- Part 2: scan forward for '{', tolerate any intervening lines ---
bool functionHasBlockBody(const QVector<QString>& stripped, int line, int parenStart) {
    // Scan from the '(' on the signature line, tracking paren depth to find
    // the closing ')'. Then look for '{' after it (possibly on a later line).
    int depth = 0;
    int parenEnd = -1;
    for (int ln = line; ln < stripped.size() && ln <= line + 10; ++ln) {
        const QString& l = stripped[ln];
        int start = (ln == line) ? parenStart : 0;
        for (int i = start; i < l.size(); ++i) {
            if (l[i] == '(') ++depth;
            else if (l[i] == ')') {
                --depth;
                if (depth == 0) { parenEnd = i; goto done_paren; }
            }
        }
    }
done_paren:
    if (parenEnd < 0) return false;

    // From the closing ')', scan forward for '{' (must appear before ';' or '}').
    for (int ln = line; ln < stripped.size() && ln <= line + 10; ++ln) {
        const QString& l = stripped[ln];
        int start = (ln == line) ? parenEnd + 1 : 0;
        for (int i = start; i < l.size(); ++i) {
            if (l[i] == '{') return true;
            if (l[i] == ';' || l[i] == '}') return false;
        }
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

    static const QRegularExpression funcRe(
        R"(^\s*function\s+([A-Za-z_$][\w$]*)\s*\()");
    static const QRegularExpression handlerRe(
        R"(^\s*(?:[A-Za-z_]\w*\s*\.\s*)?(Component\.on[A-Za-z]\w*|on[A-Z]\w*)\s*:)");

    static const QRegularExpression bindingBlockRe(
        R"(^\s*(?:readonly\s+)?property\s+\S+\s+([A-Za-z_$][\w$]*)\s*:\s*$)");

    for (int i = 0; i < stripped.size(); ++i) {
        const QString& line = stripped[i];

        // ---------- JS function ----------
       auto funcMatch = funcRe.match(line);
        if (funcMatch.hasMatch()) {
            // parenStart = position right after the name (the '(' is at capturedEnd(1) + whitespace)
            int parenStart = funcMatch.capturedEnd(1);
            // Skip whitespace to get to '('
            while (parenStart < line.size() && line[parenStart] != '(') ++parenStart;
            if (parenStart >= line.size()) continue;

            if (!functionHasBlockBody(stripped, i, parenStart)) continue;

            FunctionInfo info;
            info.filePath       = qmlPath;
            info.name           = funcMatch.captured(1);
            info.qmlHandlerName = info.name;
            info.signatureLine  = i + 1;
            info.bodyKind       = BodyKind::Block;

            if (looksLikeHandlerName(info.name)) {
                info.kind       = SourceKind::QmlHandler;
                info.originHint = classifyHandler(info.name);
            } else {
                info.kind       = SourceKind::QmlFunction;
                info.originHint = OriginHint::Internal;
            }

            // Param parsing: scan from parenStart to the matching ')' and split by ','.
            // (Optional — only needed if you display params in the UI.)
            // ... existing param parsing but adapted for multi-line ...

            // Find the opening brace: from parenStart forward, first '{'.
            int open = -1;
            for (int ln = i; ln < stripped.size() && ln <= i + 10; ++ln) {
                const QString& l = stripped[ln];
                int start = (ln == i) ? parenStart : 0;
                for (int c = start; c < l.size(); ++c) {
                    if (l[c] == '{') { open = ln; break; }
                }
                if (open >= 0) break;
            }
            if (open < 0) continue;

            int close = findCloseBrace(stripped, open);
            if (close < 0) continue;

            info.openBraceLine  = open + 1;
            info.closeBraceLine = close + 1;
            info.bodyStartLine  = info.openBraceLine;
            info.bodyEndLine    = info.closeBraceLine;
            out.push_back(info);
            continue;
        }

        // ---------- Signal handler ----------
        auto handlerMatch = handlerRe.match(line);
        if (handlerMatch.hasMatch()) {
            const QString name = handlerMatch.captured(1);
            const int colonPos = handlerMatch.capturedEnd(1);

            FunctionInfo info;
            info.kind           = SourceKind::QmlHandler;
            info.filePath       = qmlPath;
            info.qmlHandlerName = name;
            info.name           = name;
            info.signatureLine  = i + 1;
            info.originHint     = classifyHandler(name);

            const int openBraceLine = findBodyOpenBrace(stripped, i, colonPos);
            if (openBraceLine < 0) {
                info.bodyKind       = BodyKind::Expression;
                info.openBraceLine  = 0;
                info.closeBraceLine = 0;
                info.bodyStartLine  = i + 1;
                info.bodyEndLine    = i + 1;
                out.push_back(info);
                continue;
            }

            info.bodyKind = BodyKind::Block;
            int close = findCloseBrace(stripped, openBraceLine);
            if (close < 0) continue;
            info.openBraceLine  = openBraceLine + 1;
            info.closeBraceLine = close + 1;
            info.bodyStartLine  = info.openBraceLine;
            info.bodyEndLine    = info.closeBraceLine;
            out.push_back(info);
            continue;
        }

        // ---------- property X: { ... }  (block binding) ----------
        auto bindingMatch = bindingBlockRe.match(line);
        if (bindingMatch.hasMatch()) {
            int open = -1;
            for (int j = i + 1; j < stripped.size() && j <= i + 3; ++j) {
                QString t = stripped[j].trimmed();
                if (t.isEmpty()) continue;
                if (t.startsWith('{')) open = j;
                break;
            }
            if (open < 0) continue;

            int close = findCloseBrace(stripped, open);
            if (close < 0) continue;

            FunctionInfo info;
            info.kind           = SourceKind::QmlFunction;
            info.filePath       = qmlPath;
            info.name           = bindingMatch.captured(1);
            info.qmlHandlerName = info.name;
            info.signatureLine  = i + 1;
            info.originHint     = OriginHint::Internal;
            info.bodyKind       = BodyKind::Block;
            info.openBraceLine  = open + 1;
            info.closeBraceLine = close + 1;
            info.bodyStartLine  = info.openBraceLine;
            info.bodyEndLine    = info.closeBraceLine;
            out.push_back(info);
        }
    }

    return out;
}
