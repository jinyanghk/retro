#include "GL.h"
#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_map>
#include "shaders_generated.h"
#include "TextureCache.h"
#include "MapLoader.h"
#include "BVH.h"
#include "Log.h"

static GLuint g_brushProg = 0;
static GLuint g_brushVAO = 0;
static GLuint g_brushVBO = 0;
static GLuint g_gridVAO = 0;
static GLuint g_gridVBO = 0;
static GLuint g_lineVAO = 0;
static GLuint g_lineVBO = 0;
static GLuint g_lineColorProg = 0;
static GLuint g_faceHiProg = 0;
static GLuint g_faceHiVAO = 0;
static GLuint g_faceHiVBO = 0;

static GLuint g_fbo = 0;
static GLuint g_fboTex = 0;
static GLuint g_fboDepth = 0;
static int g_fboW = 1280, g_fboH = 800;

static GLuint g_bvhVAO = 0, g_bvhVBO = 0, g_bvhEBO = 0;
static uint32_t g_bvhIndexCount = 0;

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "[SHADER] %s\n", log);
    }
    return s;
}

static GLuint LinkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

static GLuint EnsureLineColorProg() {
    if (g_lineColorProg != 0) return g_lineColorProg;
    GLuint v = CompileShader(GL_VERTEX_SHADER, Shaders::editor_line_vert);
    GLuint f = CompileShader(GL_FRAGMENT_SHADER, Shaders::editor_line_frag);
    g_lineColorProg = LinkProgram(v, f);
    return g_lineColorProg;
}

static void EnsureFaceHighlightGL() {
    if (g_faceHiProg != 0) return;
    GLuint v = CompileShader(GL_VERTEX_SHADER, Shaders::editor_face_hi_vert);
    GLuint f = CompileShader(GL_FRAGMENT_SHADER, Shaders::editor_face_hi_frag);
    g_faceHiProg = LinkProgram(v, f);
    glGenVertexArrays(1, &g_faceHiVAO);
    glGenBuffers(1, &g_faceHiVBO);
}

void ResizeFBO(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (w == g_fboW && h == g_fboH && g_fbo != 0) return;
    g_fboW = w; g_fboH = h;
    if (g_fbo == 0) {
        glGenFramebuffers(1, &g_fbo);
        glGenTextures(1, &g_fboTex);
        glGenRenderbuffers(1, &g_fboDepth);
    }
    glBindTexture(GL_TEXTURE_2D, g_fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, g_fboDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fboTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_fboDepth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned int GetFBOTexture() { return g_fboTex; }

void BindFBO() {
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
}

void InitGL() {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, Shaders::editor_brush_vert);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, Shaders::editor_brush_frag);
    g_brushProg = LinkProgram(vs, fs);
    glGenVertexArrays(1, &g_brushVAO); glGenBuffers(1, &g_brushVBO);
    glGenVertexArrays(1, &g_lineVAO); glGenBuffers(1, &g_lineVBO);
    glGenVertexArrays(1, &g_gridVAO); glGenBuffers(1, &g_gridVBO);

    std::vector<float> gridVerts;
    for (float x = -4096; x <= 4096; x += 64) {
        gridVerts.insert(gridVerts.end(), {x, 0, -4096, x, 0, 4096});
    }
    for (float z = -4096; z <= 4096; z += 64) {
        gridVerts.insert(gridVerts.end(), {-4096, 0, z, 4096, 0, z});
    }
    glBindVertexArray(g_gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_gridVBO);
    glBufferData(GL_ARRAY_BUFFER, gridVerts.size() * sizeof(float), gridVerts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    ResizeFBO(g_fboW, g_fboH);
    TextureCache::Init();
}

void CleanupGL() {
    if (g_bvhVBO) glDeleteBuffers(1, &g_bvhVBO);
    if (g_bvhEBO) glDeleteBuffers(1, &g_bvhEBO);
    if (g_bvhVAO) glDeleteVertexArrays(1, &g_bvhVAO);
    TextureCache::Shutdown();
}

void DrawGrid(const glm::mat4& viewProj) {
    static GLuint gridProg = 0;
    if (gridProg == 0) {
        GLuint v = CompileShader(GL_VERTEX_SHADER, Shaders::editor_grid_vert);
        GLuint f = CompileShader(GL_FRAGMENT_SHADER, Shaders::editor_grid_frag);
        gridProg = LinkProgram(v, f);
    }
    glUseProgram(gridProg);
    glUniformMatrix4fv(glGetUniformLocation(gridProg, "uVP"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(g_gridVAO);
    glDrawArrays(GL_LINES, 0, (GLsizei)((4096*2/64 + 1) * 2 * 2));
    glBindVertexArray(0);
}

void DrawBrushes(const glm::mat4& viewProj) {
    if (!g_bShowBrushes) return;
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    glUseProgram(g_brushProg);
    glUniform1i(glGetUniformLocation(g_brushProg, "uUseTexture"), g_bRenderTexture ? 1 : 0);
    GLint uMVP = glGetUniformLocation(g_brushProg, "uMVP");
    glUniform1i(glGetUniformLocation(g_brushProg, "uTex"), 0);

    std::unordered_map<std::string, std::vector<float>> matVertices;
    for (size_t i = 0; i < g_brushes.size(); i++) {
        if (i < g_brushVisible.size() && g_brushVisible[i] == 0) continue;
        const auto& b = g_brushes[i];
        if (b.m_vertices.size() < 8 || b.m_faces.size() < 6) continue;
        for (int f = 0; f < 6; f++) {
            const auto& face = b.m_faces[f];
            std::string mat = face.m_szMaterial.empty() ? b.m_szMaterial : face.m_szMaterial;
            if (mat.empty()) mat = "DEV/DEV_MEASUREGENERIC01";
            auto& buf = matVertices[mat];
            auto push = [&](int idx) {
                const glm::vec3& p = b.m_vertices[idx];
                buf.insert(buf.end(), { p.x, p.y, p.z, face.m_vecNormal.x, face.m_vecNormal.y, face.m_vecNormal.z, 0.0f, 0.0f });
            };
            push(face.m_nVertexIndices[0]); push(face.m_nVertexIndices[1]); push(face.m_nVertexIndices[2]);
            push(face.m_nVertexIndices[0]); push(face.m_nVertexIndices[2]); push(face.m_nVertexIndices[3]);
        }
    }

    glBindVertexArray(g_brushVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_brushVBO);
    for (auto& kv : matVertices) {
        if (kv.second.empty()) continue;
        glBufferData(GL_ARRAY_BUFFER, kv.second.size()*sizeof(float), kv.second.data(), GL_STREAM_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(6*sizeof(float)));
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(viewProj));
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, TextureCache::GetTexture(kv.first));
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(kv.second.size()/8));
    }
    glBindVertexArray(0);
}

void DrawBrushWireframe(const glm::mat4& viewProj) {
    if (!g_bShowBrushes) return;
    glUseProgram(g_brushProg);
    glUniformMatrix4fv(glGetUniformLocation(g_brushProg, "uMVP"), 1, GL_FALSE, glm::value_ptr(viewProj));
    for (const auto& b : g_brushes) {
        if (b.m_vertices.size() < 8) continue;
        std::vector<float> lines;
        for (int e = 0; e < 12; e++) {
            auto& p0 = b.m_vertices[kEdgeVertexIndices[e][0]];
            auto& p1 = b.m_vertices[kEdgeVertexIndices[e][1]];
            lines.insert(lines.end(), {p0.x,p0.y,p0.z, 0,0,1, 0,0, p1.x,p1.y,p1.z, 0,0,1, 0,0});
        }
        glBindVertexArray(g_lineVAO); glBindBuffer(GL_ARRAY_BUFFER, g_lineVBO);
        glBufferData(GL_ARRAY_BUFFER, lines.size()*sizeof(float), lines.data(), GL_STREAM_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(6*sizeof(float)));
        glDrawArrays(GL_LINES, 0, (GLsizei)(lines.size()/8));
    }
    glBindVertexArray(0);
}

void DrawEntityBBoxes(const glm::mat4& viewProj) {
    if (!g_bShowEntities) return;
    GLuint prog = EnsureLineColorProg();
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "uVP"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(g_lineVAO); glBindBuffer(GL_ARRAY_BUFFER, g_lineVBO);

    for (size_t i = 0; i < g_entities.size(); i++) {
        if (i < g_entityVisible.size() && g_entityVisible[i] == 0) continue;
        const auto& e = g_entities[i];
        glm::vec3 mn = e.m_vecBBoxMins, mx = e.m_vecBBoxMaxs;
        bool isTrigger = (e.m_szName.find("trigger") != std::string::npos);
        if (!isTrigger && glm::length(mx - mn) < 16.0f) { mn -= glm::vec3(8); mx += glm::vec3(8); }
        if (isTrigger) {
            glm::vec3 center = (mn + mx) * 0.5f, size = mx - mn;
            if (size.x < 4.0f) { mn.x = center.x - 2.0f; mx.x = center.x + 2.0f; }
            if (size.y < 4.0f) { mn.y = center.y - 2.0f; mx.y = center.y + 2.0f; }
            if (size.z < 4.0f) { mn.z = center.z - 2.0f; mx.z = center.z + 2.0f; }
        }
        glm::vec3 corners[8] = {
            {mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},{mx.x,mx.y,mn.z},{mn.x,mx.y,mn.z},
            {mn.x,mn.y,mx.z},{mx.x,mn.y,mx.z},{mx.x,mx.y,mx.z},{mn.x,mx.y,mx.z},
        };
        int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        bool sel = ((int)i == g_iSelectedEntity);
        glm::vec3 col = sel ? glm::vec3(1, 1, 0) : (isTrigger ? glm::vec3(1, 0.2f, 0.2f) : glm::vec3(0.4f, 1, 0.4f));
        std::vector<float> buf;
        for (auto& ed : edges) {
            auto& p0 = corners[ed[0]]; auto& p1 = corners[ed[1]];
            buf.insert(buf.end(), { p0.x,p0.y,p0.z, col.x,col.y,col.z, p1.x,p1.y,p1.z, col.x,col.y,col.z });
        }
        glBufferData(GL_ARRAY_BUFFER, buf.size()*sizeof(float), buf.data(), GL_STREAM_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
        glDrawArrays(GL_LINES, 0, (GLsizei)(buf.size()/6));
    }
    glBindVertexArray(0);
}

void DrawColorLines(const std::vector<float>& buf, const glm::mat4& viewProj) {
    if (buf.empty()) return;
    GLuint prog = EnsureLineColorProg();
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "uVP"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(g_lineVAO); glBindBuffer(GL_ARRAY_BUFFER, g_lineVBO);
    glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glDrawArrays(GL_LINES, 0, (GLsizei)(buf.size() / 6));
    glBindVertexArray(0);
}

void DrawFaceHighlight(const glm::vec3* quad, const glm::mat4& viewProj) {
    EnsureFaceHighlightGL();
    float buf[4 * 3];
    for (int k = 0; k < 4; k++) { buf[k*3+0]=quad[k].x; buf[k*3+1]=quad[k].y; buf[k*3+2]=quad[k].z; }
    glUseProgram(g_faceHiProg);
    glUniformMatrix4fv(glGetUniformLocation(g_faceHiProg, "uVP"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform4f(glGetUniformLocation(g_faceHiProg, "uColor"), 1.0f, 1.0f, 0.0f, 0.5f);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -1.0f);
    glBindVertexArray(g_faceHiVAO); glBindBuffer(GL_ARRAY_BUFFER, g_faceHiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(buf), buf, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL); glDisable(GL_BLEND); glDepthFunc(GL_LESS);
}

void DrawVertexPoints(const std::vector<std::pair<glm::vec3, glm::vec3>>& pts, const glm::mat4& viewProj) {
    if (pts.empty()) return;
    GLuint prog = EnsureLineColorProg();
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "uVP"), 1, GL_FALSE, glm::value_ptr(viewProj));
    std::vector<float> buf;
    for (auto& p : pts) {
        buf.insert(buf.end(), {p.first.x, p.first.y, p.first.z, p.second.x, p.second.y, p.second.z});
    }
    glBindVertexArray(g_lineVAO); glBindBuffer(GL_ARRAY_BUFFER, g_lineVBO);
    glBufferData(GL_ARRAY_BUFFER, buf.size()*sizeof(float), buf.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glEnable(GL_PROGRAM_POINT_SIZE); glPointSize(10.0f);
    glDisable(GL_DEPTH_TEST); glDrawArrays(GL_POINTS, 0, (GLsizei)pts.size()); glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

void DrawRawPrimitives(const std::vector<float>& buf, unsigned int mode, float pointSize, float lineWidth, const glm::mat4& viewProj) {
    if (buf.empty()) return;
    GLuint prog = EnsureLineColorProg();
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "uVP"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(g_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_lineVBO);
    glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    
    glLineWidth(lineWidth);
    if (mode == GL_POINTS) {
        glEnable(GL_PROGRAM_POINT_SIZE);
        glPointSize(pointSize);
    }
    
    glDisable(GL_DEPTH_TEST); // 穿透
    glDrawArrays(mode, 0, (GLsizei)(buf.size() / 6));
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glLineWidth(1.0f);
}

void UploadBvhToGL(const MapData& data) {
    if (g_bvhVAO == 0) {
        glGenVertexArrays(1, &g_bvhVAO); glGenBuffers(1, &g_bvhVBO); glGenBuffers(1, &g_bvhEBO);
    }
    glBindVertexArray(g_bvhVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_bvhVBO);
    glBufferData(GL_ARRAY_BUFFER, data.verts.size() * 4, data.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_bvhEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * 4, data.indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(3 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(9 * sizeof(float)));
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    g_bvhIndexCount = (uint32_t)data.indices.size();
}

void DrawLoadedBvh(const glm::mat4& viewProj) {
    if (g_bvhVAO == 0 || g_bvhIndexCount == 0) return;
    glUseProgram(g_brushProg);
    glUniform1i(glGetUniformLocation(g_brushProg, "uUseTexture"), 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
    glUniformMatrix4fv(glGetUniformLocation(g_brushProg, "uMVP"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(g_bvhVAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)g_bvhIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}