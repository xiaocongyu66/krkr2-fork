#pragma once

//---------------------------------------------------------------------------
// B3: engine texture (iTVPTexture2D) adoption for the SDL3 presentation path.
//
// Replaces the cocos2d AdapterTexture2D half of UpdateDrawBuffer
// (TVPWindow.cpp:602-629): the engine renderer (visual/ogl) owns every GL
// texture object; KrkrTexture holds a reference to the engine texture, tracks
// its content metrics, and exposes the GL name for compositor draw.
//
// Two content sources:
//  * update(iTVPTexture2D*): adopt an engine texture. GL name comes from a
//    host-provided KrkrTextureNameResolver (the concrete ogl texture exposes
//    its GL name as a public field, RenderManager_ogl.cpp:879; reaching it
//    requires the concrete renderer type, which this module deliberately
//    does not include). Reference semantics mirror AdapterTexture2D
//    (ogl:1029-1048 -- AddRef on adoption, Release on swap-out, engine-side
//    deferred delete queue RenderManager.cpp:310-324).
//  * upload(...): raw CPU pixels into our own GL texture (software-renderer
//    textures, explicit uploads). Reallocates with glTexImage2D when the size
//    or format changes, updates in place with glTexSubImage2D otherwise --
//    the same size-change gate as YUVSprite.cpp:114-148.
//---------------------------------------------------------------------------

#include "KrkrRenderContext.h"
#include "RenderManager.h" // iTVPTexture2D, TVPTextureFormat

namespace krkr::render {

// Compressed texture internal formats understood by uploadCompressed(),
// mirroring the engine's constant set (RenderManager_ogl.cpp:40-57).
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif
#ifndef GL_COMPRESSED_RGB8_ETC2
#define GL_COMPRESSED_RGB8_ETC2 0x9274
#define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#define GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 0x9276
#endif
#ifndef GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG
#define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG 0x8C00
#define GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG 0x8C01
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG 0x8C02
#define GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG 0x8C03
#endif
#ifndef GL_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG
#define GL_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG 0x9137
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG 0x9138
#endif

    class KrkrTextureNameResolver {
    public:
        virtual ~KrkrTextureNameResolver() = default;
        // GL texture name for an engine texture (tTVPOGLTexture2D::texture,
        // RenderManager_ogl.cpp:879). Returns 0 when the texture has no GL
        // object. Implemented by the B3 wiring in environ/sdl3.
        virtual GLuint resolve(const iTVPTexture2D *tex) = 0;
    };

    class KrkrTexture {
    public:
        KrkrTexture();
        ~KrkrTexture();

        KrkrTexture(const KrkrTexture &) = delete;
        KrkrTexture &operator=(const KrkrTexture &) = delete;

        // Installed once at B3 init and immutable afterward; glName() is
        // called only on the GL thread.
        static void setNameResolver(KrkrTextureNameResolver *resolver);

        // Adopt an engine texture: AddRef (releasing any previously adopted
        // texture), capture width/height/internal size/scale/format. GL-thread
        // only when followed by drawing; adoption itself touches no GL state.
        void update(iTVPTexture2D *tex);

        // Upload raw pixels into the texture owned by this wrapper. Switches
        // out any adopted engine texture (this wrapper then owns the GL
        // object). Format must be Gray/RGB/RGBA (TVPTextureFormat); compressed
        // data uses uploadCompressed(). GL-thread only.
        void upload(const void *data, int pitch, int width, int height,
                    TVPTextureFormat::e format);

        // Upload a compressed image (ETC1/ETC2/PVRTC internal formats above).
        // Compressed content is redefined via glCompressedTexImage2D whenever
        // the size or internal format changes (in-place compressed sub-image
        // update is not required by the engine path -- static compressed
        // textures are immutable, RenderManager_ogl.cpp:1612-1624).
        void uploadCompressed(const void *data, int len, GLenum internalFormat,
                              int width, int height);

        // GL texture name: the engine-shared name for adopted textures
        // (resolved through the resolver), otherwise the owned name from
        // upload(). Returns 0 when neither is available. GL-thread only.
        GLuint glName() const;

        bool hasEngineTexture() const { return engineTex_ != nullptr; }
        bool hasContent() const {
            return engineTex_ != nullptr || ownedName_ != 0;
        }

        // Content metrics. `width()/height()` are the engine's logical content
        // size (LayerWidth/Height source in UpdateDrawBuffer):606-622 uses
        // GetWidth/GetHeight plus GetScale for NPOT-scaled content.
        int width() const { return width_; }
        int height() const { return height_; }
        int internalWidth() const { return internalW_; }
        int internalHeight() const { return internalH_; }
        void getScale(float &x, float &y) const {
            x = scaleX_;
            y = scaleY_;
        }
        TVPTextureFormat::e format() const { return format_; }

        // Release the engine texture reference and delete the owned GL name
        // (GL-thread only when it deletes GL objects). Invalidates all metrics.
        void release();

        // Mark the owned GL name stale without calling GL. Adopted texture GL
        // names are engine-managed; glName() resolves the current name each
        // call. After context loss, the engine redraws to restore content.
        void invalidate();

        // Uncompressed format mapping (ogl:928-945).
        struct GLFormat {
            GLenum internalFormat;
            GLenum pixelFormat;
            GLenum type;
            int pixelSize; // bytes per pixel
        };
        static bool mapFormat(TVPTextureFormat::e format, GLFormat &out);

    private:
        void releaseEngine();
        void releaseOwned();
        void ensureOwnedName() const;

        iTVPTexture2D *engineTex_ = nullptr;
        mutable GLuint ownedName_ = 0; // GL name we created (managed uploads)
        mutable bool ownedStale_ =
            false; // owned name marked stale by invalidate()
        int width_ = 0, height_ = 0;
        int internalW_ = 0, internalH_ = 0;
        float scaleX_ = 1.f, scaleY_ = 1.f;
        TVPTextureFormat::e format_ = TVPTextureFormat::None;

        static KrkrTextureNameResolver *resolver_;
    };

} // namespace krkr::render
