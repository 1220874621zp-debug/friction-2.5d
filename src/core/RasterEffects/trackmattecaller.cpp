#include "trackmattecaller.h"

#include "glhelpers.h"
#include "skia/skiaincludes.h"

bool TrackMatteCaller::sInitialized = false;
GLuint TrackMatteCaller::sProgramId = 0;
GLint TrackMatteCaller::sMaskTexLoc = 0;
GLint TrackMatteCaller::sModeLoc = 0;
GLint TrackMatteCaller::sRect2Loc = 0;

static const char* const sTrackMatteFrag =
    "#version 330 core\n"
    "in vec2 texCoord;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D tex;\n"
    "uniform sampler2D mask;\n"
    "uniform vec4 rect2;\n"
    "uniform int mode;\n"
    "void main(void) {\n"
    "    vec4 s = texture(tex, texCoord);\n"
    "    vec2 uv2 = vec2(mix(rect2.x, rect2.z, texCoord.x),\n"
    "                    mix(rect2.y, rect2.w, texCoord.y));\n"
    "    vec4 m = texture(mask, uv2);\n"
    "    float ma = (mode == 1 || mode == 2) ?\n"
    "        m.a : dot(m.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
    "    if(mode == 2 || mode == 4) ma = 1.0 - ma;\n"
    "    float a = s.a * ma;\n"
    "    fragColor = vec4(s.rgb * a, a);\n"
    "}\n";

sk_sp<SkColorFilter> TrackMatteCaller::makeAlphaFilter(const Mode mode) {
    // rows: R G B A out, columns: r g b a 1 (only alpha output matters
    // for the following kDstIn draw)
    float m[20];
    for(int i = 0; i < 20; i++) m[i] = 0.f;
    switch(mode) {
    case Mode::alpha:
        return nullptr;
    case Mode::alphaInv:
        m[15] = -1.f; m[19] = 1.f;
        break;
    case Mode::luma:
        m[15] = 0.2126f; m[16] = 0.7152f; m[17] = 0.0722f;
        break;
    case Mode::lumaInv:
        m[15] = -0.2126f; m[16] = -0.7152f; m[17] = -0.0722f; m[19] = 1.f;
        break;
    default:
        return nullptr;
    }
    return SkColorFilters::Matrix(m);
}

TrackMatteCaller::TrackMatteCaller(stdsptr<BoxRenderData> matte,
                                   const Mode mode) :
    // cpuOnly: the GPU path would need the shader registered in the
    // resource system (gIniProgram reads a :/shaders/ path, not source
    // text); the renderer routes cpuOnly callers through processCpu
    RasterEffectCaller(HardwareSupport::cpuOnly, false),
    mMatte(std::move(matte)), mMode(mode) {}

void TrackMatteCaller::sInitialize(QGL33* const gl) {
    try {
        gIniProgram(gl, sProgramId, GL_TEXTURED_VERT, sTrackMatteFrag);
    } catch(...) {
        RuntimeThrow("Could not initialize the track matte program");
    }
    gl->glUseProgram(sProgramId);
    const GLint tex = gl->glGetUniformLocation(sProgramId, "tex");
    gl->glUniform1i(tex, 0);
    sMaskTexLoc = gl->glGetUniformLocation(sProgramId, "mask");
    gl->glUniform1i(sMaskTexLoc, 1);
    sModeLoc = gl->glGetUniformLocation(sProgramId, "mode");
    sRect2Loc = gl->glGetUniformLocation(sProgramId, "rect2");
}

void TrackMatteCaller::processGpu(QGL33* const gl,
                                  GpuRenderTools& renderTools) {
    if(!mMatte) return;
    const auto& matteImg = mMatte->fRenderedImage;
    if(!matteImg) return;
    renderTools.switchToOpenGL(gl);
    if(!sInitialized) {
        sInitialize(gl);
        sInitialized = true;
    }

    eTexture maskTex;
    renderTools.imageToTexture(matteImg, maskTex);

    renderTools.requestTargetFbo().bind(gl);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    gl->glUseProgram(sProgramId);
    gl->glActiveTexture(GL_TEXTURE0);
    renderTools.getSrcTexture().bind(gl);
    gl->glActiveTexture(GL_TEXTURE1);
    maskTex.bind(gl);

    const int modeI = static_cast<int>(mMode);
    gl->glUniform1i(sModeLoc, modeI);
    {
        const auto& mG = mMatte->fGlobalRect;
        const auto& dG = renderTools.fGlobalRect;
        const float mw = qMax(1, mG.width());
        const float mh = qMax(1, mG.height());
        const float left   = (dG.left()   - mG.left())/mw;
        const float top    = (dG.top()    - mG.top())/mh;
        const float right  = (dG.right()  - mG.left())/mw;
        const float bottom = (dG.bottom() - mG.top())/mh;
        gl->glUniform4f(sRect2Loc, left, top, right, bottom);
    }

    gl->glBindVertexArray(renderTools.getSquareVAO());
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    renderTools.swapTextures();
}

void TrackMatteCaller::processCpu(CpuRenderTools& renderTools,
                                  const CpuRenderData& data) {
    const bool inverted = (mMode == Mode::alphaInv || mMode == Mode::lumaInv);
    auto copySrcTile = [&renderTools, &data]() {
        SkBitmap srcTile;
        if(renderTools.fSrcBtmp.extractSubset(&srcTile, data.fTexTile)) {
            SkPixmap srcTilePix;
            if(srcTile.peekPixels(&srcTilePix)) {
                renderTools.fDstBtmp.writePixels(srcTilePix, 0, 0);
            }
        }
    };
    // no sample at all (preserve-alpha with nothing below) or an
    // empty/degenerate matte (e.g. a zero-area shape): AE hides the
    // target entirely, inverted modes show everything - never a
    // silent no-op
    if(!mMatte || !mMatte->fRenderedImage) {
        copySrcTile();
        if(!inverted) {
            SkCanvas clear(renderTools.fDstBtmp);
            SkPaint p;
            p.setBlendMode(SkBlendMode::kDstOut);
            p.setColor(SK_ColorWHITE); // a = 1 - dst.a
            clear.drawPaint(p);
        }
        return;
    }
    const auto& img = mMatte->fRenderedImage;
    const auto raster = img->makeRasterImage();
    if(!raster) return;
    SkPixmap mattePix;
    if(!raster->peekPixels(&mattePix)) return;

    // the dst tile starts as uninitialized memory - copy the source
    // tile in first, the kDstIn below multiplies THIS content
    copySrcTile();

    // matte origin in this layer's image coordinates
    const QPoint imgOff = mMatte->fGlobalRect.topLeft() - data.fPos;
    const SkIRect& tile = data.fTexTile;
    // matte coverage within the tile; everything outside the matte is
    // fully masked out (AE semantics)
    SkIRect ov = SkIRect::MakeXYWH(imgOff.x(), imgOff.y(),
                                   mattePix.width(), mattePix.height());
    if(!ov.intersect(tile)) {
        if(!inverted) {
            // no matte pixels here: clear the tile's alpha completely
            SkCanvas clear(renderTools.fDstBtmp);
            SkPaint p;
            p.setBlendMode(SkBlendMode::kDstOut);
            p.setColor(SK_ColorWHITE); // a = 1 - dst.a
            clear.drawPaint(p);
        }
        return;
    }
    // ov is in IMAGE coordinates; mattePix lives in its own local space
    // (its (0,0) is the matte origin) - shift before extracting, the
    // same conversion MotionBlurCaller applies to its sample tiles
    SkPixmap mattePart;
    mattePix.extractSubset(
                &mattePart, ov.makeOffset(-imgOff.x(), -imgOff.y()));
    const int drawX = ov.left()  - tile.left();
    const int drawY = ov.top()   - tile.top();

    // build an alpha-only tile: transparent outside the matte for normal modes,
    // opaque white (a=1) for inverted modes, and filtered matte alpha inside
    SkBitmap alphaBmp;
    alphaBmp.allocPixels(renderTools.fDstBtmp.info());
    alphaBmp.eraseColor(inverted ? SK_ColorWHITE : SK_ColorTRANSPARENT);
    SkCanvas ac(alphaBmp);
    SkPaint pf;
    pf.setBlendMode(SkBlendMode::kSrc);
    pf.setColorFilter(makeAlphaFilter(mMode));
    ac.drawImage(SkImage::MakeRasterCopy(mattePart),
                 drawX, drawY, &pf);

    // dst = src masked by the alpha tile
    SkCanvas dc(renderTools.fDstBtmp);
    SkPaint pd;
    pd.setBlendMode(SkBlendMode::kDstIn);
    dc.drawImage(SkImage::MakeFromBitmap(alphaBmp), 0, 0, &pd);
}
