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
*/

#include "scenenavigatorbar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>

#include "Private/document.h"
#include "canvas.h"
#include "Boxes/containerbox.h"
#include "Boxes/internallinkcanvas.h"
#include "Animators/eboxorsound.h"
#include "GUI/global.h"
#include "smartPointers/ememory.h"

QList<Canvas*> SceneNavigatorBar::sPath;

namespace {
// resolve a link (or a chain of links) to the scene owning its target
Canvas* resolveLinkScene(InternalLinkCanvas* const link) {
    auto target = link->getLinkTarget();
    int guard = 0;
    while (target && target->isLink() && guard++ < 16) {
        const auto inner = enve_cast<InternalLinkCanvas*>(target);
        if (!inner) return nullptr;
        target = inner->getLinkTarget();
    }
    if (!target) return nullptr;
    return target->getParentScene();
}

void collectLinkedScenes(ContainerBox* const group,
                         QList<Canvas*>* const out,
                         const int depth) {
    if (!group || depth > 8) return;
    for (const auto& child : group->getContained()) {
        const auto raw = child.get();
        const auto link = enve_cast<InternalLinkCanvas*>(raw);
        if (link) {
            const auto scene = resolveLinkScene(link);
            if (scene && !out->contains(scene)) *out << scene;
            // links nested inside the linked scene show up when
            // navigating INTO it, not here
            continue;
        }
        const auto inner = enve_cast<ContainerBox*>(raw);
        if (inner) collectLinkedScenes(inner, out, depth + 1);
    }
}

// drop path entries whose scene no longer exists
void prunePath(QList<Canvas*>& path, Document& doc) {
    for (int i = path.count() - 1; i >= 0; i--) {
        const auto entry = path.at(i);
        bool alive = false;
        for (const auto& scene : doc.fScenes) {
            if (scene.get() == entry) { alive = true; break; }
        }
        if (!alive) path.removeAt(i);
    }
}

void setupFlatBtn(QPushButton* const btn) {
    btn->setObjectName("QActionButton");
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setFlat(true);
    eSizesUI::widget.add(btn, [btn](const int size) {
        btn->setFixedHeight(size);
    });
}
}

SceneNavigatorBar::SceneNavigatorBar(Document& doc,
                                     QWidget* const parent) :
    QWidget(parent), mDocument(doc) {
    // Preferred (NOT Ignored - Ignored squeezed the bar to zero
    // width in the menu row) + a zero minimumSizeHint so the row can
    // still shrink and engage the breadcrumb overflow collapse
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    eSizesUI::widget.add(this, [this](const int size) {
        setFixedHeight(size);
    });
    mRowLayout = new QHBoxLayout(this);
    mRowLayout->setContentsMargins(0, 0, 0, 0);
    mRowLayout->setSpacing(2);

    connect(&mDocument, &Document::activeSceneSet,
            this, &SceneNavigatorBar::onActiveScene);
    connect(&mDocument, &Document::sceneCreated,
            this, [this](Canvas*) { rebuild(); });
    connect(&mDocument, qOverload<Canvas*>(&Document::sceneRemoved),
            this, &SceneNavigatorBar::onSceneRemoved);
}

QList<Canvas*> SceneNavigatorBar::linkedScenes(Canvas* const scene) {
    QList<Canvas*> result;
    collectLinkedScenes(scene, &result, 0);
    return result;
}

void SceneNavigatorBar::onActiveScene(Canvas* const scene) {
    prunePath(sPath, mDocument);
    qWarning() << "NAV: activeScene"
               << (scene ? scene->prp_getName() : QString("null"));
    if (scene) {
        if (sPath.isEmpty() || sPath.last() != scene) {
            // entering a scene nested in the current one extends the
            // breadcrumb; anything else (dropdown, layout combo)
            // starts a fresh path
            bool nestedStep = false;
            if (!sPath.isEmpty()) {
                nestedStep = linkedScenes(sPath.last()).contains(scene);
            }
            if (nestedStep) sPath << scene;
            else sPath = {scene};
        }
    } else {
        sPath.clear();
    }
    rebind(scene);
    rebuild();
}

void SceneNavigatorBar::onSceneRemoved(Canvas* const scene) {
    Q_UNUSED(scene)
    prunePath(sPath, mDocument);
    rebuild();
}

void SceneNavigatorBar::rebind(Canvas* const scene) {
    QObject::disconnect(mInsConn);
    QObject::disconnect(mRemConn);
    for (const auto& conn : mNameConns) { QObject::disconnect(conn); }
    mNameConns.clear();
    if (!scene) return;

    mInsConn = connect(scene, &ContainerBox::insertedObject,
            this, [this](int, eBoxOrSound*) { rebuild(); });
    mRemConn = connect(scene, &ContainerBox::removedObject,
            this, [this](int, eBoxOrSound*) { rebuild(); });

    QList<Canvas*> named;
    for (const auto& scene : mDocument.fScenes) {
        const auto s = scene.get();
        if (s && !named.contains(s)) named << s;
    }
    for (const auto& s : named) {
        mNameConns << connect(s, &Canvas::prp_nameChanged,
                this, [this](const QString&) { rebuild(); });
    }
}

void SceneNavigatorBar::rebuild() {
    prunePath(sPath, mDocument);
    while (mRowLayout->count()) {
        const auto item = mRowLayout->takeAt(0);
        delete item->widget();
        delete item;
    }
    mMoreBtn = nullptr;
    mMoreMenu = nullptr;
    mCrumbBtns.clear();
    mChipScenes.clear();

    // left-most "..." overflow button, hidden unless the row is
    // too narrow; its menu lists the collapsed chips
    mMoreBtn = new QPushButton("...", this);
    setupFlatBtn(mMoreBtn);
    mMoreMenu = new QMenu(mMoreBtn);
    mMoreBtn->setMenu(mMoreMenu);
    mRowLayout->addWidget(mMoreBtn);
    mMoreBtn->hide();

    // flat scene list: EVERY other scene of the project as a
    // directly clickable chip (the current scene is shown by the
    // row-start dropdown, rendering it here too read as two
    // identical dropdowns). Scenes nested (linked) inside the
    // current scene carry a bold marker - that is the
    // folder-structure part
    const auto cur = sPath.isEmpty() ? nullptr : sPath.last();
    QList<Canvas*> nested;
    if (cur) nested = linkedScenes(cur);

    const auto addChip = [this](Canvas* const scene, const bool isNested) {
        const QString name = scene->prp_getName();
        const QString label = isNested
                ? QString(QChar(0x25B8)) + QStringLiteral(" ") + name
                : name;
        const auto chip = new QPushButton(
                    fontMetrics().elidedText(label,
                                             Qt::ElideMiddle, 160), this);
        setupFlatBtn(chip);
        chip->setToolTip(name);
        if (isNested) {
            auto font = chip->font();
            font.setBold(true);
            chip->setFont(font);
        }
        connect(chip, &QPushButton::clicked, this,
                [this, scene]() {
            emit sceneRequested(scene);
        });
        mRowLayout->addWidget(chip);
        mCrumbBtns << chip;
        mChipScenes << scene;
    };

    // plain sibling scenes first, nested ones last: the overflow
    // collapse eats from the left, so the nested (drill-in) chips
    // survive the longest
    for (const auto& scene : mDocument.fScenes) {
        const auto s = scene.get();
        if (!s || s == cur || nested.contains(s)) { continue; }
        addChip(s, false);
    }
    for (const auto& scene : mDocument.fScenes) {
        const auto s = scene.get();
        if (!s || s == cur || !nested.contains(s)) { continue; }
        addChip(s, true);
    }
    updateOverflow();
    qWarning() << "NAV: rebuild chips=" << mCrumbBtns.count()
               << "nested=" << nested.count()
               << "w=" << width() << "visible=" << isVisible();
}

void SceneNavigatorBar::updateOverflow() {
    if (!mMoreBtn || !mMoreMenu) { return; }
    mMoreMenu->clear();
    for (const auto& btn : mCrumbBtns) { btn->show(); }

    const auto totalWidth = [this]() {
        int w = 0;
        int visible = 0;
        for (int i = 0; i < mRowLayout->count(); i++) {
            const auto item = mRowLayout->itemAt(i);
            const auto wid = item->widget();
            if (!wid || wid->isHidden()) { continue; }
            w += item->sizeHint().width();
            visible++;
        }
        return w + mRowLayout->spacing() * qMax(0, visible - 1);
    };

    int hiddenCount = 0;
    while (totalWidth() > width() &&
           hiddenCount < mCrumbBtns.count()) {
        // collapse chips from the left; the bold nested chips are
        // added last so they survive the longest
        mCrumbBtns.at(hiddenCount)->hide();
        hiddenCount++;
    }

    if (hiddenCount > 0) {
        mMoreBtn->show();
        for (int i = 0; i < hiddenCount; i++) {
            const auto scene = mChipScenes.value(i);
            if (!scene) { continue; }
            const auto act = mMoreMenu->addAction(scene->prp_getName());
            connect(act, &QAction::triggered, this, [this, scene]() {
                emit sceneRequested(scene);
            });
        }
    } else {
        mMoreBtn->hide();
    }
}

void SceneNavigatorBar::resizeEvent(QResizeEvent* const e) {
    QWidget::resizeEvent(e);
    updateOverflow();
}

QSize SceneNavigatorBar::minimumSizeHint() const {
    return QSize(0, QWidget::minimumSizeHint().height());
}
