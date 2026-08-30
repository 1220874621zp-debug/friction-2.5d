#include "ocaimporter.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>

#include "Boxes/imagebox.h"
#include "Boxes/containerbox.h"
#include "canvas.h"
#include "exceptions.h"
#include "Timeline/durationrectangle.h"
#include "Animators/transformanimator.h"
#include "Animators/qpointfanimator.h"

namespace {
// any non-_meta .json/.oca FILE in this directory could be the manifest
bool dirHasManifestCandidate(const QDir& dir) {
    const auto entries = dir.entryInfoList(
                QStringList() << QStringLiteral("*.json")
                              << QStringLiteral("*.oca"),
                QDir::Files, QDir::Name);
    for(const auto& f : entries) {
        if(f.fileName().endsWith(QStringLiteral("_meta.json"))) continue;
        return true;
    }
    return false;
}

QJsonObject tryLoadJsonObject(const QString& path) {
    QFile file(path);
    if(file.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(file.readAll());
        if(doc.isObject()) return doc.object();
    }
    return QJsonObject();
}
}

QJsonObject ImportOCA::readManifest(const QDir& ocaDir,
                                    QDir* const manifestFoundIn) {
    // the manifest is a JSON file; the OCA-Krita reference exporter
    // names it <docname>.oca (JSON content despite the extension) and
    // puts it in a subfolder named after the document inside the
    // .oca folder - so search the root first, then immediate
    // subfolders
    // root: .oca / .json named like the folder, or any non-_meta json
    const auto rootEntries = ocaDir.entryInfoList(
                QStringList() << QStringLiteral("*.json")
                              << QStringLiteral("*.oca"),
                QDir::Files, QDir::Name);
    for(const auto& f : rootEntries) {
        if(f.fileName().endsWith(QStringLiteral("_meta.json"))) continue;
        const auto obj = tryLoadJsonObject(f.absoluteFilePath());
        if(!obj.isEmpty()) {
            if(manifestFoundIn) *manifestFoundIn = ocaDir;
            return obj;
        }
    }
    // immediate subfolders (Krita layout: <folder>.oca/<docname>/...)
    const auto subDirs = ocaDir.entryInfoList(
                QStringList(), QDir::Dirs | QDir::NoDotAndDotDot);
    for(const auto& d : subDirs) {
        const QDir sub(d.absoluteFilePath());
        const auto subEntries = sub.entryInfoList(
                    QStringList() << QStringLiteral("*.json")
                                  << QStringLiteral("*.oca"),
                    QDir::Files, QDir::Name);
        for(const auto& f : subEntries) {
            if(f.fileName().endsWith(QStringLiteral("_meta.json"))) continue;
            const auto obj = tryLoadJsonObject(f.absoluteFilePath());
            if(!obj.isEmpty()) {
                if(manifestFoundIn) *manifestFoundIn = sub;
                return obj;
            }
        }
    }
    RuntimeThrow("No OCA manifest found in " + ocaDir.absolutePath());
}

SkBlendMode ImportOCA::blendModeFromOca(const QString& mode) {
    // OCA blendingMode values as exported by the reference OCA-Krita
    // plugin (snake_case; spec also lists hyphenated variants)
    if(mode == QLatin1String("multiply"))   { return SkBlendMode::kMultiply; }
    if(mode == QLatin1String("screen"))     { return SkBlendMode::kScreen; }
    if(mode == QLatin1String("overlay"))    { return SkBlendMode::kOverlay; }
    if(mode == QLatin1String("darken"))     { return SkBlendMode::kDarken; }
    if(mode == QLatin1String("lighten"))    { return SkBlendMode::kLighten; }
    if(mode == QLatin1String("difference")) { return SkBlendMode::kDifference; }
    if(mode == QLatin1String("exclusion"))  { return SkBlendMode::kExclusion; }
    if(mode == QLatin1String("dodge") ||
       mode == QLatin1String("color_dodge") ||
       mode == QLatin1String("colorodge") ||
       mode == QLatin1String("color-dodge"))
                                              { return SkBlendMode::kColorDodge; }
    if(mode == QLatin1String("burn") ||
       mode == QLatin1String("color_burn") ||
       mode == QLatin1String("colorburn") ||
       mode == QLatin1String("color-burn"))
                                              { return SkBlendMode::kColorBurn; }
    if(mode == QLatin1String("hard_light") ||
       mode == QLatin1String("hardlight") ||
       mode == QLatin1String("hard-light"))
                                              { return SkBlendMode::kHardLight; }
    if(mode == QLatin1String("soft_light") ||
       mode == QLatin1String("softlight") ||
       mode == QLatin1String("soft-light"))
                                              { return SkBlendMode::kSoftLight; }
    if(mode == QLatin1String("hue"))        { return SkBlendMode::kHue; }
    if(mode == QLatin1String("saturation")) { return SkBlendMode::kSaturation; }
    if(mode == QLatin1String("color"))      { return SkBlendMode::kColor; }
    if(mode == QLatin1String("luminosity") ||
       mode == QLatin1String("value"))
                                              { return SkBlendMode::kLuminosity; }
    if(mode == QLatin1String("dissolve"))   { return SkBlendMode::kSrcOver; }
    if(mode == QLatin1String("dst_in") ||
       mode == QLatin1String("destination-in"))
                                              { return SkBlendMode::kDstIn; }
    if(mode == QLatin1String("dst_out") ||
       mode == QLatin1String("destination-out"))
                                              { return SkBlendMode::kDstOut; }
    if(mode == QLatin1String("src_atop") ||
       mode == QLatin1String("source-atop"))
                                              { return SkBlendMode::kSrcATop; }
    // "normal" and unknown (incl. the many Krita-specific modes with
    // no direct skia equivalent - fall back to normal)
    return SkBlendMode::kSrcOver;
}

qsptr<BoundingBox> ImportOCA::buildLayer(const QJsonObject& layerJson,
                                         ContainerBox* const parent,
                                         const QDir& manifestDir) {
    const QString type = layerJson[QStringLiteral("type")].toString(
                QStringLiteral("paintlayer"));

    if(type == QLatin1String("grouplayer")) {
        const auto group = enve::make_shared<ContainerBox>(
                    eBoxType::group);
        group->prp_setName(layerJson[QStringLiteral("name")].toString(
                    QStringLiteral("group")));
        // groups render as-is; passThrough groups are UI-only in OCA
        // (children composite directly with what is below) - closest
        // friction equivalent is still a group (isolated), noted as a
        // known deviation
        parent->addContained(group);
        const auto children = layerJson[QStringLiteral("childLayers")].toArray();
        // OCA stores layers bottom-to-top; addContained PREPENDS, so
        // iterating in order keeps the stacking correct
        for(const auto& child : children) {
            buildLayer(child.toObject(), group.get(), manifestDir);
        }
        const qreal opacity = layerJson[QStringLiteral("opacity")].toDouble(1.);
        group->setOpacity(opacity);
        if(!layerJson[QStringLiteral("visible")].toBool(true)) {
            group->hide();
        }
        return group;
    }

    // paintlayer / vectorlayer / other image-bearing layers: one
    // ImageBox per OCA frame, with a duration rectangle for the
    // exposure (frameNumber .. +duration)
    const auto frames = layerJson[QStringLiteral("frames")].toArray();
    if(frames.isEmpty()) return nullptr;
    BoundingBox* firstBox = nullptr;
    for(const auto& f : frames) {
        const auto frameJson = f.toObject();
        const QString fileName = frameJson[QStringLiteral("fileName")].toString();
        if(fileName.isEmpty()) continue; // "_blank" placeholder frame
        const QString absPath = manifestDir.absoluteFilePath(fileName);
        if(!QFile::exists(absPath)) {
            qWarning() << "OCA import: missing frame file" << absPath;
            continue;
        }
        const auto img = enve::make_shared<ImageBox>(absPath);
        img->prp_setName(layerJson[QStringLiteral("name")].toString());
        // position is the layer/frame CENTER in canvas pixels
        const auto posJson = frameJson[QStringLiteral("position")].toArray();
        const auto layerPos = layerJson[QStringLiteral("position")].toArray();
        const qreal cx = posJson.count() > 0 ? posJson.at(0).toDouble() :
                        (layerPos.count() > 0 ?
                             layerPos.at(0).toDouble() : 0.);
        const qreal cy = posJson.count() > 1 ? posJson.at(1).toDouble() :
                        (layerPos.count() > 1 ?
                             layerPos.at(1).toDouble() : 0.);
        // friction position is the top-left of the image; the image
        // size is only known after load, so shift later via translate
        // using the frame dimensions from the manifest when present
        const qreal w = frameJson[QStringLiteral("width")].toDouble(
                    layerJson[QStringLiteral("width")].toDouble(0.));
        const qreal h = frameJson[QStringLiteral("height")].toDouble(
                    layerJson[QStringLiteral("height")].toDouble(0.));
        const qreal opacity = frameJson[QStringLiteral("opacity")].toDouble(1.);
        if(w > 0. && h > 0.) {
            img->getBoxTransformAnimator()->
                    getPosAnimator()->setBaseValue(
                        cx - w*0.5, cy - h*0.5);
        }
        if(opacity < 0.999) img->setOpacity(opacity);
        // exposure: visible from frameNumber for duration frames
        const int frameNumber = frameJson[QStringLiteral("frameNumber")].toInt(0);
        const int duration = frameJson[QStringLiteral("duration")].toInt(1);
        if(frameNumber != 0 || duration != 1) {
            const auto dur = enve::make_shared<DurationRectangle>(
                        *img.get());
            dur->setMinRelFrame(frameNumber);
            dur->setMaxRelFrame(frameNumber + duration - 1);
            img->setDurationRectangle(dur);
        }
        parent->addContained(img);
        if(!firstBox) firstBox = img.get();
        else {
            // additional frames of the same layer hide by default -
            // their duration rectangles make them visible in time
            // (in-scene visibility handles the rest at render time)
        }
    }
    if(!firstBox) return nullptr;
    firstBox->setBlendModeSk(blendModeFromOca(
            layerJson[QStringLiteral("blendingMode")].toString(
                QStringLiteral("normal"))));
    const qreal layerOpacity = layerJson[QStringLiteral("opacity")].toDouble(1.);
    if(layerOpacity < 0.999) firstBox->setOpacity(layerOpacity);
    if(!layerJson[QStringLiteral("visible")].toBool(true)) {
        firstBox->hide();
    }
    return firstBox->ref<BoundingBox>();
}

bool ImportOCA::looksLikeOCA(const QString& folderPath) {
    const QDir dir(folderPath);
    if(!dir.isReadable()) return false;
    if(dirHasManifestCandidate(dir)) return true;
    // Krita nests the manifest one level down: <pkg>.oca/<docname>/
    const auto subDirs = dir.entryInfoList(
                QStringList(), QDir::Dirs | QDir::NoDotAndDotDot);
    for(const auto& d : subDirs) {
        if(dirHasManifestCandidate(QDir(d.absoluteFilePath()))) {
            return true;
        }
    }
    return false;
}

bool ImportOCA::looksLikeOCAJson(const QString& filePath) {
    const auto obj = tryLoadJsonObject(filePath);
    if(obj.isEmpty()) return false;
    return obj.contains(QStringLiteral("layers")) ||
           obj.contains(QStringLiteral("ocaVersion"));
}

qsptr<ContainerBox> ImportOCA::loadOCAManifestFile(const QString& filePath,
                                                   Canvas* const scene) {
    Q_UNUSED(scene)
    const auto manifest = tryLoadJsonObject(filePath);
    if(manifest.isEmpty()) {
        RuntimeThrow(filePath + " is not a readable JSON object.");
    }
    if(!manifest.contains(QStringLiteral("layers"))) {
        RuntimeThrow(filePath +
                     " has no \"layers\" array - not an OCA manifest.");
    }
    // frame fileName paths are relative to the manifest's own folder
    // (same rule as the DuIO/AE reference importer: file.parent)
    const QDir manifestDir = QFileInfo(filePath).absoluteDir();
    qWarning() << "OCA: importing manifest file" << filePath;
    return buildTree(manifest, manifestDir);
}

qsptr<ContainerBox> ImportOCA::loadOCAFolder(const QString& folderPath,
                                             Canvas* const scene) {
    Q_UNUSED(scene)
    const QDir ocaDir(folderPath);
    QDir manifestDir = ocaDir;
    const auto manifest = readManifest(ocaDir, &manifestDir);
    return buildTree(manifest, manifestDir);
}

qsptr<ContainerBox> ImportOCA::buildTree(const QJsonObject& manifest,
                                         const QDir& manifestDir) {
    // the whole document becomes one group named after the OCA doc
    const auto rootGroup = enve::make_shared<ContainerBox>(
                eBoxType::group);
    rootGroup->prp_setName(manifest[QStringLiteral("name")].toString(
                QStringLiteral("OCA")));

    const auto layers = manifest[QStringLiteral("layers")].toArray();
    if(layers.isEmpty()) {
        RuntimeThrow("OCA manifest contains no layers.");
    }
    qWarning() << "OCA: building" << layers.count()
               << "layers from manifest in" << manifestDir.absolutePath();
    // OCA stores layers bottom-to-top; addContained prepends so the
    // final stacking order matches the manifest
    for(const auto& layer : layers) {
        buildLayer(layer.toObject(), rootGroup.get(), manifestDir);
    }
    return rootGroup;
}
