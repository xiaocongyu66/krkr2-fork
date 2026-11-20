#include "KrkrTexture.h"

#include <cstring>

namespace krkr::render {

    KrkrTextureNameResolver *KrkrTexture::resolver_ = nullptr;

    void KrkrTexture::setNameResolver(KrkrTextureNameResolver *resolver) {
        resolver_ = resolver;
    }

    KrkrTexture::KrkrTexture() = default;

    KrkrTexture::~KrkrTexture() { release(); }

    void KrkrTexture::releaseEngine() {
        if(engineTex_) {
            // AdapterTexture2D semantics (ogl:1029-1048): Release hands the
            // texture to the engine's deferred delete queue
            // (iTVPTexture2D::Release, RenderManager.cpp:310-324).
            engineTex_->Release();
            engineTex_ = nullptr;
        }
        resolveDirty_ = true;
        resolvedName_ = 0;
    }

    void KrkrTexture::releaseOwned() {
        if(ownedName_ && !ownedStale_ && krkrGL().isBound()) {
            // GL-thread only: glDeleteTextures touches the current context.
            krkrGL().glDeleteTextures(1, &ownedName_);
        }
        ownedName_ = 0;
        ownedStale_ = false;
    }

    void KrkrTexture::ensureOwnedName() const {
        if(ownedName_ && !ownedStale_)
            return;
        ownedName_ = 0;
        ownedStale_ = false;
        if(!krkrGL().isBound())
            return;
        krkrGL().glGenTextures(1, &ownedName_);
        if(!ownedName_)
            return;
        // Engine texture defaults (tTVPOGLTexture2D ctor, ogl:892-895).
        krkrGL().glBindTexture(GL_TEXTURE_2D, ownedName_);
        krkrGL().glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                 GL_LINEAR);
        krkrGL().glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                 GL_LINEAR);
        krkrGL().glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                 GL_CLAMP_TO_EDGE);
        krkrGL().glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                 GL_CLAMP_TO_EDGE);
    }

    void KrkrTexture::update(iTVPTexture2D *tex) {
        releaseEngine();
        if(!tex)
            return;
        tex->AddRef();
        engineTex_ = tex;
        width_ = (int)tex->GetWidth();
        height_ = (int)tex->GetHeight();
        internalW_ = (int)tex->GetInternalWidth();
        internalH_ = (int)tex->GetInternalHeight();
        tex->GetScale(scaleX_, scaleY_);
        format_ = tex->GetFormat();
        // Drop managed content, if any: the engine texture owns the GL object
        // now.
        releaseOwned();
        resolveDirty_ = true;
        resolvedName_ = 0;
    }

    bool KrkrTexture::mapFormat(TVPTextureFormat::e format, GLFormat &out) {
        // ogl:928-945; compressed formats carry the GL internal format
        // themselves (og:40-57) and go through uploadCompressed.
        switch(format) {
            case TVPTextureFormat::Gray:
                out.internalFormat = GL_LUMINANCE;
                out.pixelFormat = GL_LUMINANCE;
                out.type = GL_UNSIGNED_BYTE;
                out.pixelSize = 1;
                return true;
            case TVPTextureFormat::RGB:
                out.internalFormat = GL_RGB;
                out.pixelFormat = GL_RGB;
                out.type = GL_UNSIGNED_BYTE;
                out.pixelSize = 3;
                return true;
            case TVPTextureFormat::RGBA:
                out.internalFormat = GL_RGBA;
                out.pixelFormat = GL_RGBA;
                out.type = GL_UNSIGNED_BYTE;
                out.pixelSize = 4;
                return true;
            default:
                return false;
        }
    }

    static void applyUnpackAlignment(KrkrGLFunctions &gl, int pitch) {
        // ogl:952-965: the engine derives UNPACK_ALIGNMENT from the source
        // pitch.
        switch(pitch & 7) {
            case 0:
                gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
                break;
            case 2:
                gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
                break;
            case 4:
                gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                break;
            case 6:
                gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
                break;
            default:
                gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                break;
        }
    }

    void KrkrTexture::upload(const void *data, int pitch, int width, int height,
                             TVPTextureFormat::e format) {
        if(!data || width <= 0 || height <= 0)
            return;
        GLFormat fmt;
        if(!mapFormat(format, fmt)) {
            // Compressed / unknown: use uploadCompressed.
            return;
        }
        if(!krkrGL().isBound())
            return;
        // Managed content replaces any adopted engine texture.
        releaseEngine();
        ensureOwnedName();
        if(!ownedName_)
            return;

        krkrGL().glBindTexture(GL_TEXTURE_2D, ownedName_);
        applyUnpackAlignment(krkrGL(), pitch);

        const void *src = data;
        unsigned char *rearranged = nullptr;
        if(pitch != width * fmt.pixelSize) {
            // Tighten rows: mirrors the engine's non-GL_EXT_unpack_subimage
            // path ( ogl:1000-1012 ) -- the real GL_UNPACK_ROW_LENGTH fast path
            // is engine-side policy; here we never assume it.
            int arrpitch = width * fmt.pixelSize;
            rearranged = new unsigned char[(size_t)arrpitch * height];
            unsigned char *dst = rearranged;
            const unsigned char *s = (const unsigned char *)data;
            for(int y = 0; y < height; ++y, dst += arrpitch, s += pitch)
                std::memcpy(dst, s, arrpitch);
            src = rearranged;
        }

        bool sizeChanged =
            width != internalW_ || height != internalH_ || format != format_;
        if(sizeChanged) {
            // Size re-create == initWithData gate (YUVSprite.cpp:114-148).
            krkrGL().glTexImage2D(GL_TEXTURE_2D, 0, fmt.internalFormat, width,
                                  height, 0, fmt.pixelFormat, fmt.type, src);
            internalW_ = width;
            internalH_ = height;
            format_ = format;
        } else {
            krkrGL().glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                                     fmt.pixelFormat, fmt.type, src);
        }
        delete[] rearranged;
    }

    void KrkrTexture::uploadCompressed(const void *data, int len,
                                       GLenum internalFormat, int width,
                                       int height) {
        if(!data || len <= 0 || width <= 0 || height <= 0)
            return;
        if(!krkrGL().isBound())
            return;
        releaseEngine();
        ensureOwnedName();
        if(!ownedName_)
            return;

        krkrGL().glBindTexture(GL_TEXTURE_2D, ownedName_);
        krkrGL().glCompressedTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width,
                                        height, 0, len, data);
        // Track the content metrics as the engine does (ogl:1612-1624).
        internalW_ = width;
        internalH_ = height;
        format_ = TVPTextureFormat::Compressed;
    }

    GLuint KrkrTexture::glName() const {
        if(engineTex_) {
            if(resolveDirty_) {
                resolvedName_ = resolver_ ? resolver_->resolve(engineTex_) : 0;
                resolveDirty_ = false;
            }
            return resolvedName_;
        }
        if(!ownedName_ || ownedStale_)
            ensureOwnedName();
        return ownedName_;
    }

    void KrkrTexture::release() {
        releaseEngine();
        releaseOwned();
        width_ = height_ = internalW_ = internalH_ = 0;
        scaleX_ = scaleY_ = 1.f;
        format_ = TVPTextureFormat::None;
    }

    void KrkrTexture::invalidate() {
        // No GL calls here: after context loss the old names belong to a dead
        // context.ogl:2968-2980 shows the engine only rebuilds *shaders* on
        // EVENT_RENDERER_RECREATED -- texture content is restored by redrawing.
        resolveDirty_ = true;
        resolvedName_ = 0;
        ownedStale_ = true; // forget; next upload/glName re-creates
    }

} // namespace krkr::render
