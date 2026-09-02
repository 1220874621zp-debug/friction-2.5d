#ifndef QSCIAPIS_H
#define QSCIAPIS_H

#include "qscilexerjavascript.h"

class QsciAPIs : public QObject
{
    Q_OBJECT
public:
    explicit QsciAPIs(QsciLexer *parent = nullptr) : QObject(reinterpret_cast<QObject*>(parent)) {}
    void add(const QString &) {}
    void clear() {}
    void prepare() {}
    bool load(const QString &) { return true; }
};

#endif // QSCIAPIS_H
