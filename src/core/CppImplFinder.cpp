// core/CppImplFinder.cpp
#include "CppImplFinder.h"
#include "BraceScanner.h"
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace {

// Return true if this line looks like the start of a member-function
// definition: contains "ClassName::name(", does not contain ';' after the
// '(', does not start with '#', and is not a template declaration.
bool looksLikeDefinitionHeader(const QString& stripped, QString* outQualifiedName) {
    const QString t = stripped.trimmed();
    if (t.isEmpty()) return false;
    if (t.startsWith('#')) return false;

    // Reject obvious non-definitions
    if (t.startsWith("template")) return false;
    if (t.startsWith("using "))  return false;
    if (t.startsWith("typedef ")) return false;
    if (t.startsWith("namespace ")) return false;
    if (t.startsWith("class ")) return false;
    if (t.startsWith("struct ")) return false;
    if (t.startsWith("enum ")) return false;

    // Must contain "::" and "("
    const int scope = t.indexOf("::");
    if (scope < 0) return false;
    const int paren = t.indexOf('(', scope);
    if (paren < 0) return false;

    // A ';' on the same line after '(' means forward decl, not definition.
    const int semi = t.indexOf(';', paren);
    if (semi >= 0) return false;

    // Extract qualified name: identifier(::identifier)+(? maybe ~)
    // We want exactly Class::name (allow namespaced class too).
    // Use a regex: ([\w:]+(?:::[\w~]+)+)\s*\(
    static const QRegularExpression nameRe(
        R"(([A-Za-z_]\w*(?:::[A-Za-z_~]\w*)+)\s*\()");
    auto m = nameRe.match(t);
    if (!m.hasMatch()) return false;

    // Reject operators and template angle brackets
    const QString qualified = m.captured(1);
    if (qualified.contains('<') || qualified.contains('>')) return false;
    if (qualified.contains("operator")) return false;

    if (outQualifiedName) *outQualifiedName = qualified;
    return true;
}

// Find the line index of the opening `{` of a function body, starting
// from `sigLine`. Stops at a top-level `;` or `}` (meaning no body follows).
// Tracks paren depth so commas/semicolons inside parameter defaults are
// tolerated (though rare in definitions).
int findBodyOpenBrace(const QVector<QString>& stripped, int sigLine, int maxLookahead = 60) {
    const int last = qMin(sigLine + maxLookahead, stripped.size() - 1);
    int parenDepth = 0;
    for (int i = sigLine; i <= last; ++i) {
        const QString& l = stripped[i];
        for (int c = 0; c < l.size(); ++c) {
            const QChar ch = l[c];
            if (ch == QLatin1Char('(')) { ++parenDepth; continue; }
            if (ch == QLatin1Char(')')) { --parenDepth; continue; }
            if (parenDepth == 0) {
                if (ch == QLatin1Char(';') || ch == QLatin1Char('}'))
                    return -1;
                if (ch == QLatin1Char('{'))
                    return i;
            }
        }
    }
    return -1;
}

// Split "ClassName::method" into ("ClassName", "method").
void splitQualified(const QString& qualified, QString* cls, QString* name) {
    const int pos = qualified.lastIndexOf("::");
    *cls  = qualified.left(pos);
    *name = qualified.mid(pos + 2);
}

} // namespace

QVector<FunctionInfo> CppImplFinder::findImplementations(const QString& cppFilePath,
                                                         const QVector<FunctionInfo>& /*headerFns*/) {
    QVector<FunctionInfo> found;
    QFile f(cppFilePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return found;

    QTextStream ts(&f);
    QVector<QString> lines;
    while (!ts.atEnd()) lines.push_back(ts.readLine());

    // Strip comments / string / template literals (already in your project).
    const QVector<QString> stripped = BraceScanner::stripLiteralsAndComments(lines);

    int i = 0;
    while (i < stripped.size()) {
        QString qualified;
        if (!looksLikeDefinitionHeader(stripped[i], &qualified)) {
            ++i;
            continue;
        }

        const int openBrace = findBodyOpenBrace(stripped, i);
        if (openBrace < 0) { ++i; continue; }

        const int closeBrace = BraceScanner::findMatchingClose(stripped, openBrace);
        if (closeBrace < 0) { ++i; continue; }

        FunctionInfo fi;
        splitQualified(qualified, &fi.className, &fi.name);
        fi.filePath         = cppFilePath;
        fi.signatureLine    = i + 1;
        fi.openBraceLine    = openBrace + 1;
        fi.closeBraceLine   = closeBrace + 1;
        fi.isInlineInHeader = false;

        // Populate params as best-effort (only used for the preview table
        // and for the EditPlanner string builder — if you don't use them,
        // leave empty).
        // (Optionally: parse from `stripped[i..openBrace]`, but for tracing
        //  purposes an empty param list is fine.)

        found.push_back(fi);

        // Jump past this body so lambdas inside it don't get picked up
        // as top-level definitions. The next candidate starts after the
        // closing brace of this function.
        i = closeBrace + 1;
    }

    return found;
}
