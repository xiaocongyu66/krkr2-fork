#include "KrkrYUVRenderer.h"

#include <cstdio>
#include <cstring>

namespace krkr::render {

    namespace {
        // BT.601 YUV->RGB, translated from YUVSprite.cpp:7-38. Vertex half is
        // the module convention (a_position -> 0, a_texCoord -> 1, bound by
        // compileProgram). v_fragmentColor is dropped: the sprite color is
        // white and video alpha is always 1 (gl_FragColor.a = 1.0).
        const char *kYuvVertexShader =
            "attribute vec2 a_position;"
            "attribute vec2 a_texCoord;"
            "varying vec2 v_texCoord;"
            "void main()"
            "{"
            "    gl_Position = vec4(a_position, 0.0, 1.0);"
            "    v_texCoord = a_texCoord;"
            "}";

        const char *kYuvFragmentShader =
            "#ifdef GL_ES"
            "precision mediump float;"
            "#endif"
            "varying vec2 v_texCoord;"
            "uniform sampler2D u_texY;"
            "uniform sampler2D u_texU;"
            "uniform sampler2D u_texV;"
            "void main()"
            "{"
            "    vec3 yuv;"
            "    yuv.x = texture2D(u_texY, v_texCoord).r - 0.0625;"
            "    yuv.y = texture2D(u_texU, v_texCoord).r - 0.5;"
            "    yuv.z = texture2D(u_texV, v_texCoord).r - 0.5;"
            "    vec3 rgb = mat3(1.164, 1.164, 1.164,"
            "                    0.0, -0.392, 2.017,"
            "                    1.596, -0.813, 0.0) * yuv;"
            "    gl_FragColor = vec4(rgb, 1.0);"
            "}";

        void uploadPlane(GLuint &name, int &nameW, int &nameH, const void *data,
                         int w, int h) {
            // YUVSprite.cpp:114-148 gate: (re)allocate on size change via
            // glTexImage2D, else update in place via glTexSubImage2D.
            // Single-byte pixels, so UNPACK_ALIGNMENT 1 handles any pitch.
            KrkrGLFunctions &gl = krkrGL();
            bool sizeChanged = !name || nameW != w || nameH != h;
            if(sizeChanged) {
                if(!name)
                    gl.glGenTextures(1, &name);
                gl.glBindTexture(GL_TEXTURE_2D, name);
                gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                   GL_LINEAR);
                gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                   GL_LINEAR);
                gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                   GL_CLAMP_TO_EDGE);
                gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                   GL_CLAMP_TO_EDGE);
                // GL_LUMINANCE matches the engine's single-channel mapping
                // ( Gray -> GL_LUMINANCE, RenderManager_ogl.cpp:928-935 ); the
                // shader samples .r, which the luminance swizzle provides.
                gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0,
                                GL_LUMINANCE, GL_UNSIGNED_BYTE, data);
                nameW = w;
                nameH = h;
            } else {
                gl.glBindTexture(GL_TEXTURE_2D, name);
                gl.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_LUMINANCE,
                                   GL_UNSIGNED_BYTE, data);
            }
        }
    } // namespace

    KrkrYUVRenderer::KrkrYUVRenderer() = default;

    KrkrYUVRenderer::~KrkrYUVRenderer() {
        if(!krkrGL().isBound())
            return;
        if(texY_)
            krkrGL().glDeleteTextures(1, &texY_);
        if(texU_)
            krkrGL().glDeleteTextures(1, &texU_);
        if(texV_)
            krkrGL().glDeleteTextures(1, &texV_);
        if(program_)
            krkrGL().glDeleteProgram(program_);
    }

    void KrkrYUVRenderer::updatePlanes(const void *Y, int YW, int YH,
                                       const void *U, const void *V, int UH,
                                       int VH) {
        if(!Y || !U || !V || YW <= 0 || YH <= 0)
            return;
        if(!krkrGL().isBound())
            return;
        // Chroma width is YW/2 (half-width chroma, KRMoviePlayer.cpp:328-330).
        int UW = YW / 2;
        int VW = YW / 2;
        uploadPlane(texY_, planeYW_, planeYH_, Y, YW, YH);
        uploadPlane(texU_, planeUW_, planeUH_, U, UW, UH);
        uploadPlane(texV_, planeVW_, planeVH_, V, VW, VH);
    }

    void KrkrYUVRenderer::setOutputSize(int width, int height) {
        outW_ = width;
        outH_ = height;
    }

    void KrkrYUVRenderer::ensureProgram() {
        if(program_)
            return;
        program_ = compileProgram(kYuvVertexShader, kYuvFragmentShader);
        if(!program_) {
            std::fprintf(stderr, "krkr::render: YUV shader build failed\n");
            return;
        }
        uniformY_ = krkrGL().glGetUniformLocation(program_, "u_texY");
        uniformU_ = krkrGL().glGetUniformLocation(program_, "u_texU");
        uniformV_ = krkrGL().glGetUniformLocation(program_, "u_texV");
    }

    void KrkrYUVRenderer::draw(const tTVPRect &bounds) {
        if(!visible_ || !krkrGL().isBound())
            return;
        if(!texY_ || !texU_ || !texV_)
            return;
        ensureProgram();
        if(!program_)
            return;

        KrkrGLFunctions &gl = krkrGL();

        // Bounds are top-left origin (engine tTVPRect); GL is bottom-left.
        // PresentPicture computes `top = parentHeight - rc.top`
        // (KRMoviePlayer.cpp:313-318); invert back here.
        int left = bounds.left;
        int width = bounds.get_width();
        int height = bounds.get_height();
        int glBottom = outH_ - bounds.bottom; // bottom edge in GL coords
        if(width <= 0 || height <= 0)
            return;

        float x0 = (float)left / (float)outW_ * 2.f - 1.f;
        float x1 = (float)(left + width) / (float)outW_ * 2.f - 1.f;
        float y0 = (float)glBottom / (float)outH_ * 2.f - 1.f;
        float y1 = (float)(glBottom + height) / (float)outH_ * 2.f - 1.f;
        const float positions[4][2] = {
            { x0, y0 },
            { x1, y0 },
            { x1, y1 },
            { x0, y1 },
        };
        const float texcoords[4][2] = {
            { 0.f, 0.f },
            { 1.f, 0.f },
            { 1.f, 1.f },
            { 0.f, 1.f },
        };

        gl.glDisable(GL_BLEND); // video is opaque (gl_FragColor.a = 1.0)
        gl.glUseProgram(program_);
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_2D, texY_);
        gl.glActiveTexture(GL_TEXTURE1);
        gl.glBindTexture(GL_TEXTURE_2D, texU_);
        gl.glActiveTexture(GL_TEXTURE2);
        gl.glBindTexture(GL_TEXTURE_2D, texV_);
        gl.glUniform1i(uniformY_, 0);
        gl.glUniform1i(uniformU_, 1);
        gl.glUniform1i(uniformV_, 2);

        gl.glEnableVertexAttribArray(0);
        gl.glEnableVertexAttribArray(1);
        gl.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, positions);
        gl.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, texcoords);
        gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        gl.glDisableVertexAttribArray(0);
        gl.glDisableVertexAttribArray(1);
    }

    void KrkrYUVRenderer::setVisible(bool visible) { visible_ = visible; }

    void KrkrYUVRenderer::invalidate() {
        // No GL calls -- names belong to the lost context; the next
        // updatePlanes() re-creates everything (content redraw, not GL-name
        // salvage: ogl only rebuilds shaders on EVENT_RENDERER_RECREATED,
        // ogl:2968-2980).
        texY_ = texU_ = texV_ = 0;
        planeYW_ = planeYH_ = planeUW_ = planeUH_ = planeVW_ = planeVH_ = 0;
        program_ = 0;
        uniformY_ = uniformU_ = uniformV_ = -1;
    }

} // namespace krkr::render
