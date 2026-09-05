#ifndef QSCISCINTILLA_H
#define QSCISCINTILLA_H

#include <QTextEdit>
#include <QFocusEvent>
#include <QColor>
#include <QFont>

class QsciLexer;

class QsciScintilla : public QTextEdit
{
    Q_OBJECT
public:
    enum { NumberMargin = 0 };
    enum { SloppyBraceMatch = 0 };
    enum { AiMaintain = 0 };
    enum { AcsAll = 0 };
    enum { SCI_SETKEYWORDS = 0 };

    explicit QsciScintilla(QWidget *parent = nullptr) : QTextEdit(parent) {}

    void setMargins(int) {}
    void setMarginType(int, int) {}
    void setMarginWidth(int, const QString &) {}
    void setMarginsFont(const QFont &) {}
    void setMarginsForegroundColor(const QColor &) {}
    void setMarginsBackgroundColor(const QColor &) {}
    void setTabWidth(int) {}
    void setBraceMatching(int) {}
    void setMatchedBraceBackgroundColor(const QColor &) {}
    void setUnmatchedBraceBackgroundColor(const QColor &) {}
    void setMatchedBraceForegroundColor(const QColor &) {}
    void setUnmatchedBraceForegroundColor(const QColor &) {}
    void setCaretForegroundColor(const QColor &) {}
    void setCaretWidth(int) {}
    void setAutoCompletionThreshold(int) {}
    void setAutoCompletionCaseSensitivity(bool) {}
    void setScrollWidth(int) {}
    void setScrollWidthTracking(bool) {}
    void setAutoCompletionSource(int) {}
    void SendScintilla(int, int, const char *) {}
    void recolor() {}
    int length() const { return toPlainText().length(); }

    QsciLexer *lexer() const { return mLexer; }
    void setLexer(QsciLexer *lexer) { mLexer = lexer; }

    QString text() const { return toPlainText(); }
    virtual void setText(const QString &text) { QTextEdit::setText(text); }

public slots:
    void autoCompleteFromAll() {}

signals:
    void SCN_FOCUSIN();
    void SCN_FOCUSOUT();

protected:
    void focusInEvent(QFocusEvent *e) override {
        QTextEdit::focusInEvent(e);
        emit SCN_FOCUSIN();
    }
    void focusOutEvent(QFocusEvent *e) override {
        QTextEdit::focusOutEvent(e);
        emit SCN_FOCUSOUT();
    }

private:
    QsciLexer *mLexer = nullptr;
};

#endif // QSCISCINTILLA_H
