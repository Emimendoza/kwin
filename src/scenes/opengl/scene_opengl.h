/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_SCENE_OPENGL_H
#define KWIN_SCENE_OPENGL_H

#include "openglbackend.h"

#include "decorationitem.h"
#include "scene.h"
#include "shadow.h"

#include "kwinglutils.h"

namespace KWin
{
class OpenGLBackend;

class KWIN_EXPORT SceneOpenGL
    : public Scene
{
    Q_OBJECT
public:
    struct RenderNode
    {
        GLTexture *texture = nullptr;
        WindowQuadList quads;
        QMatrix4x4 transformMatrix;
        int firstVertex = 0;
        int vertexCount = 0;
        qreal opacity = 1;
        bool hasAlpha = false;
        TextureCoordinateType coordinateType = UnnormalizedCoordinates;
    };

    struct RenderContext
    {
        QVector<RenderNode> renderNodes;
        QStack<QMatrix4x4> transforms;
        const QRegion clip;
        const WindowPaintData &paintData;
        const bool hardwareClipping;
    };

    class EffectFrame;
    explicit SceneOpenGL(OpenGLBackend *backend, QObject *parent = nullptr);
    ~SceneOpenGL() override;
    bool initFailed() const override;
    void paint(RenderTarget *renderTarget, const QRegion &region) override;
    Scene::EffectFrame *createEffectFrame(EffectFrameImpl *frame) override;
    Shadow *createShadow(Window *window) override;
    bool makeOpenGLContextCurrent() override;
    void doneOpenGLContextCurrent() override;
    bool supportsNativeFence() const override;
    DecorationRenderer *createDecorationRenderer(Decoration::DecoratedClientImpl *impl) override;
    bool animationsSupported() const override;
    SurfaceTexture *createSurfaceTextureInternal(SurfacePixmapInternal *pixmap) override;
    SurfaceTexture *createSurfaceTextureX11(SurfacePixmapX11 *pixmap) override;
    SurfaceTexture *createSurfaceTextureWayland(SurfacePixmapWayland *pixmap) override;
    void render(Item *item, int mask, const QRegion &region, const WindowPaintData &data) override;

    OpenGLBackend *backend() const
    {
        return m_backend;
    }

    QVector<QByteArray> openGLPlatformInterfaceExtensions() const override;
    QSharedPointer<GLTexture> textureForOutput(Output *output) const override;

    QMatrix4x4 screenProjectionMatrix() const override
    {
        return m_screenProjectionMatrix;
    }

    static SceneOpenGL *createScene(OpenGLBackend *backend, QObject *parent);
    static bool supported(OpenGLBackend *backend);

protected:
    void paintBackground(const QRegion &region) override;
    QMatrix4x4 transformation(int mask, const ScreenPaintData &data) const;
    void paintOffscreenQuickView(OffscreenQuickView *w) override;

    void paintSimpleScreen(int mask, const QRegion &region) override;
    void paintGenericScreen(int mask, const ScreenPaintData &data) override;

private:
    void doPaintBackground(const QVector<float> &vertices);
    QMatrix4x4 modelViewProjectionMatrix(int mask, const WindowPaintData &data) const;
    QVector4D modulate(float opacity, float brightness) const;
    void setBlendEnabled(bool enabled);
    void createRenderNode(Item *item, RenderContext *context);

    bool init_ok = true;
    OpenGLBackend *m_backend;
    QMatrix4x4 m_screenProjectionMatrix;
    GLuint vao = 0;
    bool m_blendingEnabled = false;
};

class SceneOpenGL::EffectFrame
    : public Scene::EffectFrame
{
public:
    EffectFrame(EffectFrameImpl *frame, SceneOpenGL *scene);
    ~EffectFrame() override;

    void free() override;
    void freeIconFrame() override;
    void freeTextFrame() override;
    void freeSelection() override;

    void render(const QRegion &region, double opacity, double frameOpacity) override;

    void crossFadeIcon() override;
    void crossFadeText() override;

    static void cleanup();

private:
    void updateTexture();
    void updateTextTexture();

    GLTexture *m_texture;
    GLTexture *m_textTexture;
    GLTexture *m_oldTextTexture;
    QPixmap *m_textPixmap; // need to keep the pixmap around to workaround some driver problems
    GLTexture *m_iconTexture;
    GLTexture *m_oldIconTexture;
    GLTexture *m_selectionTexture;
    GLVertexBuffer *m_unstyledVBO;
    SceneOpenGL *m_scene;

    static GLTexture *m_unstyledTexture;
    static QPixmap *m_unstyledPixmap; // need to keep the pixmap around to workaround some driver problems
    static void updateUnstyledTexture(); // Update OpenGL unstyled frame texture
};

/**
 * @short OpenGL implementation of Shadow.
 *
 * This class extends Shadow by the Elements required for OpenGL rendering.
 * @author Martin Gräßlin <mgraesslin@kde.org>
 */
class SceneOpenGLShadow
    : public Shadow
{
public:
    explicit SceneOpenGLShadow(Window *window);
    ~SceneOpenGLShadow() override;

    GLTexture *shadowTexture()
    {
        return m_texture.data();
    }

protected:
    bool prepareBackend() override;

private:
    QSharedPointer<GLTexture> m_texture;
};

class SceneOpenGLDecorationRenderer : public DecorationRenderer
{
    Q_OBJECT
public:
    enum class DecorationPart : int {
        Left,
        Top,
        Right,
        Bottom,
        Count
    };
    explicit SceneOpenGLDecorationRenderer(Decoration::DecoratedClientImpl *client);
    ~SceneOpenGLDecorationRenderer() override;

    void render(const QRegion &region) override;

    GLTexture *texture()
    {
        return m_texture.data();
    }
    GLTexture *texture() const
    {
        return m_texture.data();
    }

private:
    void renderPart(const QRect &rect, const QRect &partRect, const QPoint &textureOffset, qreal devicePixelRatio, bool rotated = false);
    static const QMargins texturePadForPart(const QRect &rect, const QRect &partRect);
    void resizeTexture();
    QScopedPointer<GLTexture> m_texture;
};

} // namespace

#endif
