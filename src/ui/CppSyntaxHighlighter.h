// ui/CppSyntaxHighlighter.h
#pragma once
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QVector>

class CppSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    enum class Mode { Cpp, Qml };
    explicit CppSyntaxHighlighter(QTextDocument* doc, Mode mode = Mode::Cpp);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat    format;
    };
    QVector<Rule>        m_rules;
    QTextCharFormat      m_multilineCommentFmt;
    QTextCharFormat      m_stringFmt;
    QRegularExpression   m_blockStart;
    QRegularExpression   m_blockEnd;

    void setupCpp();
    void setupQml();
};