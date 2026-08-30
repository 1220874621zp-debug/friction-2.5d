/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# See 'README.md' for more information.
#
*/

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#include "expression.h"

#include "exceptions.h"
#include "Private/esettings.h"
#include "Animators/animator.h"

#include <cmath>

Expression::ResultTester Expression::sQrealAnimatorTester =
        [](const QJSValue& val) {
            if(!val.isNumber()) PrettyRuntimeThrow("Invalid return type");
        };

Expression::Expression(const QString& definitionsStr,
                       const QString& scriptStr,
                       PropertyBindingMap&& bindings,
                       std::unique_ptr<QJSEngine>&& engine,
                       QJSValue&& eEvaluate) :
    mDefinitionsStr(definitionsStr),
    mScriptStr(scriptStr),
    mEEvaluate(std::move(eEvaluate)),
    mBindings(std::move(bindings)),
    mEngine(std::move(engine)) {
    for(const auto& binding : mBindings) {
        connect(binding.second.get(), &PropertyBinding::currentValueChanged,
                this, &Expression::currentValueChanged);
        connect(binding.second.get(), &PropertyBinding::relRangeChanged,
                this, &Expression::relRangeChanged);
    }
    parseLoopHeader();
}


void throwIfError(const QJSValue& value, const QString& name) {
    if(value.isError()) {
        PrettyRuntimeThrow("Uncaught exception in " + name + " at line "
                           + value.property("lineNumber").toString() +
                           ":\n" + value.toString());
    }
}

void Expression::sAddDefinitionsTo(const QString& definitionsStr,
                                   QJSEngine& e)
{
    QString defs;
    const auto expressions = eSettings::sInstance->fExpressions.getDefinitions();
    for (const auto &expr : expressions) { defs.append(expr.definitions); }
    defs.append(definitionsStr);

    const auto defRet = e.evaluate(defs);
    throwIfError(defRet, "Definitions");
}

void Expression::sAddScriptTo(const QString& scriptStr,
                              const PropertyBindingMap& bindings,
                              QJSEngine& e, QJSValue& eEvaluate,
                              const ResultTester& resultTester) {
    QStringList bindingVars;
    QJSValueList testArgs;
    for(const auto& binding : bindings) {
        bindingVars << binding.first;
        testArgs << binding.second->getJSValue(e);
    }
    const QString evalVars = bindingVars.join(", ");
    eEvaluate = e.evaluate(
            "var eEvaluate;"
            "eEvaluate = function(" + evalVars + ") {" +
                scriptStr +
            "}");
    throwIfError(eEvaluate, "Script");
    if(!eEvaluate.isCallable())
        PrettyRuntimeThrow("Uncallable script.");
    const auto testResult = eEvaluate.call(testArgs);
    if(testResult.isError()) {
        PrettyRuntimeThrow("Script test error:\n" +
                           testResult.toString());
    } else if(resultTester) resultTester(testResult);
}

qsptr<Expression> Expression::sCreate(const QString& bindingsStr,
                                      const QString& definitionsStr,
                                      const QString& scriptStr,
                                      const Property* const context,
                                      const ResultTester& resultTester) {
    auto bindings = PropertyBindingParser::parseBindings(
                              bindingsStr, nullptr, context);
    auto engine = std::make_unique<QJSEngine>();
    sAddDefinitionsTo(definitionsStr, *engine);
    QJSValue eEvaluate;
    sAddScriptTo(scriptStr, bindings, *engine, eEvaluate, resultTester);
    return sCreate(definitionsStr, scriptStr,
                   std::move(bindings),
                   std::move(engine),
                   std::move(eEvaluate));
}

qsptr<Expression> Expression::sCreate(const QString& definitionsStr,
                                      const QString& scriptStr,
                                      PropertyBindingMap&& bindings,
                                      std::unique_ptr<QJSEngine>&& engine,
                                      QJSValue&& eEvaluate) {
    if(!eEvaluate.isCallable())
        RuntimeThrow("Uncallable script:\n" + scriptStr);
    return qsptr<Expression>(new Expression(definitionsStr, scriptStr,
                                            std::move(bindings),
                                            std::move(engine),
                                            std::move(eEvaluate)));
}

bool Expression::setAbsFrame(const int absFrame) {
    bool changed = false;
    for(const auto& binding : mBindings) {
        const bool c = binding.second->setAbsFrame(absFrame);
        changed = changed || c;
    }
    return changed;
}

bool Expression::isStatic() const {
    return identicalRelRange(0) == FrameRange::EMINMAX;
}

bool Expression::isValid() {
    for(const auto& binding : mBindings) {
        const bool valid = binding.second->isValid();
        if(!valid) return false;
    }
    return true;
}

bool Expression::dependsOn(const Property* const prop) {
    for(const auto& binding : mBindings) {
        const bool depends = binding.second->dependsOn(prop);
        if(depends) return true;
    }
    return false;
}

QJSValue Expression::evaluate() {
    if (mLoopMode && mLoopKeys) {
        return evaluate(mLoopKeys->anim_getCurrentRelFrame());
    }
    QJSValueList values;
    for(const auto& binding : mBindings) {
        values << binding.second->getJSValue(*mEngine);
    }
    return mEEvaluate.call(values);
}

QJSValue Expression::evaluate(const qreal relFrame)
{
    const qreal f = mLoopMode ? mapLoopFrame(relFrame) : relFrame;
    QJSValueList values;
    for (const auto& binding : mBindings) {
        QString path = binding.second->path();
        QJSValue val = binding.second->getJSValue(*mEngine, f);
        if (path == "$frame") { values << QJSValue(f); }
        else { values << val; }
    }
    QJSValue res = mEEvaluate.call(values);
    return res;
}

void Expression::parseLoopHeader() {
    const QString firstLine = mScriptStr.section(QLatin1Char('\n'), 0, 0)
                                  .trimmed();
    if (firstLine == QLatin1String("//loop:cycle")) {
        mLoopMode = 1;
    } else if (firstLine == QLatin1String("//loop:pingpong")) {
        mLoopMode = 2;
    } else if (firstLine.startsWith(QLatin1String("//loop:skip="))) {
        const QString skipStr = firstLine.mid(12).trimmed();
        bool ok = false;
        const int skip = skipStr.toInt(&ok);
        if (ok) {
            mLoopMode = 3;
            mLoopSkip = qMax(1, skip);
        }
    }
}

qreal Expression::mapLoopFrame(const qreal relFrame) const {
    if (!mLoopKeys) return relFrame;
    const auto& keys = mLoopKeys->anim_getKeys();
    const int n = keys.count();
    if (n < 2) return relFrame;
    // keys are sorted by frame; cycle over the animator's own list
    Key* first = nullptr;
    Key* last = nullptr;
    Key* start = nullptr;
    const int startId = mLoopMode == 3 ?
                qBound(1, mLoopSkip, n - 1) : 0;
    int i = 0;
    for (auto it = keys.begin(); it != keys.end(); ++it, i++) {
        Key* const k = *it;
        if (!k) continue;
        if (!first) first = k;
        last = k;
        if (i == startId) start = k;
    }
    if (!first || !last || !start) return relFrame;
    const qreal firstF = start->getRelFrame();
    const qreal lastF = last->getRelFrame();
    if (relFrame <= lastF) return relFrame;
    if (mLoopMode == 2) { // ping-pong: 1,2,3 -> 1,2,3,2,1,2,3...
        const qreal period = lastF - firstF;
        if (period < 1.) return relFrame;
        const qreal off = std::fmod(relFrame - firstF, 2. * period);
        return off < period ? firstF + off : lastF - (off - period);
    } else { // cycle / skip: the period includes the last frame, so
             // the frame right after the last key shows the first
             // cycled value (keys 1,2,3 -> frame 4 shows key 1)
        const qreal period = lastF - firstF + 1.;
        return firstF + std::fmod(relFrame - firstF, period);
    }
}

FrameRange Expression::identicalRelRange(const int absFrame) const {
    // loop modes break the "value stays constant past the last key"
    // assumption the cache relies on - fall back to per-frame
    if (mLoopMode) return {absFrame, absFrame};
    FrameRange result{FrameRange::EMINMAX};
    for(const auto& binding : mBindings) {
        const auto prop = binding.second.get();
        result *= prop->identicalRelRange(absFrame);
        if(result.isUnary()) return result;
    }
    return result;
}

FrameRange Expression::nextNonUnaryIdenticalRelRange(const int absFrame) const
{
    for (int i = absFrame; i < FrameRange::EMAX; i++) {
        FrameRange result{FrameRange::EMINMAX};
        int lowestMax = INT_MAX;
        for (const auto& binding : mBindings) {
            // ok, so binding.second is bork, why? ask the original author, I don't know or care anymore :)
            // I don't see any issues with this (everything works), but it's not "good" code either :P
            Q_UNUSED(binding)
            //const auto prop = binding.second.get();
            const auto childRange = FrameRange{FrameRange::EMAX/2, FrameRange::EMAX}; //prop->nextNonUnaryIdenticalRelRange(i);
            lowestMax = qMin(lowestMax, childRange.fMax);
            result *= childRange;
        }
        if (!result.isUnary()) { return result; }
        i = lowestMax;
    }

    return FrameRange::EMINMAX;
}

QString Expression::bindingsString() const {
    QString result;
    for(const auto& binding : mBindings) {
        result += binding.first + " = " + binding.second->path() + ";\n";
    }
    return result;
}
