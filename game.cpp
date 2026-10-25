#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <thread>

// Jolt 核心头文件
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// 碰撞三角形 & BVH
// ============================================================================
struct CollisionTri {
    glm::vec3 v0, v1, v2;
    glm::vec3 normal;
};

static bool RaycastTriangle(const glm::vec3& orig, const glm::vec3& dir, const CollisionTri& tri, float& t, glm::vec3& outNormal) {
    const float EPSILON = 1e-7f;
    glm::vec3 edge1 = tri.v1 - tri.v0;
    glm::vec3 edge2 = tri.v2 - tri.v0;
    glm::vec3 h = glm::cross(dir, edge2);
    float a = glm::dot(edge1, h);
    if (fabsf(a) < EPSILON) return false;
    float f = 1.0f / a;
    glm::vec3 s = orig - tri.v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    t = f * glm::dot(edge2, q);
    if (t > EPSILON) {
        outNormal = tri.normal;
        return true;
    }
    return false;
}

struct BVHNode {
    glm::vec3 bmin, bmax;
    int left = -1, right = -1;
    int start = 0, count = 0;
    bool isLeaf() const { return count > 0; }
};

static std::vector<CollisionTri> g_sortedTris;
static std::vector<BVHNode>      g_bvh;

static int BuildBVH(int start, int count) {
    int nodeIdx = (int)g_bvh.size();
    g_bvh.push_back({});
    glm::vec3 mn(1e30f), mx(-1e30f);
    for (int i = start; i < start + count; i++) {
        const auto& t = g_sortedTris[i];
        mn = glm::min(mn, glm::min(t.v0, glm::min(t.v1, t.v2)));
        mx = glm::max(mx, glm::max(t.v0, glm::max(t.v1, t.v2)));
    }
    g_bvh[nodeIdx].bmin = mn; g_bvh[nodeIdx].bmax = mx;
    if (count <= 4) {
        g_bvh[nodeIdx].start = start; g_bvh[nodeIdx].count = count;
        return nodeIdx;
    }
    glm::vec3 extent = mx - mn;
    int axis = (extent.y > extent.x) ? 1 : 0;
    if (extent.z > ((axis == 0) ? extent.x : extent.y)) axis = 2;
    float mid = mn[axis] + extent[axis] * 0.5f;
    int leftCount = 0;
    for (int i = start; i < start + count; i++) {
        glm::vec3 c = (g_sortedTris[i].v0 + g_sortedTris[i].v1 + g_sortedTris[i].v2) * (1.0f / 3.0f);
        if (c[axis] < mid) leftCount++;
    }
    if (leftCount == 0 || leftCount == count) leftCount = count / 2;
    std::nth_element(
        g_sortedTris.begin() + start, g_sortedTris.begin() + start + leftCount, g_sortedTris.begin() + start + count,
        [axis](const CollisionTri& a, const CollisionTri& b) {
            glm::vec3 ca = (a.v0 + a.v1 + a.v2) * (1.0f / 3.0f);
            glm::vec3 cb = (b.v0 + b.v1 + b.v2) * (1.0f / 3.0f);
            return ca[axis] < cb[axis];
        });
    int leftIdx = BuildBVH(start, leftCount);
    int rightIdx = BuildBVH(start + leftCount, count - leftCount);
    g_bvh[nodeIdx].left = leftIdx; g_bvh[nodeIdx].right = rightIdx;
    return nodeIdx;
}

static bool RayAABB(const glm::vec3& o, const glm::vec3& d, const glm::vec3& bmin, const glm::vec3& bmax, float& tmin, float& tmax) {
    tmin = 0.0f; tmax = 1e30f;
    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < 1e-8f) {
            if (o[i] < bmin[i] || o[i] > bmax[i]) return false;
        } else {
            float invD = 1.0f / d[i];
            float t0 = (bmin[i] - o[i]) * invD, t1 = (bmax[i] - o[i]) * invD;
            if (t0 > t1) std::swap(t0, t1);
            if (t0 > tmin) tmin = t0;
            if (t1 < tmax) tmax = t1;
            if (tmin > tmax) return false;
        }
    }
    return true;
}

static float TraceBVH(const glm::vec3& start, const glm::vec3& dir, glm::vec3& outNormal) {
    if (g_bvh.empty()) return -1.0f;
    float closestT = 1e30f; bool hit = false;
    int stack[64], sp = 0; stack[sp++] = 0;
    while (sp > 0) {
        int nodeIdx = stack[--sp];
        const BVHNode& node = g_bvh[nodeIdx];
        float tmin, tmax;
        if (!RayAABB(start, dir, node.bmin, node.bmax, tmin, tmax)) continue;
        if (tmin > closestT) continue;
        if (node.isLeaf()) {
            for (int i = node.start; i < node.start + node.count; i++) {
                float t;
                glm::vec3 n;
                if (RaycastTriangle(start, dir, g_sortedTris[i], t, n) && t < closestT) { 
                    closestT = t; 
                    outNormal = n;
                    hit = true; 
                }
            }
        } else {
            if (sp + 2 <= 64) { stack[sp++] = node.left; stack[sp++] = node.right; }
        }
    }
    return hit ? closestT : -1.0f;
}

static float TraceDynamicBoxes(JPH::BodyInterface& bi,
                               const std::vector<JPH::BodyID>& ids,
                               JPH::BodyID ignoreBody,
                               const glm::vec3& start, const glm::vec3& dir,
                               float maxDist)
{
    float best = maxDist;
    bool hit = false;
    for (auto id : ids) {
        if (!ignoreBody.IsInvalid() && id == ignoreBody) continue;
        JPH::RMat44 t = bi.GetWorldTransform(id);
        JPH::AABox aabb = bi.GetShape(id)->GetWorldSpaceBounds(t, JPH::Vec3(1,1,1));
        glm::vec3 bmin((float)aabb.mMin.GetX(), (float)aabb.mMin.GetY(), (float)aabb.mMin.GetZ());
        glm::vec3 bmax((float)aabb.mMax.GetX(), (float)aabb.mMax.GetY(), (float)aabb.mMax.GetZ());
        float tmin, tmax;
        if (RayAABB(start, dir, bmin, bmax, tmin, tmax)) {
            if (tmin >= 0.0f && tmin < best) { best = tmin; hit = true; }
        }
    }
    return hit ? best : -1.0f;
}

// ---------------------------------------------------------------------------
// 敌人定义（提前声明，供 TraceFire 和主循环使用）
// ---------------------------------------------------------------------------
struct EnemyInfo {
    glm::vec3 pos;
    glm::vec3 vel = glm::vec3(0.0f);
    float hp = 100.0f;
    float radius = 20.0f;
    float height = 60.0f;
    bool isGrounded = false;
    float attackTimer = 0.0f;
    enum State { IDLE, CHASE, ATTACK, DEAD } state = CHASE;
    float deathTimer = 0.0f;
};

// ---------------------------------------------------------------------------
// 门对玩家的射线阻挡
// ---------------------------------------------------------------------------
struct DoorStateForTrace {
    glm::vec3 mins, maxs;
    glm::vec3 openDir;
    float openDist;
    float openAmount;
};

static float TraceDoors(const std::vector<DoorStateForTrace>& ds,
                        const glm::vec3& start, const glm::vec3& dir, float maxDist)
{
    float best = maxDist; bool hit = false;
    for (auto& d : ds) {
        if (d.openAmount > 0.5f) continue;
        glm::vec3 off = d.openDir * (d.openDist * d.openAmount);
        glm::vec3 worldMin = d.mins + off;
        glm::vec3 worldMax = d.maxs + off;
        float tmin, tmax;
        if (RayAABB(start, dir, worldMin, worldMax, tmin, tmax)) {
            if (tmin >= 0.0f && tmin < best) { best = tmin; hit = true; }
        }
    }
    return hit ? best : -1.0f;
}

// ---------------------------------------------------------------------------
// 武器开火检测
// type: 0=无, 1=BSP静态, 2=物理箱子, 3=门, 4=敌人
// ---------------------------------------------------------------------------
static float TraceFire(const glm::vec3& start, const glm::vec3& dir,
                       JPH::BodyInterface& bi, const std::vector<JPH::BodyID>& physboxIds,
                       const std::vector<DoorStateForTrace>& doorTrace,
                       const std::vector<EnemyInfo>& enemies,
                       glm::vec3& outNormal, int& outHitType, int& outHitIndex)
{
    float closestDist = 2048.0f;
    outHitType = 0;
    outHitIndex = -1;
    outNormal = glm::vec3(0, 1, 0);

    // 1. BSP 静态几何
    glm::vec3 bspNormal;
    float distBSP = TraceBVH(start, dir, bspNormal);
    if (distBSP >= 0.0f && distBSP < closestDist) {
        closestDist = distBSP;
        outHitType = 1;
        outNormal = bspNormal;
    }

    // 2. 物理箱子
    for (size_t i = 0; i < physboxIds.size(); i++) {
        JPH::RMat44 t = bi.GetWorldTransform(physboxIds[i]);
        JPH::AABox aabb = bi.GetShape(physboxIds[i])->GetWorldSpaceBounds(t, JPH::Vec3(1,1,1));
        glm::vec3 bmin((float)aabb.mMin.GetX(), (float)aabb.mMin.GetY(), (float)aabb.mMin.GetZ());
        glm::vec3 bmax((float)aabb.mMax.GetX(), (float)aabb.mMax.GetY(), (float)aabb.mMax.GetZ());
        
        float tmin, tmax;
        if (RayAABB(start, dir, bmin, bmax, tmin, tmax) && tmin >= 0.0f && tmin < closestDist) {
            closestDist = tmin;
            outHitType = 2;
            outHitIndex = (int)i;
            glm::vec3 hitPos = start + dir * tmin;
            glm::vec3 boxCenter = (bmin + bmax) * 0.5f;
            outNormal = glm::normalize(hitPos - boxCenter);
        }
    }

    // 3. 门
    for (size_t i = 0; i < doorTrace.size(); i++) {
        auto& d = doorTrace[i];
        glm::vec3 off = d.openDir * (d.openDist * d.openAmount);
        glm::vec3 wMin = d.mins + off;
        glm::vec3 wMax = d.maxs + off;
        
        float tmin, tmax;
        if (RayAABB(start, dir, wMin, wMax, tmin, tmax) && tmin >= 0.0f && tmin < closestDist) {
            closestDist = tmin;
            outHitType = 3;
            outHitIndex = (int)i;
            glm::vec3 hitPos = start + dir * tmin;
            glm::vec3 doorCenter = (wMin + wMax) * 0.5f;
            outNormal = glm::normalize(hitPos - doorCenter);
        }
    }

    // 4. 敌人
    for (size_t i = 0; i < enemies.size(); i++) {
        if (enemies[i].state == EnemyInfo::DEAD) continue;
        glm::vec3 bmin = enemies[i].pos - glm::vec3(enemies[i].radius, enemies[i].height * 0.5f, enemies[i].radius);
        glm::vec3 bmax = enemies[i].pos + glm::vec3(enemies[i].radius, enemies[i].height * 0.5f, enemies[i].radius);
        
        float tmin, tmax;
        if (RayAABB(start, dir, bmin, bmax, tmin, tmax) && tmin >= 0.0f && tmin < closestDist) {
            closestDist = tmin;
            outHitType = 4;
            outHitIndex = (int)i;
            glm::vec3 hitPos = start + dir * tmin;
            outNormal = glm::normalize(hitPos - enemies[i].pos);
        }
    }

    return closestDist < 2048.0f ? closestDist : -1.0f;
}

// ============================================================================
// Jolt 物理层过滤器
// ============================================================================
namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};
namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint NUM_LAYERS(2);
};

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }
    virtual JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override { return mObjectToBroadPhase[inLayer]; }
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:     return "MOVING";
            default: return "INVALID";
        }
    }
private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING: return true;
            default: return false;
        }
    }
};

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        switch (inObject1) {
            case Layers::NON_MOVING: return inObject2 == Layers::MOVING;
            case Layers::MOVING: return true;
            default: return false;
        }
    }
};

// ============================================================================
// main
// ============================================================================
int main(int, char**) {
#ifdef _WIN32
    ::SetProcessDPIAware();
#endif
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    // 初始化音频设备
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        printf("SDL_mixer could not initialize! SDL_mixer Error: %s\n", Mix_GetError());
    }    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow("Game + Jolt", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 加载 BSP
    float spawnPos[3] = { 0.0f, 128.0f, -400.0f };
    float spawnAngles[3] = { 0.0f, 0.0f, 0.0f };
    std::ifstream f("map.bsp", std::ios::binary);
    if (!f.is_open()) { printf("Cannot open map.bsp\n"); return 1; }
    char magic[4]; f.read(magic, 4);
    if (strncmp(magic, "MYSB", 4) != 0) return 1;
    uint32_t nv, ni; f.read((char*)&nv, 4); f.read((char*)&ni, 4);
    std::vector<float> verts(nv * 6); f.read((char*)verts.data(), nv * 6 * 4);
    std::vector<uint32_t> indices(ni); f.read((char*)indices.data(), ni * 4);
    if (f.read((char*)spawnPos, sizeof(spawnPos))) f.read((char*)spawnAngles, sizeof(spawnAngles));

    // 读取 Trigger 数据
    uint32_t numTriggers = 0;
    std::vector<glm::vec3> triggerMins, triggerMaxs;
    if (f.read((char*)&numTriggers, 4)) {
        std::vector<float> tb(numTriggers * 6); f.read((char*)tb.data(), tb.size() * 4);
        for (uint32_t i = 0; i < numTriggers; i++) {
            triggerMins.push_back(glm::vec3(tb[i*6+0], tb[i*6+1], tb[i*6+2]));
            triggerMaxs.push_back(glm::vec3(tb[i*6+3], tb[i*6+4], tb[i*6+5]));
        }
    }

    // 读取 Physbox 数据
    uint32_t numPhysboxes = 0;
    std::vector<glm::vec3> physboxMins, physboxMaxs;
    if (f.read((char*)&numPhysboxes, 4)) {
        std::vector<float> pb(numPhysboxes * 6); f.read((char*)pb.data(), pb.size() * 4);
        for (uint32_t i = 0; i < numPhysboxes; i++) {
            physboxMins.push_back(glm::vec3(pb[i*6+0], pb[i*6+1], pb[i*6+2]));
            physboxMaxs.push_back(glm::vec3(pb[i*6+3], pb[i*6+4], pb[i*6+5]));
        }
        printf("Loaded %u physbox zones.\n", numPhysboxes);
    }

    // 读取 door 数据
    struct DoorInfo {
        glm::vec3 mins, maxs;
        glm::vec3 openDir;
        float openDist = 128.0f;
        float speed    = 100.0f;
        float waitTime = 4.0f;
        float   openAmount = 0.0f;
        bool    isOpening  = false;
        float   waitTimer  = 0.0f;
    };
    std::vector<DoorInfo> doors;

    uint32_t numDoors = 0;
    if (f.read((char*)&numDoors, 4)) {
        std::vector<float> db(numDoors * 12);
        f.read((char*)db.data(), db.size() * 4);
        for (uint32_t i = 0; i < numDoors; i++) {
            DoorInfo d;
            d.mins     = glm::vec3(db[i*12+0], db[i*12+1], db[i*12+2]);
            d.maxs     = glm::vec3(db[i*12+3], db[i*12+4], db[i*12+5]);
            d.openDir  = glm::vec3(db[i*12+6], db[i*12+7], db[i*12+8]);
            d.openDist = db[i*12+9];
            d.speed    = db[i*12+10];
            d.waitTime = db[i*12+11];
            doors.push_back(d);
        }
        printf("Loaded %u doors.\n", numDoors);
    }

    f.close();

    // 弹孔数据
    struct BulletHole {
        glm::vec3 pos;
        glm::vec3 normal;
        float life;
    };
    std::vector<BulletHole> bulletHoles;

    // 构建 BVH
    std::vector<CollisionTri> collisionTris;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i], i1 = indices[i+1], i2 = indices[i+2];
        if (i0 >= nv || i1 >= nv || i2 >= nv) continue;
        glm::vec3 v0(verts[i0*6], verts[i0*6+1], verts[i0*6+2]);
        glm::vec3 v1(verts[i1*6], verts[i1*6+1], verts[i1*6+2]);
        glm::vec3 v2(verts[i2*6], verts[i2*6+1], verts[i2*6+2]);
        glm::vec3 n(verts[i0*6+3], verts[i0*6+4], verts[i0*6+5]);
        collisionTris.push_back({v0, v1, v2, n});
    }
    g_sortedTris = collisionTris;
    g_bvh.clear(); g_bvh.reserve(collisionTris.size() * 2 + 1);
    if (!g_sortedTris.empty()) BuildBVH(0, (int)g_sortedTris.size());

    // GPU 上传地图
    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, verts.size() * 4, verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * 4, indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    // Shader
    const char* vs_src = R"(
        #version 330 core
        layout(location=0) in vec3 aPos;
        layout(location=1) in vec3 aNormal;
        uniform mat4 uMVP;
        uniform mat4 uModel;
        out vec3 vNormal;
        void main() { gl_Position = uMVP * vec4(aPos, 1.0); vNormal = mat3(uModel) * aNormal; }
    )";
    const char* fs_src = R"(
        #version 330 core
        in vec3 vNormal;
        out vec4 FragColor;
        uniform vec3 uColor;
        void main() {
            vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
            float diff = max(dot(vNormal, lightDir), 0.0);
            FragColor = vec4(uColor * (0.3 + 0.7 * diff), 1.0); 
        }
    )";
    auto compile_shader = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, nullptr); glCompileShader(s); return s;
    };
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs); glAttachShader(program, fs); glLinkProgram(program);
    glDeleteShader(vs); glDeleteShader(fs);

    // ========================================================================
    // Jolt 初始化
    // ========================================================================
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    JPH::TempAllocatorImpl* temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    JPH::JobSystemThreadPool* job_system = new JPH::JobSystemThreadPool(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

    BPLayerInterfaceImpl bp_layer_interface;
    ObjectVsBroadPhaseLayerFilterImpl obj_vs_bp_filter;
    ObjectLayerPairFilterImpl obj_vs_obj_filter;

    JPH::PhysicsSystem physics;
    physics.Init(1024, 0, 1024, 1024, bp_layer_interface, obj_vs_bp_filter, obj_vs_obj_filter);
    physics.SetGravity(JPH::Vec3(0.0f, -1200.0f, 0.0f)); 

    JPH::BodyInterface& body_interface = physics.GetBodyInterface();

    // BSP 静态 mesh
    {
        JPH::VertexList vertices;
        vertices.reserve(nv);
        for (uint32_t i = 0; i < nv; i++) {
            vertices.push_back(JPH::Float3(verts[i*6+0], verts[i*6+1], verts[i*6+2]));
        }
        JPH::IndexedTriangleList triangles;
        triangles.reserve(ni / 3);
        for (uint32_t i = 0; i + 2 < ni; i += 3) {
            triangles.push_back(JPH::IndexedTriangle(indices[i], indices[i+1], indices[i+2], 0));
        }

        JPH::MeshShapeSettings mesh_settings(std::move(vertices), std::move(triangles));
        JPH::BodyCreationSettings bcs(
            mesh_settings.Create().Get(),
            JPH::RVec3::sZero(),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Static,
            Layers::NON_MOVING);
        bcs.mEnhancedInternalEdgeRemoval = true;
        body_interface.CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
    }

    // 简易静态地板
    {
        JPH::BoxShapeSettings floor_shape(JPH::Vec3(1024.0f, 8.0f, 1024.0f));
        JPH::BodyCreationSettings floor_settings(floor_shape.Create().Get(), JPH::RVec3(0, -8, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);
        body_interface.CreateAndAddBody(floor_settings, JPH::EActivation::DontActivate);
    }

    // 玩家的 Kinematic 刚体
    JPH::BodyID player_body;
    {
        JPH::BoxShapeSettings player_shape(JPH::Vec3(16.0f, 36.0f, 16.0f));
        JPH::BodyCreationSettings player_settings(
            player_shape.Create().Get(),
            JPH::RVec3(spawnPos[0], spawnPos[1] + 36.0f, spawnPos[2]),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Kinematic,
            Layers::MOVING
        );
        player_settings.mFriction = 0.0f;
        player_settings.mRestitution = 0.0f;
        player_settings.mAllowSleeping = false;
        player_body = body_interface.CreateAndAddBody(player_settings, JPH::EActivation::Activate);
    }

    // 为每个 func_physbox 创建 Jolt 动态刚体
    std::vector<JPH::BodyID> physboxIds;
    for (size_t i = 0; i < physboxMins.size(); i++) {
        glm::vec3 mn = physboxMins[i];
        glm::vec3 mx = physboxMaxs[i];
        glm::vec3 center = (mn + mx) * 0.5f;
        glm::vec3 halfSize = (mx - mn) * 0.5f;

        JPH::BoxShapeSettings box_shape(JPH::Vec3(halfSize.x, halfSize.y, halfSize.z));
        JPH::BodyCreationSettings box_settings(
            box_shape.Create().Get(),
            JPH::RVec3(center.x, center.y, center.z),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Dynamic,
            Layers::MOVING
        );
        box_settings.mLinearDamping = 0.2f;
        box_settings.mFriction = 1.0f;
        JPH::BodyID id = body_interface.CreateAndAddBody(box_settings, JPH::EActivation::Activate);
        physboxIds.push_back(id);
    }
    printf("Created %zu physboxes in Jolt.\n", physboxIds.size());

    // 箱子可视化网格
    GLuint box_vao, box_vbo, box_ebo;
    glGenVertexArrays(1, &box_vao); glGenBuffers(1, &box_vbo); glGenBuffers(1, &box_ebo);

    std::vector<float> box_verts = {
        -1,-1,-1, 0,0,-1,   1,-1,-1, 0,0,-1,   1, 1,-1, 0,0,-1,  -1, 1,-1, 0,0,-1,
        -1,-1, 1, 0,0, 1,   1,-1, 1, 0,0, 1,   1, 1, 1, 0,0, 1,  -1, 1, 1, 0,0, 1,
        -1,-1,-1, -1,0,0,  -1,-1, 1, -1,0,0,  -1, 1, 1, -1,0,0,  -1, 1,-1, -1,0,0,
         1,-1,-1,  1,0,0,   1,-1, 1,  1,0,0,   1, 1, 1,  1,0,0,   1, 1,-1,  1,0,0,
        -1, 1,-1, 0,1,0,   1, 1,-1, 0,1,0,   1, 1, 1, 0,1,0,  -1, 1, 1, 0,1,0,
        -1,-1,-1, 0,-1,0,  -1,-1, 1, 0,-1,0,  1,-1, 1, 0,-1,0,  1,-1,-1, 0,-1,0,
    };
    std::vector<uint32_t> box_idx = {
        0,1,2, 0,2,3,        // -Z
        4,5,6, 4,6,7,        // +Z
        8,9,10, 8,10,11,     // -X
        12,13,14, 12,14,15,  // +X
        16,17,18, 16,18,19,  // +Y
        20,21,22, 20,22,23,  // -Y
    };
    glBindVertexArray(box_vao);
    glBindBuffer(GL_ARRAY_BUFFER, box_vbo); glBufferData(GL_ARRAY_BUFFER, box_verts.size() * 4, box_verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, box_ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, box_idx.size() * 4, box_idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    // 生成一个简单的圆柱体网格（用于画枪管）
    GLuint gun_vao, gun_vbo, gun_ebo;
    glGenVertexArrays(1, &gun_vao); glGenBuffers(1, &gun_vbo); glGenBuffers(1, &gun_ebo);
    
    std::vector<float> gun_verts;
    std::vector<uint32_t> gun_idx;
    int segments = 16;
    float radius = 0.1f;
    float length = 1.0f;
    
    // 侧面顶点
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159265f;
        float x = cosf(angle) * radius;
        float y = sinf(angle) * radius;
        float nx = cosf(angle);
        float ny = sinf(angle);
        // 前面的圈
        gun_verts.insert(gun_verts.end(), {x, y, -length, nx, ny, 0.0f});
        // 后面的圈
        gun_verts.insert(gun_verts.end(), {x, y, 0.0f, nx, ny, 0.0f});
    }
    // 侧面索引
    for (int i = 0; i < segments; i++) {
        int base = i * 2;
        gun_idx.insert(gun_idx.end(), {(uint32_t)base, (uint32_t)base+1, (uint32_t)base+2});
        gun_idx.insert(gun_idx.end(), {(uint32_t)base+1, (uint32_t)base+3, (uint32_t)base+2});
    }
    
    glBindVertexArray(gun_vao);
    glBindBuffer(GL_ARRAY_BUFFER, gun_vbo); glBufferData(GL_ARRAY_BUFFER, gun_verts.size() * 4, gun_verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gun_ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, gun_idx.size() * 4, gun_idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    // 为每个门创建 Kinematic 刚体
    std::vector<JPH::BodyID> doorBodies;
    for (size_t i = 0; i < doors.size(); i++) {
        glm::vec3 center = (doors[i].mins + doors[i].maxs) * 0.5f;
        glm::vec3 halfSize = (doors[i].maxs - doors[i].mins) * 0.5f;
        JPH::BoxShapeSettings doorShape(JPH::Vec3(halfSize.x, halfSize.y, halfSize.z));
        JPH::BodyCreationSettings doorBCS(
            doorShape.Create().Get(),
            JPH::RVec3(center.x, center.y, center.z),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Kinematic,
            Layers::MOVING);
        doorBCS.mFriction = 0.5f;
        doorBodies.push_back(body_interface.CreateAndAddBody(doorBCS, JPH::EActivation::Activate));
    }

    // 加载音效
    Mix_Chunk* sndShoot = Mix_LoadWAV("sounds/shoot.wav");
    Mix_Chunk* sndHit   = Mix_LoadWAV("sounds/hit.wav");
    Mix_Chunk* sndPain  = Mix_LoadWAV("sounds/pain.wav");
    Mix_Chunk* sndDoor  = Mix_LoadWAV("sounds/door.wav");
    
    // 如果找不到文件，会返回 nullptr，不会崩溃，只是没声音

    // 玩家状态
    glm::vec3 playerPos(spawnPos[0], spawnPos[1] + 36.0f, spawnPos[2]);
    glm::vec3 playerVel(0.0f);
    float camYaw = spawnAngles[1];
    float camPitch = -spawnAngles[0];
    const float playerRadius = 16.0f, playerHeight = 72.0f, gravity = 800.0f, jumpSpeed = 320.0f, moveSpeed = 300.0f, mouseSensitivity = 0.1f;
    bool isGrounded = false, mouseLocked = true;
    SDL_SetRelativeMouseMode(SDL_TRUE);
    bool running = true;
    Uint32 lastTime = SDL_GetTicks();

    float recoilTimer = 0.0f; // 后坐力动画计时器
    float recoilDuration = 0.1f; // 动画持续 0.1 秒

    std::vector<DoorStateForTrace> doorTrace;

    // 准星命中反馈计时器
    float crosshairHitTimer = 0.0f;

    // ---- 敌人初始化 ----
    std::vector<EnemyInfo> enemies;
    enemies.push_back({glm::vec3(-200.0f, 36.0f, 200.0f)});
    enemies.push_back({glm::vec3(200.0f, 36.0f, 200.0f)});

    float playerHP = 100.0f;
    float playerDamageTimer = 0.0f;

    while (running) {
        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        if (deltaTime > 0.05f) deltaTime = 0.05f;
        lastTime = currentTime;

        bool bUsePressed = false;
        bool bFirePressed = false;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                if (mouseLocked) { mouseLocked = false; SDL_SetRelativeMouseMode(SDL_FALSE); }
                else running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_e && mouseLocked && !event.key.repeat) {
                bUsePressed = true;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                if (!mouseLocked) { mouseLocked = true; SDL_SetRelativeMouseMode(SDL_TRUE); }
                else if (!event.button.clicks || event.button.clicks == 1) { // 防连点
                    bFirePressed = true;
                }
            }
            if (event.type == SDL_MOUSEMOTION && mouseLocked) {
                camYaw += event.motion.xrel * mouseSensitivity;
                camPitch -= event.motion.yrel * mouseSensitivity;
                if (camPitch > 89.0f) camPitch = 89.0f;
                if (camPitch < -89.0f) camPitch = -89.0f;
            }
        }

        // 移动输入
        if (mouseLocked && !io.WantCaptureKeyboard) {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            float yawRad = glm::radians(camYaw);
            glm::vec3 forward(cos(yawRad), 0.0f, sin(yawRad));
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
            glm::vec3 wishDir(0.0f);
            if (keys[SDL_SCANCODE_W]) wishDir += forward;
            if (keys[SDL_SCANCODE_S]) wishDir -= forward;
            if (keys[SDL_SCANCODE_D]) wishDir += right;
            if (keys[SDL_SCANCODE_A]) wishDir -= right;
            if (glm::length(wishDir) > 0.0f) {
                wishDir = glm::normalize(wishDir);
                playerVel.x = wishDir.x * moveSpeed;
                playerVel.z = wishDir.z * moveSpeed;
            } else {
                playerVel.x = 0.0f; playerVel.z = 0.0f;
            }
            if (keys[SDL_SCANCODE_SPACE] && isGrounded) { playerVel.y = jumpSpeed; isGrounded = false; }
        } else {
            playerVel.x = 0.0f; playerVel.z = 0.0f;
        }

        playerVel.y -= gravity * deltaTime;
        isGrounded = false;

        doorTrace.clear();
        doorTrace.reserve(doors.size());
        for (auto& d : doors) {
            doorTrace.push_back({d.mins, d.maxs, d.openDir, d.openDist, d.openAmount});
        }

        // 垂直碰撞
        glm::vec3 moveY(0.0f, playerVel.y * deltaTime, 0.0f);
        glm::vec3 newPosY = playerPos + moveY;

        glm::vec3 dummyNormal;
        float floorDist = TraceBVH(newPosY, glm::vec3(0.0f, -1.0f, 0.0f), dummyNormal);
        {
            float dynDown = TraceDynamicBoxes(body_interface, physboxIds, player_body,
                                              newPosY, glm::vec3(0.0f, -1.0f, 0.0f),
                                              playerHeight);
            if (dynDown >= 0.0f && (floorDist < 0.0f || dynDown < floorDist)) floorDist = dynDown;
        }
        if (floorDist != -1.0f && floorDist <= (playerHeight * 0.5f + 0.5f)) {
            newPosY.y -= (floorDist - playerHeight * 0.5f); playerVel.y = 0.0f;
            if (moveY.y <= 0.0f) isGrounded = true;
        }

        float ceilDist = TraceBVH(newPosY, glm::vec3(0.0f, 1.0f, 0.0f), dummyNormal);
        {
            float dynUp = TraceDynamicBoxes(body_interface, physboxIds, player_body,
                                            newPosY, glm::vec3(0.0f, 1.0f, 0.0f),
                                            playerHeight);
            if (dynUp >= 0.0f && (ceilDist < 0.0f || dynUp < ceilDist)) ceilDist = dynUp;
        }
        if (ceilDist != -1.0f && ceilDist <= (playerHeight * 0.5f + 0.5f)) {
            newPosY.y += (ceilDist - playerHeight * 0.5f); playerVel.y = 0.0f;
        }
        playerPos = newPosY;

        // 水平碰撞 X
        float dx = playerVel.x * deltaTime;
        if (fabsf(dx) > 0.0001f) {
            glm::vec3 dirX(dx > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
            float distX = TraceBVH(playerPos, dirX, dummyNormal);
            {
                float dynX = TraceDynamicBoxes(body_interface, physboxIds, player_body,
                                               playerPos, dirX, playerRadius);
                if (dynX >= 0.0f && (distX < 0.0f || dynX < distX)) distX = dynX;
            }
            {
                float doorX = TraceDoors(doorTrace, playerPos, dirX, playerRadius);
                if (doorX >= 0.0f && (distX < 0.0f || doorX < distX)) distX = doorX;
            }
            if (distX != -1.0f && distX < playerRadius) { playerPos.x += (distX - playerRadius) * dirX.x; playerVel.x = 0.0f; }
            else { playerPos.x += dx; }
        }
        // 水平碰撞 Z
        float dz = playerVel.z * deltaTime;
        if (fabsf(dz) > 0.0001f) {
            glm::vec3 dirZ(0.0f, 0.0f, dz > 0.0f ? 1.0f : -1.0f);
            float distZ = TraceBVH(playerPos, dirZ, dummyNormal);
            {
                float dynZ = TraceDynamicBoxes(body_interface, physboxIds, player_body,
                                               playerPos, dirZ, playerRadius);
                if (dynZ >= 0.0f && (distZ < 0.0f || dynZ < distZ)) distZ = dynZ;
            }
            {
                float doorZ = TraceDoors(doorTrace, playerPos, dirZ, playerRadius);
                if (doorZ >= 0.0f && (distZ < 0.0f || doorZ < distZ)) distZ = doorZ;
            }
            if (distZ != -1.0f && distZ < playerRadius) { playerPos.z += (distZ - playerRadius) * dirZ.z; playerVel.z = 0.0f; }
            else { playerPos.z += dz; }
        }

        // 把玩家位置同步给 Jolt 刚体
        body_interface.SetPositionAndRotation(
            player_body,
            JPH::RVec3(playerPos.x, playerPos.y, playerPos.z),
            JPH::Quat::sIdentity(),
            JPH::EActivation::Activate
        );

        // ====================================================================
        // 敌人 AI 逻辑
        // ====================================================================
        for (auto& enemy : enemies) {
            if (enemy.state == EnemyInfo::DEAD) {
                enemy.deathTimer += deltaTime;
                continue;
            }

            glm::vec3 toPlayer = playerPos - enemy.pos;
            float distToPlayer = glm::length(toPlayer);
            
            // 1. 状态切换
            if (distToPlayer < 60.0f) enemy.state = EnemyInfo::ATTACK;
            else if (distToPlayer < 800.0f) enemy.state = EnemyInfo::CHASE;
            else enemy.state = EnemyInfo::IDLE;

            // 2. 执行状态
            if (enemy.state == EnemyInfo::CHASE) {
                glm::vec3 dir = glm::normalize(glm::vec3(toPlayer.x, 0, toPlayer.z));
                enemy.vel.x = dir.x * 150.0f; // 敌人速度 150
                enemy.vel.z = dir.z * 150.0f;
            } else if (enemy.state == EnemyInfo::ATTACK) {
                enemy.vel.x = 0.0f;
                enemy.vel.z = 0.0f;
                enemy.attackTimer -= deltaTime;
                if (enemy.attackTimer <= 0.0f) {
                    playerHP -= 10.0f;
                    playerDamageTimer = 0.2f;
                    enemy.attackTimer = 1.0f; // 1秒咬一口
                    if (sndPain) Mix_PlayChannel(-1, sndPain, 0); // 播放受伤声
                    printf("[AI] Enemy attacked player! HP: %.1f\n", playerHP);
                }
            } else {
                enemy.vel.x = 0.0f;
                enemy.vel.z = 0.0f;
            }

            // 3. 重力与碰撞
            enemy.vel.y -= 800.0f * deltaTime;
            glm::vec3 newPos = enemy.pos + enemy.vel * deltaTime;

            // 垂直碰撞 (脚底往下打射线)
            float eFloorDist = TraceBVH(newPos - glm::vec3(0, enemy.height*0.5f, 0), glm::vec3(0, -1, 0), dummyNormal);
            if (eFloorDist >= 0.0f && eFloorDist <= 1.5f) {
                newPos.y -= (eFloorDist - 1.0f);
                enemy.vel.y = 0.0f;
                enemy.isGrounded = true;
            } else {
                enemy.isGrounded = false;
            }

            // 水平碰撞
            float edx = enemy.vel.x * deltaTime;
            float edz = enemy.vel.z * deltaTime;
            if (fabsf(edx) > 0.0001f) {
                glm::vec3 dirX(edx > 0 ? 1.0f : -1.0f, 0, 0);
                float d = TraceBVH(newPos, dirX, dummyNormal);
                if (d < 0 || d > enemy.radius) newPos.x += edx;
                else enemy.vel.x = 0;
            }
            if (fabsf(edz) > 0.0001f) {
                glm::vec3 dirZ(0, 0, edz > 0 ? 1.0f : -1.0f);
                float d = TraceBVH(newPos, dirZ, dummyNormal);
                if (d < 0 || d > enemy.radius) newPos.z += edz;
                else enemy.vel.z = 0;
            }

            enemy.pos = newPos;
        }

        if (playerDamageTimer > 0.0f) playerDamageTimer -= deltaTime;

        if (playerHP <= 0.0f) {
            printf("Player Died! Respawning...\n");
            playerHP = 100.0f;
            playerPos = glm::vec3(spawnPos[0], spawnPos[1] + 36.0f, spawnPos[2]);
            playerVel = glm::vec3(0);
        }

        // ====================================================================
        // 武器射击
        // ====================================================================
        if (bFirePressed) {
            recoilTimer = recoilDuration; // 触发射击后坐力
            if (sndShoot) Mix_PlayChannel(-1, sndShoot, 0); // 播放开枪声
            glm::vec3 eyePos = playerPos + glm::vec3(0.0f, 8.0f, 0.0f);
            float pitchRad = glm::radians(camPitch);
            glm::vec3 front;
            front.x = cos(glm::radians(camYaw)) * cos(pitchRad);
            front.y = sin(pitchRad);
            front.z = sin(glm::radians(camYaw)) * cos(pitchRad);
            front = glm::normalize(front);

            glm::vec3 hitNormal;
            int hitType = 0, hitIndex = -1;
            float hitDist = TraceFire(eyePos, front, body_interface, physboxIds, doorTrace, enemies, hitNormal, hitType, hitIndex);

            if (hitDist >= 0.0f) {
                glm::vec3 hitPos = eyePos + front * hitDist;
                
                // 添加弹孔，沿法线稍微往外推一点防 Z-fighting
                bulletHoles.push_back({hitPos + hitNormal * 0.1f, hitNormal, 10.0f});
                
                // 准星反馈
                crosshairHitTimer = 0.15f;

                // 打飞物理箱子
                if (hitType == 2 && hitIndex >= 0) {
                    body_interface.ActivateBody(physboxIds[hitIndex]);
                    
                    float shootForce = 800.0f;
                    JPH::Vec3 newV(
                        front.x * shootForce, 
                        200.0f,
                        front.z * shootForce
                    );
                    body_interface.SetLinearVelocity(physboxIds[hitIndex], newV);
                    
                    printf("[WEAPON] Hit box %d! Applied velocity.\n", hitIndex);
                }
                // 打中敌人
                else if (hitType == 4 && hitIndex >= 0) {
                    enemies[hitIndex].hp -= 34.0f; // 3枪打死
                    if (sndHit) Mix_PlayChannel(-1, sndHit, 0); // 播放命中声
                    printf("[WEAPON] Hit enemy %d! HP left: %.1f\n", hitIndex, enemies[hitIndex].hp);
                    if (enemies[hitIndex].hp <= 0.0f) {
                        enemies[hitIndex].state = EnemyInfo::DEAD;
                        printf("[WEAPON] Enemy %d is DEAD!\n", hitIndex);
                    }
                }
            }
        }

        // 弹孔寿命更新
        for (auto it = bulletHoles.begin(); it != bulletHoles.end(); ) {
            it->life -= deltaTime;
            if (it->life <= 0.0f) it = bulletHoles.erase(it);
            else ++it;
        }

        // 推箱子
        {
            glm::vec3 playerVelH(playerVel.x, 0.0f, playerVel.z);
            float playerSpeedH = glm::length(playerVelH);
            for (size_t i = 0; i < physboxIds.size(); i++) {
                JPH::RVec3 box_pos = body_interface.GetCenterOfMassPosition(physboxIds[i]);
                glm::vec3 bpos((float)box_pos.GetX(), (float)box_pos.GetY(), (float)box_pos.GetZ());

                glm::vec3 diff = playerPos - bpos;
                diff.y = 0.0f;
                float horizontalDist = glm::length(diff);

                glm::vec3 halfSize = (physboxMaxs[i] - physboxMins[i]) * 0.5f;
                float contactRange = playerRadius + std::max(halfSize.x, halfSize.z) + 6.0f;

                if (horizontalDist < contactRange && playerSpeedH > 0.01f) {
                    glm::vec3 pushDir = glm::normalize(-diff);
                    float dot = glm::dot(glm::normalize(playerVelH), pushDir);
                    if (dot > 0.1f) {
                        JPH::Vec3 v = body_interface.GetLinearVelocity(physboxIds[i]);
                        glm::vec3 boxVel(v.GetX(), 0.0f, v.GetZ());
                        float boxSpeedAlongPush = glm::dot(boxVel, pushDir);
                        float desiredSpeed = playerSpeedH * dot * 0.8f;
                        if (boxSpeedAlongPush < desiredSpeed) {
                            float add = desiredSpeed - boxSpeedAlongPush;
                            JPH::Vec3 newV(v.GetX() + pushDir.x * add,
                                           v.GetY(),
                                           v.GetZ() + pushDir.z * add);
                            body_interface.SetLinearVelocity(physboxIds[i], newV);
                            body_interface.ActivateBody(physboxIds[i]);
                        }
                    }
                }
            }
        }

        // Jolt 物理更新
        physics.Update(deltaTime, 1, temp_allocator, job_system);

        // Use 交互
        if (bUsePressed) {
            glm::vec3 eyePos = playerPos + glm::vec3(0.0f, 8.0f, 0.0f);
            float pitchRad2 = glm::radians(camPitch);
            glm::vec3 front2;
            front2.x = cos(glm::radians(camYaw)) * cos(pitchRad2);
            front2.y = sin(pitchRad2);
            front2.z = sin(glm::radians(camYaw)) * cos(pitchRad2);
            front2 = glm::normalize(front2);

            float bestT = 64.0f;
            int   bestDoor = -1;
            for (size_t i = 0; i < doors.size(); i++) {
                glm::vec3 off = doors[i].openDir * (doors[i].openDist * doors[i].openAmount);
                glm::vec3 wMin = doors[i].mins + off;
                glm::vec3 wMax = doors[i].maxs + off;
                float tmin, tmax;
                if (RayAABB(eyePos, front2, wMin, wMax, tmin, tmax)) {
                    if (tmin >= 0.0f && tmin < bestT) { bestT = tmin; bestDoor = (int)i; }
                }
            }
            if (bestDoor >= 0 && doors[bestDoor].openAmount < 0.01f && !doors[bestDoor].isOpening) {
                doors[bestDoor].isOpening = true;
                doors[bestDoor].waitTimer = 0.0f;
                if (sndDoor) Mix_PlayChannel(-1, sndDoor, 0); // 播放开门声
                printf("[USE] door %d opened\n", bestDoor);
            }
        }

        // 门动画推进
        for (auto& d : doors) {
            float move = (d.speed * deltaTime) / std::max(d.openDist, 1.0f);
            if (d.isOpening) {
                d.openAmount += move;
                if (d.openAmount >= 1.0f) {
                    d.openAmount = 1.0f;
                    d.isOpening = false;
                    d.waitTimer = d.waitTime;
                }
            } else if (d.waitTimer > 0.0f) {
                d.waitTimer -= deltaTime;
            } else if (d.openAmount > 0.0f) {
                d.openAmount -= move;
                if (d.openAmount <= 0.0f) d.openAmount = 0.0f;
            }
        }

        // 同步门 Kinematic 位置
        for (size_t i = 0; i < doors.size(); i++) {
            if (i >= doorBodies.size()) break;
            glm::vec3 center = (doors[i].mins + doors[i].maxs) * 0.5f
                             + doors[i].openDir * (doors[i].openDist * doors[i].openAmount);
            body_interface.SetPositionAndRotation(
                doorBodies[i],
                JPH::RVec3(center.x, center.y, center.z),
                JPH::Quat::sIdentity(),
                JPH::EActivation::Activate);
        }

        if (crosshairHitTimer > 0.0f) crosshairHitTimer -= deltaTime;
        if (recoilTimer > 0.0f) recoilTimer -= deltaTime;

        // 渲染
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Pos: %.1f, %.1f, %.1f", playerPos.x, playerPos.y, playerPos.z);
        ImGui::Text("Grounded: %s", isGrounded ? "YES" : "NO");
        ImGui::Text("Tris: %zu BVH nodes: %zu", g_sortedTris.size(), g_bvh.size());
        ImGui::Text("Physboxes: %zu", physboxIds.size());
        ImGui::Text("Doors: %zu", doors.size());
        ImGui::Text("Enemies: %zu", enemies.size());
        ImGui::End();

        int scrW, scrH; SDL_GetWindowSize(window, &scrW, &scrH);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)scrW, (float)scrH));
        ImGui::Begin("##crosshair", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoBackground);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 center((float)scrW * 0.5f, (float)scrH * 0.5f);
        
        // 准星颜色反馈
        ImU32 crossColor = (crosshairHitTimer > 0.0f) ? IM_COL32(255, 50, 50, 255) : IM_COL32(255, 255, 255, 255);
        drawList->AddLine(ImVec2(center.x - 5, center.y), ImVec2(center.x + 5, center.y), crossColor, 2.0f);
        drawList->AddLine(ImVec2(center.x, center.y - 5), ImVec2(center.x, center.y + 5), crossColor, 2.0f);

        // 玩家受击红屏反馈
        if (playerDamageTimer > 0.0f) {
            drawList->AddRectFilled(ImVec2(0, 0), ImVec2((float)scrW, (float)scrH), IM_COL32(255, 0, 0, (int)(playerDamageTimer * 150.0f / 0.2f)));
        }

        // 左下角血量
        char hpText[64];
        snprintf(hpText, sizeof(hpText), "HP: %.0f", playerHP);
        drawList->AddText(ImGui::GetFont(), 32.0f, ImVec2(30, scrH - 60), IM_COL32(255, 255, 255, 255), hpText);

        ImGui::End();

        ImGui::Render();

        glViewport(0, 0, scrW, scrH);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);

        glm::mat4 projection = glm::perspective(glm::radians(90.0f), (float)scrW / (float)scrH, 0.1f, 5000.0f);
        float pitchRad = glm::radians(camPitch);
        glm::vec3 front;
        front.x = cos(glm::radians(camYaw)) * cos(pitchRad);
        front.y = sin(pitchRad);
        front.z = sin(glm::radians(camYaw)) * cos(pitchRad);
        glm::vec3 eyePos = playerPos + glm::vec3(0.0f, 8.0f, 0.0f);
        glm::mat4 view = glm::lookAt(eyePos, eyePos + glm::normalize(front), glm::vec3(0, 1, 0));
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 mvp = projection * view * model;
        
        // 渲染地图
        glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform3f(glGetUniformLocation(program, "uColor"), 0.6f, 0.6f, 0.65f); // 地图颜色
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, ni, GL_UNSIGNED_INT, 0);

        // 渲染物理箱子
        glUniform3f(glGetUniformLocation(program, "uColor"), 0.6f, 0.4f, 0.2f); // 箱子颜色偏黄
        for (size_t i = 0; i < physboxIds.size(); i++) {
            JPH::RMat44 box_transform = body_interface.GetWorldTransform(physboxIds[i]);
            JPH::Vec3 t = box_transform.GetTranslation();
            JPH::Quat q = box_transform.GetQuaternion();
            glm::vec3 box_pos((float)t.GetX(), (float)t.GetY(), (float)t.GetZ());
            glm::quat box_quat((float)q.GetW(), (float)q.GetX(), (float)q.GetY(), (float)q.GetZ());

            glm::vec3 halfSize = (physboxMaxs[i] - physboxMins[i]) * 0.5f;

            glm::mat4 box_model = glm::translate(glm::mat4(1.0f), box_pos) * glm::mat4_cast(box_quat) * glm::scale(glm::mat4(1.0f), halfSize);
            glm::mat4 box_mvp = projection * view * box_model;
            glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, glm::value_ptr(box_mvp));
            glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, glm::value_ptr(box_model));
            glBindVertexArray(box_vao);
            glDrawElements(GL_TRIANGLES, (GLsizei)box_idx.size(), GL_UNSIGNED_INT, 0);
        }

        // 渲染门
        glUniform3f(glGetUniformLocation(program, "uColor"), 0.2f, 0.6f, 0.8f); // 门颜色偏蓝
        for (auto& d : doors) {
            glm::vec3 center = (d.mins + d.maxs) * 0.5f + d.openDir * (d.openDist * d.openAmount);
            glm::vec3 halfSize = (d.maxs - d.mins) * 0.5f;
            glm::mat4 doorModel = glm::translate(glm::mat4(1.0f), center)
                                * glm::scale(glm::mat4(1.0f), halfSize);
            glm::mat4 doorMvp = projection * view * doorModel;
            glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, glm::value_ptr(doorMvp));
            glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, glm::value_ptr(doorModel));
            glBindVertexArray(box_vao);
            glDrawElements(GL_TRIANGLES, (GLsizei)box_idx.size(), GL_UNSIGNED_INT, 0);
        }

        // 渲染敌人
        for (auto& enemy : enemies) {
            if (enemy.state == EnemyInfo::DEAD && enemy.deathTimer > 10.0f) continue; // 死后10秒消失
            
            if (enemy.state == EnemyInfo::DEAD) {
                glUniform3f(glGetUniformLocation(program, "uColor"), 0.2f, 0.1f, 0.1f);
            } else {
                glUniform3f(glGetUniformLocation(program, "uColor"), 0.8f, 0.2f, 0.2f); // 活着红色
            }

            glm::vec3 halfSize(enemy.radius, enemy.height * 0.5f, enemy.radius);
            glm::mat4 enemyModel = glm::translate(glm::mat4(1.0f), enemy.pos) 
                                 * glm::scale(glm::mat4(1.0f), halfSize);
            
            if (enemy.state == EnemyInfo::DEAD) {
                // 死亡倒地效果：躺平 + 压扁
                enemyModel = glm::translate(glm::mat4(1.0f), enemy.pos)
                           * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0))
                           * glm::scale(glm::mat4(1.0f), glm::vec3(enemy.radius, enemy.radius, enemy.height * 0.5f));
            }

            glm::mat4 enemyMvp = projection * view * enemyModel;
            glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, glm::value_ptr(enemyMvp));
            glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, glm::value_ptr(enemyModel));
            glBindVertexArray(box_vao);
            glDrawElements(GL_TRIANGLES, (GLsizei)box_idx.size(), GL_UNSIGNED_INT, 0);
        }

        // 渲染弹孔
        glUniform3f(glGetUniformLocation(program, "uColor"), 0.1f, 0.1f, 0.1f); // 弹孔纯黑
        for (auto& bh : bulletHoles) {
            glm::mat4 bulletModel = glm::translate(glm::mat4(1.0f), bh.pos)
                                  * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f)); // 极小方块
            glm::mat4 bulletMvp = projection * view * bulletModel;
            glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, glm::value_ptr(bulletMvp));
            glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, glm::value_ptr(bulletModel));
            glBindVertexArray(box_vao);
            glDrawElements(GL_TRIANGLES, (GLsizei)box_idx.size(), GL_UNSIGNED_INT, 0);
        }

        // ====================================================================
        // 渲染第一人称武器（枪）
        // ====================================================================
        // 用独立的投影矩阵，视野窄一点，保证枪不穿模
        glm::mat4 gunProj = glm::perspective(glm::radians(60.0f), (float)scrW / (float)scrH, 0.01f, 10.0f);
        glm::mat4 gunView = glm::mat4(1.0f); // 视角固定在原点

        // 基础位置：右下方，腰间
        float baseX = 0.25f;
        float baseY = -0.25f;
        float baseZ = -0.4f;

        // 后坐力偏移计算
        float recoilOffset = 0.0f;
        float recoilPitch = 0.0f;
        if (recoilTimer > 0.0f) {
            float progress = recoilTimer / recoilDuration; // 1.0 -> 0.0
            // 模拟枪托向后猛推，枪口向上抬
            recoilOffset = progress * 0.1f; // 向后移动
            recoilPitch = progress * 15.0f; // 向上抬
        }

        // 组合枪的模型矩阵
        glm::mat4 gunModel = glm::translate(glm::mat4(1.0f), glm::vec3(baseX, baseY, baseZ + recoilOffset));
        gunModel = glm::rotate(gunModel, glm::radians(recoilPitch), glm::vec3(1, 0, 0)); // 枪口上抬
        gunModel = glm::rotate(gunModel, glm::radians(-5.0f), glm::vec3(0, 1, 0));       // 稍微偏转一点
        gunModel = glm::scale(gunModel, glm::vec3(0.1f, 0.1f, 0.4f));                    // 拉伸成细长形

        glm::mat4 gunMvp = gunProj * gunView * gunModel;

        // 渲染枪
        glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, glm::value_ptr(gunMvp));
        glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, glm::value_ptr(gunModel));
        glUniform3f(glGetUniformLocation(program, "uColor"), 0.2f, 0.2f, 0.2f); // 枪身深灰色
        glBindVertexArray(gun_vao);
        glDrawElements(GL_TRIANGLES, (GLsizei)gun_idx.size(), GL_UNSIGNED_INT, 0);

        // 为了避免枪的深度值污染主场景（虽然清空了深度缓冲区，但安全起见）
        // 其实因为 ImGui 是 2D 的，所以直接画在上面就行
        // ====================================================================

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // 清理
    for (auto id : physboxIds) {
        body_interface.RemoveBody(id);
        body_interface.DestroyBody(id);
    }
    for (auto id : doorBodies) {
        body_interface.RemoveBody(id);
        body_interface.DestroyBody(id);
    }
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance; JPH::Factory::sInstance = nullptr;
    delete job_system; delete temp_allocator;

    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(); SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window); 
    // 在 SDL_Quit() 之前
    Mix_FreeChunk(sndShoot);
    Mix_FreeChunk(sndHit);
    Mix_FreeChunk(sndPain);
    Mix_FreeChunk(sndDoor);
    Mix_CloseAudio();
        
    SDL_Quit();
    return 0;
}