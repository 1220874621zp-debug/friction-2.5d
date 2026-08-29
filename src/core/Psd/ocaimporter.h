#ifndef OCAIMPORTER_H
#define OCAIMPORTER_H

#include <QJsonObject>
#include <QDir>

#include "skia/skiaincludes.h"
#include "smartPointers/selfref.h"

class ContainerBox;
class BoundingBox;
class Canvas;

// Native importer for the Open Cel Animation format (OCA v1.x).
// An OCA "file" is a FOLDER whose name ends with .oca containing a
// JSON manifest at its root plus per-layer frame images (PNG/EXR/SVG)
// in subfolders. Layer tree, blend modes, opacity, positions and frame
// exposure (x-sheet) are all described by the JSON:
//   root: name/width/height/frameRate/startTime/endTime/layers
//   layer: name/type(paintlayer|grouplayer|vectorlayer...)/frames/
//          blendingMode/opacity/position/width/height/visible/
//          childLayers (groups)
//   frame: fileName/frameNumber/duration/opacity/position
class ImportOCA {
public:
    // loads the manifest from an .oca FOLDER; the caller hands the
    // folder path (not the json). Throws enve exceptions on bad data
    static qsptr<ContainerBox> loadOCAFolder(const QString& folderPath,
                                             Canvas* const scene);
private:
    static QJsonObject readManifest(const QDir& ocaDir);
    static qsptr<BoundingBox> buildLayer(const QJsonObject& layerJson,
                                         const QDir& ocaDir,
                                         ContainerBox* const parent,
                                         Canvas* const scene);
    static SkBlendMode blendModeFromOca(const QString& mode);
};

#endif // OCAIMPORTER_H
