#include <GL/glew.h>
#include <SDL.h>
#include <SDL_opengl.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"

#include "math_compat.h"
#include "SceneGlobals.h"
#include "Entity.h"
#include "UndoStack.h"
#include "EditorLayout.h"
#include "GameDatabase.h"
#include "EditorPanels.h"
#include "GraphEditorPanel.h"
#include "View2D.h"
#include "MapIO.h"
#include "version.h"
#include "Log.h"
#include "FileSystem.h"
#include "MapLoader.h"
#include "TextureCache.h" // 👈 引入
#include "shaders_generated.h"
#include "BVH.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

// ----temporary only ----
#include <filesystem>
#include <unordered_map>
#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

// 👈 全局材质名列表（带路径，比如 "nature/red_grass"）
std::vector<std::string> g_allMaterials;

// 👈 材质名 → 完整文件路径（用于 TextureCache 加载）
std::unordered_map<std::string, std::string> g_materialPathMap;

// 👈 材质名 → GLuint 缩略图缓存
std::unordered_map<std::string, GLuint> g_matThumbCache;

static void ScanMaterialsRecursive(const std::string& rootDir) {
    g_allMaterials.clear();
    g_materialPathMap.clear();
    g_matThumbCache.clear();

    if (!fs::exists(rootDir)) {
        printf("[Editor] Materials directory '%s' not found!\n", rootDir.c_str());
        return;
    }

    int count = 0;
    for (auto& p : fs::recursive_directory_iterator(rootDir)) {
        if (!p.is_regular_file()) continue;
        if (p.path().extension() != ".png") continue;
        if (p.path().filename().string() == "normal_map.png") continue; // 跳过法线贴图之类

        // 1. 计算相对路径（比如 "nature/red_grass"）
        std::string relative = fs::relative(p.path(), rootDir).generic_string();
        relative = relative.substr(0, relative.size() - 4); // 去掉 ".png"

        // 2. 存进全局列表
        g_allMaterials.push_back(relative);
        // 3. 存完整路径（用于后续加载）
        g_materialPathMap[relative] = p.path().generic_string();
        count++;
    }

    // 4. 按字母排序，方便查找
    std::sort(g_allMaterials.begin(), g_allMaterials.end());

    printf("[Editor] Scanned %d materials from %s\n", count, rootDir.c_str());
}

// 👈 根据材质名获取纹理 ID（带缓存）
GLuint GetMaterialThumb(const std::string& matName) {
    auto it = g_matThumbCache.find(matName);
    if (it != g_matThumbCache.end()) return it->second;

    // 优先用完整路径加载，不行就交给 TextureCache
    GLuint texId = 0;
    auto pathIt = g_materialPathMap.find(matName);
    if (pathIt != g_materialPathMap.end()) {
        texId = TextureCache::GetTextureByPath(pathIt->second);
    }
    if (!texId) {
        // 兜底：直接交给 TextureCache
        texId = TextureCache::GetTexture(matName);
    }
    g_matThumbCache[matName] = texId;
    return texId;
}
// -----------------------

// ---------------------------------------------------------------------------
// 相机 (Y-up)
// ---------------------------------------------------------------------------
struct Camera {
    glm::vec3 target = glm::vec3(0, 100, 0); // 目标看向房间中心
    float yaw   = 0.0f;
    float pitch = 0.0f;
    float dist  = 200.0f;

    glm::vec3 GetEye() const {
        float rp = glm::radians(pitch);
        float ry = glm::radians(yaw);
        glm::vec3 off(std::cos(rp)*std::cos(ry), std::sin(rp), std::cos(rp)*std::sin(ry));
        off *= dist;
        return target + off;
    }
    glm::mat4 GetView() const {
        return glm::lookAt(GetEye(), target, glm::vec3(0,1,0)); // Y-up
    }
};

// ---------------------------------------------------------------------------
// 全局状态
// ---------------------------------------------------------------------------
static SDL_Window*   g_window    = nullptr;
static SDL_GLContext g_glContext = nullptr;
static int           g_winW      = 1280;
static int           g_winH      = 800;
static float         main_scale  = 1.0f;

static GLuint        g_brushProg = 0;
static GLuint        g_brushVAO  = 0;
static GLuint        g_brushVBO  = 0;
static GLuint        g_gridVAO   = 0;
static GLuint        g_gridVBO   = 0;
static GLuint        g_lineVAO   = 0;
static GLuint        g_lineVBO   = 0;

// FBO
static GLuint        g_fbo       = 0;
static GLuint        g_fboTex    = 0;
static GLuint        g_fboDepth  = 0;
static int           g_fboW      = 1280;
static int           g_fboH      = 800;

static Camera        g_cam;
static bool          g_bLeftMouseDown  = false;
static bool          g_bRightMouseDown = false;
static bool          g_bMiddleMouseDown= false;
static bool          g_bDraggingGizmo  = false;
static int           g_iMouseX = 0, g_iMouseY = 0;
static int           g_iLastMouseX = 0, g_iLastMouseY = 0;

static glm::mat4     g_proxyMatrixPrev = glm::mat4(1.0f);
static int           g_iActiveSubIndex = -1;

// ---------------------------------------------------------------------------
// 通用彩色 line shader（bbox / edge 高亮复用）
// ---------------------------------------------------------------------------
static GLuint g_lineColorProg = 0;

static GLuint g_faceHiProg  = 0;
static GLuint g_faceHiVAO   = 0;
static GLuint g_faceHiVBO   = 0;

// from libgame
static MapData g_loadedMapData;
static bool g_bLoadedBvh = false;
static GLuint g_bvhVAO = 0, g_bvhVBO = 0, g_bvhEBO = 0;

// ---------------------------------------------------------------------------
// GL 辅助与 FBO
// ---------------------------------------------------------------------------
static GLuint CompileShader(GLenum type, const char* src)
{
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
static GLuint LinkProgram(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

static GLuint EnsureLineColorProg()
{
    if (g_lineColorProg != 0) return g_lineColorProg;
    GLuint v = CompileShader(GL_VERTEX_SHADER, Shaders::editor_line_vert);
    GLuint f = CompileShader(GL_FRAGMENT_SHADER, Shaders::editor_line_frag);
    g_lineColorProg = LinkProgram(v, f);
    return g_lineColorProg;
}

static void EnsureFaceHighlightGL()
{
    if (g_faceHiProg != 0) return;
    GLuint v = CompileShader(GL_VERTEX_SHADER, Shaders::editor_face_hi_vert);
    GLuint f = CompileShader(GL_FRAGMENT_SHADER, Shaders::editor_face_hi_frag);
    g_faceHiProg = LinkProgram(v, f);

    glGenVertexArrays(1, &g_faceHiVAO);
    glGenBuffers(1, &g_faceHiVBO);
}

static void ResizeFBO(int w, int h)
{
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

static void InitGL()
{
    GLuint vs = CompileShader(GL_VERTEX_SHADER,   Shaders::editor_brush_vert);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, Shaders::editor_brush_frag);
    g_brushProg = LinkProgram(vs, fs);

    glGenVertexArrays(1, &g_brushVAO);
    glGenBuffers(1, &g_brushVBO);
    glGenVertexArrays(1, &g_lineVAO);
    glGenBuffers(1, &g_lineVBO);
    glGenVertexArrays(1, &g_gridVAO);
    glGenBuffers(1, &g_gridVBO);

    // 网格在 Y-up 下画在 XZ 平面
    std::vector<float> gridVerts;
    for (float x = -4096; x <= 4096; x += 64) {
        gridVerts.push_back(x); gridVerts.push_back(0); gridVerts.push_back(-4096);
        gridVerts.push_back(x); gridVerts.push_back(0); gridVerts.push_back( 4096);
    }
    for (float z = -4096; z <= 4096; z += 64) {
        gridVerts.push_back(-4096); gridVerts.push_back(0); gridVerts.push_back(z);
        gridVerts.push_back( 4096); gridVerts.push_back(0); gridVerts.push_back(z);
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

// ---------------------------------------------------------------------------
// 3D 绘制辅助
// ---------------------------------------------------------------------------
static void DrawGrid(const glm::mat4& viewProj)
{
    static GLuint gridProg = 0;
    if (gridProg == 0) {
        GLuint v = CompileShader(GL_VERTEX_SHADER, Shaders::editor_grid_vert);
        GLuint f = CompileShader(GL_FRAGMENT_SHADER, Shaders::editor_grid_frag);
        gridProg = LinkProgram(v, f);
    }
    glUseProgram(gridProg);
    GLint loc = glGetUniformLocation(gridProg, "uVP");
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(g_gridVAO);
    glDrawArrays(GL_LINES, 0, (GLsizei)((4096*2/64 + 1) * 2 * 2));
    glBindVertexArray(0);
}

static void DrawBrushes(const glm::mat4& viewProj)
{
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    glUseProgram(g_brushProg);
    GLint uMVP = glGetUniformLocation(g_brushProg, "uMVP");
    GLint uTex = glGetUniformLocation(g_brushProg, "uTex");
    glUniform1i(uTex, 0);

    // 1. 按材质分组
    std::unordered_map<std::string, std::vector<float>> matVertices;

    for (const auto& b : g_brushes) {
        if (b.m_vertices.size() < 8 || b.m_faces.size() < 6) continue;
        
        for (int f = 0; f < 6; f++) {
            const auto& face = b.m_faces[f];
            int i0 = face.m_nVertexIndices[0];
            int i1 = face.m_nVertexIndices[1];
            int i2 = face.m_nVertexIndices[2];
            int i3 = face.m_nVertexIndices[3];
            
            // 优先用面材质，否则用 Brush 材质
            std::string mat = face.m_szMaterial.empty() ? b.m_szMaterial : face.m_szMaterial;
            if (mat.empty()) mat = "DEV/DEV_MEASUREGENERIC01";

            auto& buf = matVertices[mat];
            auto push = [&](int idx) {
                const glm::vec3& p = b.m_vertices[idx];
                buf.insert(buf.end(), { 
                    p.x, p.y, p.z, 
                    face.m_vecNormal.x, face.m_vecNormal.y, face.m_vecNormal.z,
                    0.0f, 0.0f // UV 留空，靠 Shader 三平面投影
                });
            };
            push(i0); push(i1); push(i2);
            push(i0); push(i2); push(i3);
        }
    }

    // 2. 逐材质绘制
    glBindVertexArray(g_brushVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_brushVBO);
    
    for (auto& kv : matVertices) {
        const std::string& matName = kv.first;
        const std::vector<float>& vbuf = kv.second;
        if (vbuf.empty()) continue;

        glBufferData(GL_ARRAY_BUFFER, vbuf.size()*sizeof(float), vbuf.data(), GL_STREAM_DRAW);
        
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(6*sizeof(float)));

        glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(viewProj));
        
        // 绑定贴图（用 TextureCache 获取）
        GLuint texId = TextureCache::GetTexture(matName);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texId);

        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(vbuf.size()/8));
    }
    glBindVertexArray(0);
}

static void DrawBrushWireframe(const glm::mat4& viewProj)
{
    glUseProgram(g_brushProg);
    GLint uMVP = glGetUniformLocation(g_brushProg, "uMVP");
    glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(viewProj));

    for (const auto& b : g_brushes) {
        if (b.m_vertices.size() < 8) continue;
        std::vector<float> lines;
        for (int e = 0; e < 12; e++) {
            auto& p0 = b.m_vertices[kEdgeVertexIndices[e][0]];
            auto& p1 = b.m_vertices[kEdgeVertexIndices[e][1]];
            lines.insert(lines.end(), {p0.x,p0.y,p0.z, 0,0,1, 0,0, p1.x,p1.y,p1.z, 0,0,1, 0,0});
        }
        glBindVertexArray(g_lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, g_lineVBO);
        glBufferData(GL_ARRAY_BUFFER, lines.size()*sizeof(float), lines.data(), GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(6*sizeof(float)));
        glDrawArrays(GL_LINES, 0, (GLsizei)(lines.size()/8));
    }
    glBindVertexArray(0);
}

static void DrawEntityBBoxes(const glm::mat4& viewProj)
{
    glBindVertexArray(g_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_lineVBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glDisableVertexAttribArray(2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    GLuint prog = EnsureLineColorProg();
    glUseProgram(prog);
    GLint uVP = glGetUniformLocation(prog, "uVP");
    glUniformMatrix4fv(uVP, 1, GL_FALSE, glm::value_ptr(viewProj));

    for (size_t i = 0; i < g_entities.size(); i++) {
        const auto& e = g_entities[i];
        
        glm::vec3 mn = e.m_vecBBoxMins;
        glm::vec3 mx = e.m_vecBBoxMaxs;
        
        bool isTrigger = (e.m_szName.find("trigger") != std::string::npos);
        
        // 普通实体如果太小，放大一点方便看
        if (!isTrigger && glm::length(mx - mn) < 16.0f) {
            mn -= glm::vec3(8);
            mx += glm::vec3(8);
        }
        
        // ★ 确保 Trigger 也有一个最小可见尺寸，防止缩到 0 时看不见
        if (isTrigger) {
            glm::vec3 center = (mn + mx) * 0.5f;
            glm::vec3 size = mx - mn;
            if (size.x < 4.0f) { mn.x = center.x - 2.0f; mx.x = center.x + 2.0f; }
            if (size.y < 4.0f) { mn.y = center.y - 2.0f; mx.y = center.y + 2.0f; }
            if (size.z < 4.0f) { mn.z = center.z - 2.0f; mx.z = center.z + 2.0f; }
        }
        
        glm::vec3 corners[8] = {
            {mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},{mx.x,mx.y,mn.z},{mn.x,mx.y,mn.z},
            {mn.x,mn.y,mx.z},{mx.x,mn.y,mx.z},{mx.x,mx.y,mx.z},{mn.x,mx.y,mx.z},
        };
        int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0},
            {4,5},{5,6},{6,7},{7,4},
            {0,4},{1,5},{2,6},{3,7},
        };
        
        bool sel = ((int)i == g_iSelectedEntity);
        
        glm::vec3 col;
        if (sel) {
            col = glm::vec3(1, 1, 0); 
        } else if (isTrigger) {
            col = glm::vec3(1, 0.2f, 0.2f); // 红色
        } else {
            col = glm::vec3(0.4f, 1, 0.4f);
        }

        std::vector<float> buf;
        for (auto& ed : edges) {
            auto& p0 = corners[ed[0]];
            auto& p1 = corners[ed[1]];
            buf.insert(buf.end(), { p0.x,p0.y,p0.z, col.x,col.y,col.z });
            buf.insert(buf.end(), { p1.x,p1.y,p1.z, col.x,col.y,col.z });
        }
        
        glBindVertexArray(g_lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, g_lineVBO);
        glBufferData(GL_ARRAY_BUFFER, buf.size()*sizeof(float), buf.data(), GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
        glDrawArrays(GL_LINES, 0, (GLsizei)(buf.size()/6));
    }
    glBindVertexArray(0);
}

// 用 pos3+color3 布局绘制 GL_LINES，复用 g_lineVAO/g_lineVBO
static void DrawColorLines(const std::vector<float>& buf, const glm::mat4& viewProj)
{
    if (buf.empty()) return;
    GLuint prog = EnsureLineColorProg();
    glUseProgram(prog);
    GLint uVP = glGetUniformLocation(prog, "uVP");
    glUniformMatrix4fv(uVP, 1, GL_FALSE, glm::value_ptr(viewProj));

    glBindVertexArray(g_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_lineVBO);
    glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glDisableVertexAttribArray(2);
    glDrawArrays(GL_LINES, 0, (GLsizei)(buf.size() / 6));
    glBindVertexArray(0);
}

static void DrawFaceHighlight(const glm::vec3* quad, const glm::mat4& viewProj)
{
    EnsureFaceHighlightGL();

    float buf[4 * 3];
    for (int k = 0; k < 4; k++) {
        buf[k*3+0] = quad[k].x;
        buf[k*3+1] = quad[k].y;
        buf[k*3+2] = quad[k].z;
    }

    glUseProgram(g_faceHiProg);
    GLint uVP = glGetUniformLocation(g_faceHiProg, "uVP");
    glUniformMatrix4fv(uVP, 1, GL_FALSE, glm::value_ptr(viewProj));
    GLint uColor = glGetUniformLocation(g_faceHiProg, "uColor");
    glUniform4f(uColor, 1.0f, 1.0f, 0.0f, 0.5f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);          // 让高亮和 brush 同深度也能通过
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);

    glBindVertexArray(g_faceHiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_faceHiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(buf), buf, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
    glDepthFunc(GL_LESS);
}

// 用 GL_POINTS 画顶点指示器（core profile 合法）
static void DrawVertexPoints(const std::vector<std::pair<glm::vec3, glm::vec3>>& pts,
                             const glm::mat4& viewProj)
{
    if (pts.empty()) return;
    GLuint prog = EnsureLineColorProg();
    glUseProgram(prog);
    GLint uVP = glGetUniformLocation(prog, "uVP");
    glUniformMatrix4fv(uVP, 1, GL_FALSE, glm::value_ptr(viewProj));

    // pos3 + color3
    std::vector<float> buf;
    buf.reserve(pts.size() * 6);
    for (auto& p : pts) {
        buf.push_back(p.first.x);  buf.push_back(p.first.y);  buf.push_back(p.first.z);
        buf.push_back(p.second.x); buf.push_back(p.second.y); buf.push_back(p.second.z);
    }

    glBindVertexArray(g_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_lineVBO);
    glBufferData(GL_ARRAY_BUFFER, buf.size()*sizeof(float), buf.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glDisableVertexAttribArray(2);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glPointSize(10.0f);
    glDisable(GL_DEPTH_TEST);        // x-ray：所有顶点都可见
    glDrawArrays(GL_POINTS, 0, (GLsizei)pts.size());
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// 拾取
// ---------------------------------------------------------------------------
static bool ScreenPointInAABBWorld(const glm::vec3& worldMin, const glm::vec3& worldMax,
                                   const glm::mat4& view, const glm::mat4& proj,
                                   int w, int h, int mx, int my, float padding = 10.0f)
{
    glm::vec3 corners[8] = {
        {worldMin.x,worldMin.y,worldMin.z},{worldMax.x,worldMin.y,worldMin.z},
        {worldMax.x,worldMax.y,worldMin.z},{worldMin.x,worldMax.y,worldMin.z},
        {worldMin.x,worldMin.y,worldMax.z},{worldMax.x,worldMin.y,worldMax.z},
        {worldMax.x,worldMax.y,worldMax.z},{worldMin.x,worldMax.y,worldMax.z},
    };
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (auto& c : corners) {
        float sx, sy;
        if (!WorldToScreen(c, view, proj, w, h, sx, sy)) return false;
        minX = std::min(minX, sx); maxX = std::max(maxX, sx);
        minY = std::min(minY, sy); maxY = std::max(maxY, sy);
    }
    return mx >= minX - padding && mx <= maxX + padding && my >= minY - padding && my <= maxY + padding;
}

static int PickBrush(const glm::mat4& view, const glm::mat4& proj, int w, int h, int mx, int my)
{
    for (int i = (int)g_brushes.size()-1; i >= 0; i--) {
        const auto& b = g_brushes[i];
        if (b.m_vertices.size() < 8) continue;
        glm::vec3 mn(1e9f), mx3(-1e9f);
        for (auto& v : b.m_vertices) { mn = glm::min(mn,v); mx3 = glm::max(mx3,v); }
        if (ScreenPointInAABBWorld(mn, mx3, view, proj, w, h, mx, my)) return i;
    }
    return -1;
}

// ============================================================
// 👈 第 2 刀：射线拾取（替换屏幕 AABB）
// ============================================================
static bool ScreenToWorldRay(const glm::mat4& view, const glm::mat4& proj,
                             int w, int h, int mx, int my,
                             glm::vec3& outOrigin, glm::vec3& outDir)
{
    float ndcX = (2.0f * mx) / w - 1.0f;
    float ndcY = 1.0f - (2.0f * my) / h;
    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 nearP = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farP  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    if (fabsf(nearP.w) < 1e-6f || fabsf(farP.w) < 1e-6f) return false;
    glm::vec3 nearW = glm::vec3(nearP) / nearP.w;
    glm::vec3 farW  = glm::vec3(farP)  / farP.w;
    outOrigin = nearW;
    outDir = glm::normalize(farW - nearW);
    return true;
}

static int PickBrushRay(const glm::vec3& rayOrigin, const glm::vec3& rayDir)
{
    int best = -1; float bestT = 1e30f;
    for (size_t i = 0; i < g_brushes.size(); i++) {
        const auto& b = g_brushes[i];
        if (b.m_vertices.size() < 8 || b.m_faces.size() < 6) continue;
        float tmin, tmax;
        if (!RayAABB(rayOrigin, rayDir, b.GetBBoxMins(), b.GetBBoxMaxs(), tmin, tmax)) continue;
        if (tmin > bestT) continue;
        for (int f = 0; f < 6; f++) {
            const auto& face = b.m_faces[f];
            const int* q = face.m_nVertexIndices;
            CollisionTri tris[2] = {
                { b.m_vertices[q[0]], b.m_vertices[q[1]], b.m_vertices[q[2]], face.m_vecNormal },
                { b.m_vertices[q[0]], b.m_vertices[q[2]], b.m_vertices[q[3]], face.m_vecNormal },
            };
            for (int t = 0; t < 2; t++) {
                float thit; glm::vec3 n;
                if (RaycastTriangle(rayOrigin, rayDir, tris[t], thit, n) && thit < bestT) {
                    bestT = thit; best = (int)i;
                }
            }
        }
    }
    return best;
}

static int PickEntityRay(const glm::vec3& rayOrigin, const glm::vec3& rayDir)
{
    int best = -1; float bestT = 1e30f;
    for (size_t i = 0; i < g_entities.size(); i++) {
        float tmin, tmax;
        if (RayAABB(rayOrigin, rayDir, g_entities[i].m_vecBBoxMins, g_entities[i].m_vecBBoxMaxs, tmin, tmax)) {
            if (tmin >= 0.0f && tmin < bestT) { bestT = tmin; best = (int)i; }
        }
    }
    return best;
}

static int PickEntity(const glm::mat4& view, const glm::mat4& proj, int w, int h, int mx, int my)
{
    for (int i = (int)g_entities.size()-1; i >= 0; i--) {
        const auto& e = g_entities[i];
        if (ScreenPointInAABBWorld(e.m_vecBBoxMins, e.m_vecBBoxMaxs, view, proj, w, h, mx, my, 15.0f)) return i;
    }
    return -1;
}
static int PickVertex(const CBrush& b, const glm::mat4& view, const glm::mat4& proj, int w, int h, int mx, int my, float radiusPx = 15.0f)
{
    int best = -1; float bestD2 = radiusPx*radiusPx;
    for (int i = 0; i < 8; i++) {
        float sx, sy;
        if (!WorldToScreen(b.m_vertices[i], view, proj, w, h, sx, sy)) continue;
        float dx = sx - mx, dy = sy - my;
        float d2 = dx*dx + dy*dy;
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}
static int PickEdge(const CBrush& b, const glm::mat4& view, const glm::mat4& proj, int w, int h, int mx, int my, float radiusPx = 15.0f)
{
    int best = -1; float bestD2 = radiusPx*radiusPx;
    for (int e = 0; e < 12; e++) {
        glm::vec3 p0 = b.m_vertices[kEdgeVertexIndices[e][0]];
        glm::vec3 p1 = b.m_vertices[kEdgeVertexIndices[e][1]];
        float sx0, sy0, sx1, sy1;
        if (!WorldToScreen(p0, view, proj, w, h, sx0, sy0)) continue;
        if (!WorldToScreen(p1, view, proj, w, h, sx1, sy1)) continue;
        float dx = sx1-sx0, dy = sy1-sy0;
        float len2 = dx*dx + dy*dy;
        if (len2 < 1e-6f) continue;
        float t = ((mx-sx0)*dx + (my-sy0)*dy) / len2;
        t = glm::clamp(t, 0.0f, 1.0f);
        float cx = sx0 + t*dx, cy = sy0 + t*dy;
        float d2 = (mx-cx)*(mx-cx) + (my-cy)*(my-cy);
        if (d2 < bestD2) { bestD2 = d2; best = e; }
    }
    return best;
}
static int PickFace(const CBrush& b, const glm::mat4& view, const glm::mat4& proj, int w, int h, int mx, int my)
{
    int best = -1; float bestD2 = 1e18f;
    for (int f = 0; f < 6; f++) {
        glm::vec3 c = GetFaceCenter(b, f);
        float sx, sy;
        if (!WorldToScreen(c, view, proj, w, h, sx, sy)) continue;
        float d2 = (sx-mx)*(sx-mx) + (sy-my)*(sy-my);
        if (d2 < bestD2) { bestD2 = d2; best = f; }
    }
    return best;
}

static glm::mat4 BuildSelectionMatrix(bool& isBrush, glm::vec3& pivotOut)
{
    isBrush = false;
    if (g_iSelectedBrush >= 0 && g_iSelectedBrush < (int)g_brushes.size()) {
        isBrush = true;
        const auto& b = g_brushes[g_iSelectedBrush];
        pivotOut = b.m_vecPos;
        return glm::translate(glm::mat4(1.0f), b.m_vecPos);
    }
    if (g_iSelectedEntity >= 0 && g_iSelectedEntity < (int)g_entities.size()) {
        const auto& e = g_entities[g_iSelectedEntity];
        pivotOut = e.m_vecPos;
        glm::mat4 M = glm::translate(glm::mat4(1.0f), e.m_vecPos);
        M *= AngleMatrix(e.m_angRot);
        return M;
    }
    pivotOut = glm::vec3(0);
    return glm::mat4(1.0f);
}

// ---------------------------------------------------------------------------
// Gizmo 状态与拖拽快照
// ---------------------------------------------------------------------------
struct DragSnapshot {
    glm::vec3 pivot;
    std::vector<glm::vec3> vertices;
    glm::vec3 entityPos;
    glm::vec3 entityAngles;
    float screenStartX;
    float screenStartY;
    bool  bValid = false;
} g_dragSnap;

// ---------------------------------------------------------------------------
// DrawAndManipulateGizmo
// ---------------------------------------------------------------------------
static void DrawAndManipulateGizmo(const glm::mat4& view, const glm::mat4& proj,
                                    ImVec2 viewportPos, ImVec2 viewportSize)
{
    // 每次进入先重置拖拽标志，防止状态卡死
    g_bDraggingGizmo = false;

    if (g_iGizmoMode == GIZMO_NONE) return;
    if (!g_bShowGizmo) return;

    bool isSubEdit = (g_iGizmoMode == GIZMO_FACE || g_iGizmoMode == GIZMO_EDGE || g_iGizmoMode == GIZMO_VERTEX);

    if (isSubEdit) {
        if (g_iSelectedBrush < 0 || g_iSelectedBrush >= (int)g_brushes.size()) return;
        if (g_iSubEditIndex < 0) return;
    } else {
        if (g_iSelectedBrush < 0 && g_iSelectedEntity < 0) return;
    }

    bool isBrush = false;
    glm::vec3 pivot(0);
    glm::mat4 baseMat = glm::mat4(1.0f);

    if (isSubEdit) {
        isBrush = true;
        const CBrush& b = g_brushes[g_iSelectedBrush];
        if (g_dragSnap.bValid) {
            pivot = g_dragSnap.pivot;
        } else {
            if (g_iGizmoMode == GIZMO_FACE)        pivot = GetFaceCenter(b, g_iSubEditIndex);
            else if (g_iGizmoMode == GIZMO_EDGE)   pivot = (b.m_vertices[kEdgeVertexIndices[g_iSubEditIndex][0]] + b.m_vertices[kEdgeVertexIndices[g_iSubEditIndex][1]]) * 0.5f;
            else                                   pivot = b.m_vertices[g_iSubEditIndex];
        }
        baseMat = glm::translate(glm::mat4(1.0f), pivot);
    } else {
        if (g_iSelectedBrush >= 0) {
            isBrush = true;
            pivot = g_brushes[g_iSelectedBrush].m_vecPos;
            baseMat = glm::translate(glm::mat4(1.0f), pivot);
        } else if (g_iSelectedEntity >= 0) {
            pivot = g_entities[g_iSelectedEntity].m_vecPos;
            baseMat = glm::translate(glm::mat4(1.0f), pivot);
            baseMat *= AngleMatrix(g_entities[g_iSelectedEntity].m_angRot);
        }
    }

    glm::mat4 viewIm = view;
    glm::mat4 projIm = proj;
    glm::mat4 manipulateMat = baseMat;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGui::SetCursorScreenPos(viewportPos);
    ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);

    ImGuizmo::OPERATION op = (ImGuizmo::OPERATION)0;
    if (g_iGizmoMode == GIZMO_TRANSLATE) op = ImGuizmo::TRANSLATE;
    else if (g_iGizmoMode == GIZMO_ROTATE) op = ImGuizmo::ROTATE;
    else if (g_iGizmoMode == GIZMO_SCALE) op = ImGuizmo::SCALE;
    else if (isSubEdit) op = ImGuizmo::TRANSLATE; // 子编辑统一用平移箭头

    float* snapPtr = (g_bUseSnap && g_iGizmoMode != GIZMO_SCALE) ? g_Snap : nullptr;
    
    // 记录上一帧是否在拖拽（用于判断是否刚刚松开鼠标）
    bool wasUsing = g_bDraggingGizmo;

    bool bUsing = ImGuizmo::Manipulate(glm::value_ptr(viewIm), glm::value_ptr(projIm),
                                       op, ImGuizmo::LOCAL,
                                       glm::value_ptr(manipulateMat), nullptr, snapPtr, nullptr, nullptr);

    ImVec2 mousePos = ImGui::GetMousePos();

    if (bUsing && !g_dragSnap.bValid) {
        g_dragSnap.bValid = true;
        UndoStack::Instance().PushSnapshot("Gizmo Edit");
        g_dragSnap.screenStartX = mousePos.x;
        g_dragSnap.screenStartY = mousePos.y;
        if (isBrush) {
            g_dragSnap.pivot = pivot;
            g_dragSnap.vertices = g_brushes[g_iSelectedBrush].m_vertices;
        } else if (g_iSelectedEntity >= 0) {
            g_dragSnap.entityPos = g_entities[g_iSelectedEntity].m_vecPos;
            g_dragSnap.entityAngles = g_entities[g_iSelectedEntity].m_angRot;
        }
    }

    if (bUsing && g_dragSnap.bValid) {
        if (isSubEdit) {
            float tr[3], rt[3], sc[3];
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(manipulateMat), tr, rt, sc);
            glm::vec3 newCenter(tr[0], tr[1], tr[2]);
            glm::vec3 offset = newCenter - pivot;

            CBrush& b = g_brushes[g_iSelectedBrush];
            b.m_vertices = g_dragSnap.vertices;

            if (g_iGizmoMode == GIZMO_FACE) {
                for (int k = 0; k < 4; k++) {
                    int vi = b.m_faces[g_iSubEditIndex].m_nVertexIndices[k];
                    b.m_vertices[vi] += offset;
                }
            } else if (g_iGizmoMode == GIZMO_EDGE) {
                int i0 = kEdgeVertexIndices[g_iSubEditIndex][0];
                int i1 = kEdgeVertexIndices[g_iSubEditIndex][1];
                b.m_vertices[i0] += offset;
                b.m_vertices[i1] += offset;
            } else if (g_iGizmoMode == GIZMO_VERTEX) {
                b.m_vertices[g_iSubEditIndex] += offset;
            }
            b.RecalcBoundsFromVertices();
            b.RecalcFaceNormals();
        }
        else if (isBrush) {
            CBrush& b = g_brushes[g_iSelectedBrush];
            b.m_vertices = g_dragSnap.vertices;
            glm::vec3 center = g_dragSnap.pivot;

            if (g_iGizmoMode == GIZMO_TRANSLATE) {
                float tr[3], rt[3], sc[3];
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(manipulateMat), tr, rt, sc);
                glm::vec3 newCenter(tr[0], tr[1], tr[2]);
                glm::vec3 offset = newCenter - center;
                for (auto& v : b.m_vertices) v += offset;
                b.m_vecPos = newCenter;
            } 
            else if (g_iGizmoMode == GIZMO_ROTATE) {
                glm::mat3 R = glm::mat3(manipulateMat);
                glm::mat4 rotM = glm::translate(glm::mat4(1.0f), center)
                               * glm::mat4(R)
                               * glm::translate(glm::mat4(1.0f), -center);
                for (auto& v : b.m_vertices) v = glm::vec3(rotM * glm::vec4(v, 1.0f));
                b.m_vecPos = center;
            } 
            else if (g_iGizmoMode == GIZMO_SCALE) {
                // 注意：这里的 scale 修复之前的代码。你如果没改，就先保留之前的逻辑。
                float dx = mousePos.x - g_dragSnap.screenStartX;
                float dy = mousePos.y - g_dragSnap.screenStartY;
                float delta = (fabsf(dx) > fabsf(dy)) ? dx : -dy;
                float scaleFactor = 1.0f + delta * 0.005f;
                if (scaleFactor < 0.01f) scaleFactor = 0.01f;

                glm::vec3 scVec(glm::length(glm::vec3(manipulateMat[0])),
                                glm::length(glm::vec3(manipulateMat[1])),
                                glm::length(glm::vec3(manipulateMat[2])));
                float diffX = fabsf(scVec.x - 1.0f);
                float diffY = fabsf(scVec.y - 1.0f);
                float diffZ = fabsf(scVec.z - 1.0f);

                glm::vec3 scaleVec(scaleFactor);
                if (diffX > 0.01f && diffX >= diffY && diffX >= diffZ) scaleVec = glm::vec3(scaleFactor, 1.0f, 1.0f);
                else if (diffY > 0.01f && diffY >= diffX && diffY >= diffZ) scaleVec = glm::vec3(1.0f, scaleFactor, 1.0f);
                else if (diffZ > 0.01f) scaleVec = glm::vec3(1.0f, 1.0f, scaleFactor);

                for (auto& v : b.m_vertices) {
                    glm::vec3 local = v - center;
                    local.x *= scaleVec.x;
                    local.y *= scaleVec.y;
                    local.z *= scaleVec.z;
                    v = center + local;
                }
                b.m_vecPos = center;
            }
            b.RecalcBoundsFromVertices();
            b.RecalcFaceNormals();
        } 
        else if (g_iSelectedEntity >= 0) {
            CEntity& e = g_entities[g_iSelectedEntity];
            float tr[3], rt[3], sc[3];
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(manipulateMat), tr, rt, sc);
            e.m_vecPos = glm::vec3(tr[0], tr[1], tr[2]);
            if (g_iGizmoMode == GIZMO_ROTATE) e.m_angRot = glm::vec3(rt[0], rt[1], rt[2]);
            UpdateEntityBBox(e);
        }
    }

    if (!bUsing && g_dragSnap.bValid) {
        g_dragSnap.bValid = false;
    }

    g_bDraggingGizmo = bUsing;
}

// ---------------------------------------------------------------------------
// 初始场景 (Y-up 调整)
// ---------------------------------------------------------------------------
static void BuildDefaultScene()
{
    g_brushes.clear();
    g_entities.clear();

    const float HALF_W  = 4096.0f;
    const float WALL_TH = 16.0f;
    const float FLOOR_H = 16.0f;
    const float CEIL_H  = 16.0f;
    const float ROOM_H  = 2048.0f;
    const float Y_CENTER = (ROOM_H + CEIL_H - FLOOR_H) * 0.5f;
    const float Y_HALF   = (ROOM_H + CEIL_H + FLOOR_H) * 0.5f;

    auto MakeBrush = [](glm::vec3 pos, glm::vec3 halfSize, const char* mat) {
        CBrush b;
        b.m_vecPos  = pos;
        b.m_vecSize = halfSize;
        b.m_szMaterial = mat;
        b.RebuildPrimitiveMesh();
        g_brushes.push_back(b);
    };

    // 地板 (Y = -FLOOR_H/2)
    MakeBrush(glm::vec3(0, -FLOOR_H*0.5f, 0),               glm::vec3(HALF_W, FLOOR_H*0.5f, HALF_W), "CONCRETE/CONCRETEFLOOR001A");
    // 天花板 (Y = ROOM_H + CEIL_H/2)
    MakeBrush(glm::vec3(0, ROOM_H+CEIL_H*0.5f, 0),          glm::vec3(HALF_W, CEIL_H*0.5f, HALF_W),  "CONCRETE/CONCRETECEILING001A");
    // 墙壁 (Y 在中间)
    MakeBrush(glm::vec3(0,  Y_CENTER, HALF_W+WALL_TH*0.5f), glm::vec3(HALF_W+WALL_TH, Y_HALF, WALL_TH*0.5f), "BRICK/BRICKWALL001A");
    MakeBrush(glm::vec3(0,  Y_CENTER, -(HALF_W+WALL_TH*0.5f)),glm::vec3(HALF_W+WALL_TH, Y_HALF, WALL_TH*0.5f), "BRICK/BRICKWALL001A");
    MakeBrush(glm::vec3(-(HALF_W+WALL_TH*0.5f), Y_CENTER, 0),glm::vec3(WALL_TH*0.5f, Y_HALF, HALF_W+WALL_TH), "BRICK/BRICKWALL001A");
    MakeBrush(glm::vec3( (HALF_W+WALL_TH*0.5f), Y_CENTER, 0),glm::vec3(WALL_TH*0.5f, Y_HALF, HALF_W+WALL_TH), "BRICK/BRICKWALL001A");

    {
        CEntity light;
        light.m_iType  = ENTITY_LIGHT;
        light.m_szName = "light";
        light.m_vecPos = glm::vec3(0, 2000, 0);
        light.m_properties["_light"]    = "2048 2000 2000";
        light.m_properties["_distance"] = "8192";
        UpdateEntityBBox(light);
        g_entities.push_back(light);
    }
    {
        CEntity spawn;
        spawn.m_iType  = ENTITY_PLAYER_START;
        spawn.m_szName = "info_player_start";
        spawn.m_vecPos = glm::vec3(0, 16, 0);
        UpdateEntityBBox(spawn);
        g_entities.push_back(spawn);
    }
}

// ---------------------------------------------------------------------------
// 回调
// ---------------------------------------------------------------------------
static void OnExportMap() {
    if (ExportMapJson(currentMap+".json")) LogAdd("[MAP] exported");
}
static void OnImportMap() {
    if (ImportMapJson(currentMap+".json")) {
        LogAdd("[MAP] imported");
        g_iSelectedBrush  = -1;
        g_iSelectedEntity = -1;

        // 👈 新增：自动居中相机到所有 Brush 的包围盒中心
        if (!g_brushes.empty()) {
            glm::vec3 minPos(1e9f), maxPos(-1e9f);
            for (const auto& b : g_brushes) {
                minPos = glm::min(minPos, b.m_vecPos - b.m_vecSize);
                maxPos = glm::max(maxPos, b.m_vecPos + b.m_vecSize);
            }
            glm::vec3 center = (minPos + maxPos) * 0.5f;
            glm::vec3 size = maxPos - minPos;
            
            g_cam.target = center;
            g_cam.dist   = glm::length(size) * 0.6f; // 拉到能看全图
            g_cam.yaw    = 45.0f;
            g_cam.pitch  = -30.0f;
            
            printf("[Editor] Auto-centered camera to map bounds. Center=(%.1f, %.1f, %.1f), Size=(%.1f, %.1f, %.1f)\n",
                   center.x, center.y, center.z, size.x, size.y, size.z);
        }
    }
}
static void OnLoadBvh() {
    if (!LoadMap((currentMap+".bvh").c_str(), g_loadedMapData)) {
        LogAdd("[EDITOR] Failed to load BVH");
        return;
    }
    g_bLoadedBvh = true;
    LogAdd("[EDITOR] Loaded BVH");
    printf("[Editor] verts=%zu indices=%zu groups=%zu\n",
           g_loadedMapData.verts.size(),
           g_loadedMapData.indices.size(),
           g_loadedMapData.materialGroups.size());

    // 上传 VBO
    if (g_bvhVAO == 0) {
        glGenVertexArrays(1, &g_bvhVAO);
        glGenBuffers(1, &g_bvhVBO);
        glGenBuffers(1, &g_bvhEBO);
    }
    glBindVertexArray(g_bvhVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_bvhVBO);
    glBufferData(GL_ARRAY_BUFFER, g_loadedMapData.verts.size() * 4, g_loadedMapData.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_bvhEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, g_loadedMapData.indices.size() * 4, g_loadedMapData.indices.data(), GL_STATIC_DRAW);

    // 注意：你的 g_brushProg 的顶点布局是 pos3 + normal3 + uv2 = 8 floats
    // 但 MapData 的 stride 是 11（pos3 + normal3 + color3 + uv2）
    // 所以这里需要把 color3 跳过，只传 pos/normal/uv
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(3 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(9 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}
static void OnCompileAndRun() {
    if (!ExportMapJson(currentMap+".json")) { LogAdd("[COMPILE] export FAILED"); return; }
    
    // 根据平台决定执行命令
#ifdef _WIN32
    std::string cmdMap = "bvh.exe";
    std::string cmdGame = "retro.exe";
#else
    std::string cmdMap = "./bvh";
    std::string cmdGame = "./retro";
#endif

    cmdMap+=" -n -b -i "+currentMap+".json -o "+currentMap+".bvh";
    printf("[Editor]%s\n", cmdMap.c_str());
    int ret = system(cmdMap.c_str());
    char b[128]; 
    snprintf(b, sizeof(b), "[COMPILE] %s exit=%d", cmdMap.c_str(), ret);
    LogAdd(b);

    cmdGame+=" +map "+currentMap;
    printf("[Editor]%s\n", cmdGame.c_str());
    ret = system(cmdGame.c_str());
    snprintf(b, sizeof(b), "[COMPILE] %s exit=%d", cmdGame.c_str(), ret);
    LogAdd(b);
}

// ---------------------------------------------------------------------------
// Draw3DView
// ---------------------------------------------------------------------------
static void Draw3DView(bool& outClickedLeft, bool& outClickedRight, bool& outClickedMiddle)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
    
    ImGui::Begin("3D View", nullptr, flags);
    
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    int vpW = (int)viewportSize.x;
    int vpH = (int)viewportSize.y;
    if (vpW < 1) vpW = 1;
    if (vpH < 1) vpH = 1;

    ImVec2 viewportPos = ImGui::GetCursorScreenPos();
    ImVec2 mousePos = ImGui::GetMousePos();
    bool bHovered = ImGui::IsWindowHovered();
    bool bMouseInViewport = bHovered && 
                            mousePos.x >= viewportPos.x && mousePos.x <= viewportPos.x + viewportSize.x &&
                            mousePos.y >= viewportPos.y && mousePos.y <= viewportPos.y + viewportSize.y;

    int localMouseX = (int)(mousePos.x - viewportPos.x);
    int localMouseY = (int)(mousePos.y - viewportPos.y);

    glm::mat4 view = g_cam.GetView();
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)vpW/(float)vpH, 1.0f, 10000.0f);

    // ★ 强制同步状态
    bool bIsSubEdit = (g_iGizmoMode == GIZMO_FACE || g_iGizmoMode == GIZMO_EDGE || g_iGizmoMode == GIZMO_VERTEX);
    g_iSubEditMode = bIsSubEdit ? (g_iGizmoMode == GIZMO_FACE ? SUB_FACE :
                                  (g_iGizmoMode == GIZMO_EDGE ? SUB_EDGE : SUB_VERTEX)) : SUB_NONE;

    // =========================================================
    // 渲染到 FBO
    // =========================================================
    ResizeFBO(vpW, vpH);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, vpW, vpH);
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 vp = proj * view;
    DrawGrid(vp);
    DrawBrushes(vp);
    DrawBrushWireframe(vp);
    if (g_bLoadedBvh && g_bvhVAO != 0) {
        glUseProgram(g_brushProg);
        GLint uMVP = glGetUniformLocation(g_brushProg, "uMVP");
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(vp));
        glBindVertexArray(g_bvhVAO);
        glDrawElements(GL_TRIANGLES, (GLsizei)g_loadedMapData.indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
    // ========== 选中 Brush 高亮（黄色 x-ray 线框）==========
    if (g_iSelectedBrush >= 0 && g_iSelectedBrush < (int)g_brushes.size()) {
        const auto& b = g_brushes[g_iSelectedBrush];
        std::vector<float> buf;
        buf.reserve(12 * 2 * 6);
        for (int e = 0; e < 12; e++) {
            const auto& p0 = b.m_vertices[kEdgeVertexIndices[e][0]];
            const auto& p1 = b.m_vertices[kEdgeVertexIndices[e][1]];
            buf.insert(buf.end(), {p0.x,p0.y,p0.z, 1.0f,1.0f,0.0f});
            buf.insert(buf.end(), {p1.x,p1.y,p1.z, 1.0f,1.0f,0.0f});
        }
        glDisable(GL_DEPTH_TEST);
        DrawColorLines(buf, vp);
        glEnable(GL_DEPTH_TEST);
    }    
    DrawEntityBBoxes(vp);

    // =========================================================
    // 子编辑高亮渲染（core profile：全部用 shader）
    // =========================================================
    if (bIsSubEdit && g_iSelectedBrush >= 0 && g_iSelectedBrush < (int)g_brushes.size()) {
        const CBrush& b = g_brushes[g_iSelectedBrush];

        if (g_iSubEditMode == SUB_FACE && g_iSubEditIndex >= 0 && g_iSubEditIndex < 6) {
            glm::vec3 quad[4];
            for (int k = 0; k < 4; k++) {
                int vi = b.m_faces[g_iSubEditIndex].m_nVertexIndices[k];
                quad[k] = b.m_vertices[vi];
            }
            DrawFaceHighlight(quad, vp);
        }

        if (g_iSubEditMode == SUB_EDGE && g_iSubEditIndex >= 0 && g_iSubEditIndex < 12) {
            int i0 = kEdgeVertexIndices[g_iSubEditIndex][0];
            int i1 = kEdgeVertexIndices[g_iSubEditIndex][1];
            const glm::vec3& p0 = b.m_vertices[i0];
            const glm::vec3& p1 = b.m_vertices[i1];
            std::vector<float> buf = {
                p0.x, p0.y, p0.z, 1, 1, 0,
                p1.x, p1.y, p1.z, 1, 1, 0,
            };
            glDisable(GL_DEPTH_TEST);   // x-ray 可见
            DrawColorLines(buf, vp);
            glEnable(GL_DEPTH_TEST);
        }

        if (g_iSubEditMode == SUB_VERTEX) {
            std::vector<std::pair<glm::vec3, glm::vec3>> pts;
            pts.reserve(8);
            for (int v = 0; v < 8; v++) {
                glm::vec3 col = (v == g_iSubEditIndex) ? glm::vec3(1,1,0) : glm::vec3(0.86f,0.86f,0.86f);
                pts.emplace_back(b.m_vertices[v], col);
            }
            DrawVertexPoints(pts, vp);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // =========================================================
    // 画到 ImGui
    // =========================================================
    ImGui::Image((ImTextureID)(intptr_t)g_fboTex, viewportSize, ImVec2(0, 1), ImVec2(1, 0));

    // =========================================================
    // 渲染 Gizmo （此时 ImGuizmo 会更新内部状态）
    // =========================================================
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    DrawAndManipulateGizmo(view, proj, viewportPos, viewportSize);

    // =========================================================
    // 拾取逻辑（移到了 Gizmo 之后，此时 IsUsing/IsOver 是最准确的）
    // =========================================================
    bool bIsUsingGizmo = ImGuizmo::IsUsing() || ImGuizmo::IsOver();
    
    // 使用 IsMouseClicked 保证每次点击只触发一帧
    if (bMouseInViewport && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !bIsUsingGizmo && !g_bDraggingGizmo)
    {
        if (bIsSubEdit && g_iSelectedBrush >= 0 && g_iSelectedBrush < (int)g_brushes.size())
        {
            CBrush& b = g_brushes[g_iSelectedBrush];
            int hit = -1;
            if (g_iSubEditMode == SUB_FACE)        hit = PickFace  (b, view, proj, vpW, vpH, localMouseX, localMouseY);
            else if (g_iSubEditMode == SUB_EDGE)   hit = PickEdge  (b, view, proj, vpW, vpH, localMouseX, localMouseY);
            else if (g_iSubEditMode == SUB_VERTEX) hit = PickVertex(b, view, proj, vpW, vpH, localMouseX, localMouseY);

            if (hit >= 0) {
                g_iSubEditIndex = hit;
                UndoStack::Instance().PushSnapshot("Sub-Edit");
            } else {
                g_iSubEditIndex = -1;
            }
        }
        else
        {
            // 👈 第 2 刀：射线拾取（谁近选谁）
            glm::vec3 rayO, rayD;
            int bi = -1, ei = -1;
            if (ScreenToWorldRay(view, proj, vpW, vpH, localMouseX, localMouseY, rayO, rayD)) {
                bi = PickBrushRay(rayO, rayD);
                ei = PickEntityRay(rayO, rayD);
            }
            if (ei >= 0) {
                g_iSelectedEntity = ei;
                g_iSelectedBrush  = -1;
            } else {
                g_iSelectedBrush  = bi;
                g_iSelectedEntity = -1;
            }
            g_iSubEditIndex = -1;
        }
    }

    // =========================================================
    // 右上角相机控件 (Orientation Cube / ViewManipulate)
    // =========================================================
    glm::mat4 camViewIm = view; 
    float camDistance = g_cam.dist;

    ImVec2 viewManipulatePos(viewportPos.x + viewportSize.x - 128, viewportPos.y);
    ImGui::SetCursorScreenPos(viewManipulatePos);
    
    ImGuizmo::PushID("MyViewManip");
    // ImGuizmo 会在内部把点击后计算出的新朝向写入 camViewIm
    ImGuizmo::ViewManipulate(glm::value_ptr(camViewIm), camDistance, viewManipulatePos, ImVec2(128, 128), 0x10101010);
    
    ImRect viewManipRect(viewManipulatePos, ImVec2(viewManipulatePos.x + 128, viewManipulatePos.y + 128));
    
    // 只要正在使用罗盘，或者鼠标点击了罗盘区域，我们就同步相机的朝向
    if (ImGuizmo::IsUsingViewManipulate() || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(viewManipRect.Min, viewManipRect.Max)))
    {
        // camViewIm 是 View 矩阵，所以它的逆矩阵包含了相机的 Eye 坐标
        glm::mat4 invView = glm::inverse(camViewIm);
        glm::vec3 eye = glm::vec3(invView[3][0], invView[3][1], invView[3][2]);
        
        // 算出从 Target 到 Eye 的偏移向量
        glm::vec3 offset = eye - g_cam.target;
        
        // 根据这个偏移重算 Yaw 和 Pitch（你的引擎用的是 Y-Up 坐标系）
        float r = glm::length(glm::vec2(offset.x, offset.z)); // 水平面半径
        if (r > 0.001f) {
            g_cam.yaw = glm::degrees(std::atan2(offset.z, offset.x));
            g_cam.pitch = glm::degrees(std::atan2(offset.y, r));
        }
        // 同步距离
        g_cam.dist = glm::length(offset);
    }
    ImGuizmo::PopID();

    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// 主函数
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    std::cout << "branch: " << Version::GIT_BRANCH 
                << ", commit: " << Version::GIT_HASH << ", " 
                << Version::CMAKE_BUILD_TYPE << std::endl;

    FileSystem::Init(".");
    // temporary only
    ScanMaterialsRecursive("./materials");  // 👈 扫描素材目录

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-map") == 0 && i + 1 < argc) {
            currentMap = argv[++i];
        }            
    }                

#ifdef _WIN32
    ::SetProcessDPIAware();
#endif
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    g_window = SDL_CreateWindow("Retro Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 g_winW, g_winH, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    g_glContext = SDL_GL_CreateContext(g_window);
    SDL_GL_MakeCurrent(g_window, g_glContext);
    SDL_GL_SetSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { fprintf(stderr, "GLEW init failed\n"); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    
    main_scale = ImGui_ImplSDL2_GetContentScaleForDisplay(0);
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplSDL2_InitForOpenGL(g_window, g_glContext);
    ImGui_ImplOpenGL3_Init("#version 330");

    InitGL();

    if (!GameDatabase::Instance().LoadFromFile("editor.json"))
        LogAdd("[GameDatabase] FAILED to load editor.json, entity panel will be empty");

    BuildDefaultScene();
    SetExportMapHandler(OnExportMap);
    SetImportMapHandler(OnImportMap);
    SetCompileAndRunHandler(OnCompileAndRun);
    SetLoadBvhHandler(OnLoadBvh);   // 👈 新增

    // open currentMap
    OnImportMap();

    bool running = true;
    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                g_winW = ev.window.data1; g_winH = ev.window.data2;
            }
            if (ev.type == SDL_MOUSEMOTION) {
                g_iMouseX = ev.motion.x; g_iMouseY = ev.motion.y;
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN) {
                if (ev.button.button == SDL_BUTTON_LEFT)   g_bLeftMouseDown   = true;
                if (ev.button.button == SDL_BUTTON_RIGHT)  g_bRightMouseDown  = true;
                if (ev.button.button == SDL_BUTTON_MIDDLE) g_bMiddleMouseDown = true;
                g_iMouseX = ev.button.x; g_iMouseY = ev.button.y;
            }
            if (ev.type == SDL_MOUSEBUTTONUP) {
                if (ev.button.button == SDL_BUTTON_LEFT)   g_bLeftMouseDown   = false;
                if (ev.button.button == SDL_BUTTON_RIGHT)  g_bRightMouseDown  = false;
                if (ev.button.button == SDL_BUTTON_MIDDLE) g_bMiddleMouseDown = false;
            }
            if (ev.type == SDL_KEYDOWN && !io.WantCaptureKeyboard) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = false;
                else if (k == SDLK_1) g_iGizmoMode = GIZMO_TRANSLATE;
                else if (k == SDLK_2) g_iGizmoMode = GIZMO_ROTATE;
                else if (k == SDLK_3) g_iGizmoMode = GIZMO_SCALE;
                else if (k == SDLK_4) g_iGizmoMode = GIZMO_FACE;
                else if (k == SDLK_5) g_iGizmoMode = GIZMO_EDGE;
                else if (k == SDLK_6) g_iGizmoMode = GIZMO_VERTEX;
                else if (k == SDLK_0) g_iGizmoMode = GIZMO_NONE;
                else if (k == SDLK_z && (ev.key.keysym.mod & KMOD_CTRL)) UndoStack::Instance().Undo();
                else if (k == SDLK_y && (ev.key.keysym.mod & KMOD_CTRL)) UndoStack::Instance().Redo();
                else if (k == SDLK_LEFTBRACKET)  g_flGridSize = std::max(1.0f, g_flGridSize*0.5f);
                else if (k == SDLK_RIGHTBRACKET) g_flGridSize = std::min(128.0f, g_flGridSize*2.0f);
            }
        }

        if (!io.WantCaptureKeyboard) {
            float flySpeed = g_cam.dist * 1.2f * 0.016f;
            float rp = glm::radians(g_cam.pitch), ry = glm::radians(g_cam.yaw);
            
            // Y-up 坐标系下的标准前向与右向
            glm::vec3 fwd(std::cos(rp)*std::cos(ry), std::sin(rp), std::cos(rp)*std::sin(ry));
            glm::vec3 fwdH(fwd.x, 0, fwd.z); 
            if (glm::length(fwdH) > 1e-4f) fwdH = glm::normalize(fwdH);
            
            // Y-up 右手系里的右向量：Up × Forward
            glm::vec3 rightH = glm::normalize(glm::cross(glm::vec3(0, 1, 0), fwdH));

            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            if (ks[SDL_SCANCODE_W]) g_cam.target -= fwdH * flySpeed;
            if (ks[SDL_SCANCODE_S]) g_cam.target += fwdH * flySpeed;
            if (ks[SDL_SCANCODE_A]) g_cam.target -= rightH * flySpeed; // A 是左
            if (ks[SDL_SCANCODE_D]) g_cam.target += rightH * flySpeed; // D 是右
            if (ks[SDL_SCANCODE_E]) g_cam.target.y += flySpeed;
            if (ks[SDL_SCANCODE_Q]) g_cam.target.y -= flySpeed;
        }

        for (auto& e : g_entities) UpdateEntityBBox(e);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        EditorLayout::Begin();
        DrawMenuBar();
        DrawToolsPanel();
        BrushPanelResult panelRes;
        DrawEntityPanel(g_entities.data(), (int)g_entities.size(), &g_iSelectedEntity, &g_iSelectedBrush, panelRes);
        DrawBrushPanel(g_brushes.data(), (int)g_brushes.size(), &g_iSelectedBrush, &g_iSelectedEntity, panelRes);
        DrawGraphEditorPanel();
        Draw3DView(g_bLeftMouseDown, g_bRightMouseDown, g_bMiddleMouseDown);
        Draw2DViews();

        //DrawConsolePanel();
        //DrawScenePanel();
        DrawCompileOptionsWindow();
        DrawMaterialBrowserWindow();

        if (panelRes.bRequestNew) {
            UndoStack::Instance().PushSnapshot("New Brush");
            CBrush b;
            b.m_vecPos  = glm::vec3(0,32,0);
            b.m_vecSize = glm::vec3(32,32,32);
            b.RebuildPrimitiveMesh();
            g_brushes.push_back(b);
            g_iSelectedBrush = (int)g_brushes.size() - 1;
        }
        if (panelRes.bRequestDuplicate && panelRes.iTargetIndex >= 0 && panelRes.iTargetIndex < (int)g_brushes.size()) {
            UndoStack::Instance().PushSnapshot("Duplicate Brush");
            CBrush b = g_brushes[panelRes.iTargetIndex];
            for (auto& v : b.m_vertices) v += glm::vec3(32,0,0);
            b.m_vecPos += glm::vec3(32,0,0);
            g_brushes.push_back(b);
            g_iSelectedBrush = (int)g_brushes.size() - 1;
        }
        if (panelRes.bRequestDelete && panelRes.iTargetIndex >= 0 && panelRes.iTargetIndex < (int)g_brushes.size()) {
            UndoStack::Instance().PushSnapshot("Delete Brush");
            g_brushes.erase(g_brushes.begin() + panelRes.iTargetIndex);
            g_iSelectedBrush = -1;
            printf("Delete brush %d\n", panelRes.iTargetIndex);
        }
        if (panelRes.bRequestDeleteEntity && panelRes.iTargetEntityIndex >= 0 && panelRes.iTargetEntityIndex < (int)g_entities.size()) {
            UndoStack::Instance().PushSnapshot("Delete Entity");
            g_entities.erase(g_entities.begin() + panelRes.iTargetEntityIndex);
            g_iSelectedEntity = -1;
        }
        if (panelRes.bRequestNewEntity && !panelRes.szNewEntityClass.empty()) {
            UndoStack::Instance().PushSnapshot("New Entity");
            CEntity e;
            e.m_szName = panelRes.szNewEntityClass;
            e.m_vecPos = glm::vec3(0,32,0);
            if (e.m_szName == "info_player_start") e.m_iType = ENTITY_PLAYER_START;
            else if (e.m_szName == "light")       e.m_iType = ENTITY_LIGHT;
            UpdateEntityBBox(e);
            g_entities.push_back(e);
            g_iSelectedEntity = (int)g_entities.size() - 1;
        }

        ImGui::Render();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, g_winW, g_winH);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(g_window);
        g_iLastMouseX = g_iMouseX;
        g_iLastMouseY = g_iMouseY;
    }

    TextureCache::Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(g_glContext);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}