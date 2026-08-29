#ifndef SOLIDLAYER_H
#define SOLIDLAYER_H

#include "Boxes/rectangle.h"

// AE-style solid layer: a flat-color rectangle. Inherits everything
// from RectangleBox (animated fill color, draggable corners, effects,
// serialization) - the subclass only carries its own type tag so it
// keeps its identity (icon, name) across save/load
class CORE_EXPORT SolidLayer : public RectangleBox {
    e_OBJECT
protected:
    SolidLayer();
};

#endif // SOLIDLAYER_H
