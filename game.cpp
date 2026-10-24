#include <SDL2/SDL.h>
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

static bool RaycastTriangle(const glm::vec3& orig, const glm::vec3& dir, const CollisionTri& tri, float& t) {
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
    return t > EPSILON;
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

static float TraceBVH(const glm::vec3& start, const glm::vec3& dir) {
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
                if (RaycastTriangle(start, dir, g_sortedTris[i], t) && t < closestT) { closestT = t; hit = true; }
            }
        } else {
            if (sp + 2 <= 64) { stack[sp++] = node.left; stack[sp++] = node.right; }
        }
    }
    return hit ? closestT : -1.0f;
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
    uint32_t numTriggers = 0;
    std::vector<glm::vec3> triggerMins, triggerMaxs;
    if (f.read((char*)&numTriggers, 4)) {
        std::vector<float> tb(numTriggers * 6); f.read((char*)tb.data(), tb.size() * 4);
        for (uint32_t i = 0; i < numTriggers; i++) {
            triggerMins.push_back(glm::vec3(tb[i*6+0], tb[i*6+1], tb[i*6+2]));
            triggerMaxs.push_back(glm::vec3(tb[i*6+3], tb[i*6+4], tb[i*6+5]));
        }
    }
    f.close();

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
        void main() {
            vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
            float diff = max(dot(vNormal, lightDir), 0.0);
            FragColor = vec4(vec3(0.6, 0.6, 0.65) * (0.3 + 0.7 * diff), 1.0); 
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

    JPH::BodyInterface& body_interface = physics.GetBodyInterface();

    // 简易静态地板
    {
        JPH::BoxShapeSettings floor_shape(JPH::Vec3(1024.0f, 8.0f, 1024.0f));
        JPH::BodyCreationSettings floor_settings(floor_shape.Create().Get(), JPH::RVec3(0, -8, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);
        body_interface.CreateAndAddBody(floor_settings, JPH::EActivation::DontActivate);
    }

    // 玩家的 Kinematic 刚体（用于推箱子）
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

    // 动态箱子
    JPH::BodyID box_id;
    {
        JPH::BoxShapeSettings box_shape(JPH::Vec3(16, 16, 16));
        JPH::BodyCreationSettings box_settings(box_shape.Create().Get(), JPH::RVec3(spawnPos[0] + 100.0f, spawnPos[1], spawnPos[2]), JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, Layers::MOVING);
        box_settings.mLinearDamping = 0.2f;
        box_settings.mFriction = 0.5f;
        box_id = body_interface.CreateAndAddBody(box_settings, JPH::EActivation::Activate);
    }

    // 箱子可视化网格
    GLuint box_vao, box_vbo, box_ebo;
    glGenVertexArrays(1, &box_vao); glGenBuffers(1, &box_vbo); glGenBuffers(1, &box_ebo);
    std::vector<float> box_verts = {
        -1,-1,-1, 0,0,-1,  1,-1,-1, 0,0,-1,  1, 1,-1, 0,0,-1,  -1, 1,-1, 0,0,-1,
        -1,-1, 1, 0,0, 1,  1,-1, 1, 0,0, 1,  1, 1, 1, 0,0, 1,  -1, 1, 1, 0,0, 1,
        -1,-1,-1, -1,0,0, -1,-1, 1, -1,0,0, -1, 1, 1, -1,0,0, -1, 1,-1, -1,0,0,
         1,-1,-1,  1,0,0,  1,-1, 1,  1,0,0,  1, 1, 1,  1,0,0,  1, 1,-1,  1,0,0,
        -1, 1,-1, 0,1,0,   1, 1,-1, 0,1,0,   1, 1, 1, 0,1,0,  -1, 1, 1, 0,1,0,
        -1,-1,-1, 0,-1,0,  1,-1,-1, 0,-1,0,  1,-1, 1, 0,-1,0, -1,-1, 1, 0,-1,0
    };
    std::vector<uint32_t> box_idx = {
        0,1,2, 0,2,3, 4,5,6, 4,6,7, 8,9,10, 8,10,11,
        12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23
    };
    glBindVertexArray(box_vao);
    glBindBuffer(GL_ARRAY_BUFFER, box_vbo); glBufferData(GL_ARRAY_BUFFER, box_verts.size() * 4, box_verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, box_ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, box_idx.size() * 4, box_idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

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

    while (running) {
        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        if (deltaTime > 0.05f) deltaTime = 0.05f;
        lastTime = currentTime;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                if (mouseLocked) { mouseLocked = false; SDL_SetRelativeMouseMode(SDL_FALSE); }
                else running = false;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                if (!mouseLocked) { mouseLocked = true; SDL_SetRelativeMouseMode(SDL_TRUE); }
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

        // 垂直碰撞
        glm::vec3 moveY(0.0f, playerVel.y * deltaTime, 0.0f);
        glm::vec3 newPosY = playerPos + moveY;
        float floorDist = TraceBVH(newPosY, glm::vec3(0.0f, -1.0f, 0.0f));
        if (floorDist != -1.0f && floorDist <= (playerHeight * 0.5f + 0.5f)) {
            newPosY.y -= (floorDist - playerHeight * 0.5f); playerVel.y = 0.0f;
            if (moveY.y <= 0.0f) isGrounded = true;
        }
        float ceilDist = TraceBVH(newPosY, glm::vec3(0.0f, 1.0f, 0.0f));
        if (ceilDist != -1.0f && ceilDist <= (playerHeight * 0.5f + 0.5f)) {
            newPosY.y += (ceilDist - playerHeight * 0.5f); playerVel.y = 0.0f;
        }
        playerPos = newPosY;

        // 水平碰撞 X
        float dx = playerVel.x * deltaTime;
        if (fabsf(dx) > 0.0001f) {
            glm::vec3 dirX(signbit(dx) ? -1.0f : 1.0f, 0.0f, 0.0f);
            float distX = TraceBVH(playerPos, dirX);
            if (distX != -1.0f && distX < playerRadius) { playerPos.x += (distX - playerRadius) * dirX.x; playerVel.x = 0.0f; }
            else { playerPos.x += dx; }
        }
        // 水平碰撞 Z
        float dz = playerVel.z * deltaTime;
        if (fabsf(dz) > 0.0001f) {
            glm::vec3 dirZ(0.0f, 0.0f, signbit(dz) ? -1.0f : 1.0f);
            float distZ = TraceBVH(playerPos, dirZ);
            if (distZ != -1.0f && distZ < playerRadius) { playerPos.z += (distZ - playerRadius) * dirZ.z; playerVel.z = 0.0f; }
            else { playerPos.z += dz; }
        }

        // ====================================================================
        // 把玩家的 BVH 位置同步给 Jolt 物理刚体
        // ====================================================================
        body_interface.SetPositionAndRotation(
            player_body, 
            JPH::RVec3(playerPos.x, playerPos.y, playerPos.z), 
            JPH::Quat::sIdentity(), 
            JPH::EActivation::Activate
        );

        // ====================================================================
        // 物理推挤增强：如果在箱子附近，施加冲量（Impulse）
        // ====================================================================
        {
            JPH::RVec3 box_pos = body_interface.GetCenterOfMassPosition(box_id);
            glm::vec3 bpos((float)box_pos.GetX(), (float)box_pos.GetY(), (float)box_pos.GetZ());
            
            // 计算玩家和箱子的水平距离
            glm::vec3 diff = playerPos - bpos;
            diff.y = 0.0f; // 忽略Y轴，只要贴得近就推
            
            float horizontalDist = glm::length(diff);
            // 如果玩家在箱子周围 40 单位内，且正在朝箱子方向移动
            if (horizontalDist < 40.0f) {
                glm::vec3 pushDir = glm::normalize(-diff); // 玩家朝箱子的方向
                float dot = glm::dot(glm::normalize(glm::vec3(playerVel.x, 0, playerVel.z)), pushDir);
                
                if (dot > 0.1f) { // 玩家确实在朝箱子走
                    // 增加冲量（Impulse，直接改变速度，比 Force 响应更快）
                    JPH::Vec3 impulse(pushDir.x * 80.0f, 0.0f, pushDir.z * 80.0f);
                    body_interface.AddImpulse(box_id, impulse);
                    body_interface.ActivateBody(box_id);
                }
            }
        }

        // Jolt 物理更新
        physics.Update(deltaTime, 1, temp_allocator, job_system);

        // 渲染
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Pos: %.1f, %.1f, %.1f", playerPos.x, playerPos.y, playerPos.z);
        ImGui::Text("Grounded: %s", isGrounded ? "YES" : "NO");
        ImGui::Text("Tris: %zu BVH nodes: %zu", g_sortedTris.size(), g_bvh.size());
        JPH::RVec3 dbg_box = body_interface.GetCenterOfMassPosition(box_id);
        ImGui::Text("Box Pos: %.1f, %.1f, %.1f", (float)dbg_box.GetX(), (float)dbg_box.GetY(), (float)dbg_box.GetZ());
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
        drawList->AddLine(ImVec2(center.x - 5, center.y), ImVec2(center.x + 5, center.y), IM_COL32(255, 255, 255, 255), 2.0f);
        drawList->AddLine(ImVec2(center.x, center.y - 5), ImVec2(center.x, center.y + 5), IM_COL32(255, 255, 255, 255), 2.0f);
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
        glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, ni, GL_UNSIGNED_INT, 0);

        // 渲染 Jolt 箱子
        JPH::RMat44 box_transform = body_interface.GetWorldTransform(box_id);
        JPH::Vec3 t = box_transform.GetTranslation();
        JPH::Quat q = box_transform.GetQuaternion();
        glm::vec3 box_pos((float)t.GetX(), (float)t.GetY(), (float)t.GetZ());
        glm::quat box_quat((float)q.GetW(), (float)q.GetX(), (float)q.GetY(), (float)q.GetZ());
        glm::mat4 box_model = glm::translate(glm::mat4(1.0f), box_pos) * glm::mat4_cast(box_quat) * glm::scale(glm::mat4(1.0f), glm::vec3(16.0f));
        glm::mat4 box_mvp = projection * view * box_model;
        glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, glm::value_ptr(box_mvp));
        glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, glm::value_ptr(box_model));
        glBindVertexArray(box_vao);
        glDrawElements(GL_TRIANGLES, (GLsizei)box_idx.size(), GL_UNSIGNED_INT, 0);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // 清理
    body_interface.RemoveBody(box_id); body_interface.DestroyBody(box_id);
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance; JPH::Factory::sInstance = nullptr;
    delete job_system; delete temp_allocator;

    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(); SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}