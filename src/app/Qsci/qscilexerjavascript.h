#ifndef QSCILEXERJAVASCRIPT_H
#define QSCILEXERJAVASCRIPT_H

#include <QObject>
#include <QColor>
#include <QFont>
#include "qsciscintilla.h"

class QsciLexer : public QObject
{
    Q_OBJECT
public:
    explicit QsciLexer(QObject *parent = nullptr) : QObject(parent) {}
    virtual const char *keywords(int) const { return nullptr; }
};

class QsciLexerJavaScript : public QsciLexer
{
    Q_OBJECT
public:
    enum {
        Default = 0,
        Comment,
        CommentLine,
        CommentDoc,
        CommentLineDoc,
        Number,
        DoubleQuotedString,
        SingleQuotedString,
        Keyword,
        KeywordSet2,
        GlobalClass
    };

    explicit QsciLexerJavaScript(QsciScintilla *editor = nullptr)
        : QsciLexer(editor)
    {
        if (editor) { editor->setLexer(this); }
    }

    void setDefaultPaper(const QColor &) {}
    void setFont(const QFont &) {}
    void setColor(const QColor &, int = Default) {}
    void setAutoIndentStyle(int) {}
};

#endif // QSCILEXERJAVASCRIPT_H
