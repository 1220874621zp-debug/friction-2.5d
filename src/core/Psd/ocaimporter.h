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
    // cheap check: does the folder hold anything resembling an OCA
    // manifest (any .json / .oca file at the root or in an immediate
    // subfolder - the Krita exporter nests the manifest one level
    // down)?
    static bool looksLikeOCA(const QString& folderPath);
    // cheap check: is this .json/.oca FILE an OCA manifest (parses
    // and looks for the layers array / ocaVersion key)?
    static bool looksLikeOCAJson(const QString& filePath);
    // loads the manifest from an .oca FOLDER; the caller hands the
    // folder path (not the json). Throws enve exceptions on bad data
    static qsptr<ContainerBox> loadOCAFolder(const QString& folderPath,
                                             Canvas* const scene);
    // loads OCA starting from the manifest FILE the user picked;
    // frame paths resolve relative to the manifest's own folder
    // (same rule as the DuIO/AE reference importer). Throws enve
    // exceptions on bad data
    static qsptr<ContainerBox> loadOCAManifestFile(const QString& filePath,
                                                   Canvas* const scene);
private:
    static QJsonObject readManifest(const QDir& ocaDir,
                                    QDir* const manifestFoundIn = nullptr);
    static qsptr<ContainerBox> buildTree(const QJsonObject& manifest,
                                         const QDir& manifestDir);
    static qsptr<BoundingBox> buildLayer(const QJsonObject& layerJson,
                                         ContainerBox* const parent,
                                         const QDir& manifestDir);
    static SkBlendMode blendModeFromOca(const QString& mode);
};

#endif // OCAIMPORTER_H
