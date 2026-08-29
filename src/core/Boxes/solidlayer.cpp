#include "Boxes/solidlayer.h"
#include "Animators/paintsettingsanimator.h"
#include "Animators/outlinesettingsanimator.h"

SolidLayer::SolidLayer() :
    RectangleBox(QObject::tr("Solid Layer"), eBoxType::solid) {
    // a solid is always a filled plane - force flat paint regardless
    // of the last-used fill mode, classic AE default color
    getFillSettings()->setPaintType(PaintType::FLATPAINT);
    getFillSettings()->setCurrentColor(Qt::white);
    getStrokeSettings()->setPaintType(PaintType::NOPAINT);
}
