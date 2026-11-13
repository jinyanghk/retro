#include "Physics.h"

// ---- Jolt 依赖（physics.cpp 自己用得到，不进 Physics.h）----
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/MotionProperties.h>
// 形状
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCylinderShape.h>
#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>

// 约束
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>

#include <thread>
#include <vector>
#include <cstdint>

// 👈 给 CharacterVirtual 一个基础设置
static JPH::Ref<JPH::CharacterVirtualSettings> g_enemyCharSettings;
static JPH::Ref<JPH::Shape> g_enemyShape;
static std::vector<JPH::Ref<JPH::CharacterVirtual>> g_enemyChars;

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
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const {
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
// 全局物理状态
// ============================================================================
static JPH::TempAllocatorImpl* g_temp_allocator = nullptr;
static JPH::JobSystemThreadPool* g_job_system = nullptr;
static JPH::PhysicsSystem* g_physics = nullptr;
static BPLayerInterfaceImpl g_bp_layer_interface;
static ObjectVsBroadPhaseLayerFilterImpl g_obj_vs_bp_filter;
static ObjectLayerPairFilterImpl g_obj_vs_obj_filter;

void InitPhysics() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes(); // <-- POPULATES COLLISION DISPATCH FUNCTION POINTERS!

    g_temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    g_job_system = new JPH::JobSystemThreadPool(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::max(1u, std::thread::hardware_concurrency() - 1));

    g_physics = new JPH::PhysicsSystem();
    g_physics->Init(8192, 0, 8192, 8192, g_bp_layer_interface, g_obj_vs_bp_filter, g_obj_vs_obj_filter);
    g_physics->SetGravity(JPH::Vec3(0.0f, -1200.0f, 0.0f));

    JPH::ShapeSettings::ShapeResult shapeResult = JPH::CapsuleShapeSettings(17.0f, 14.0f).Create();
    if (!shapeResult.IsValid()) {
        printf("[Jolt] ERROR: Failed to create capsule shape!\n");
        return;
    }
    g_enemyShape = shapeResult.Get();

    g_enemyCharSettings = new JPH::CharacterVirtualSettings();
    g_enemyCharSettings->mShape = g_enemyShape;
    g_enemyCharSettings->mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
    g_enemyCharSettings->mMaxStrength = 100.0f;
    g_enemyCharSettings->mCharacterPadding = 0.02f;
    g_enemyCharSettings->mPenetrationRecoverySpeed = 1.0f;
    g_enemyCharSettings->mPredictiveContactDistance = 0.1f;
    g_enemyCharSettings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -17.0f);
}

void ShutdownPhysics() {
    printf("[Jolt] ShutdownPhysics\n"); fflush(stdout);

    // 1. Destroy CharacterVirtuals, settings, and shapes FIRST while g_physics is still alive!
    g_enemyChars.clear();
    g_enemyCharSettings = nullptr;
    g_enemyShape = nullptr;

    // 2. Delete PhysicsSystem, JobSystem, and TempAllocator in reverse init order
    delete g_physics;        g_physics = nullptr;
    delete g_job_system;     g_job_system = nullptr;
    delete g_temp_allocator; g_temp_allocator = nullptr;
}

JPH::PhysicsSystem& GetPhysicsSystem() { return *g_physics; }
JPH::BodyInterface& GetBodyInterface() { return g_physics->GetBodyInterface(); }

void UpdatePhysics(float deltaTime) {
    g_physics->Update(deltaTime, 1, g_temp_allocator, g_job_system);
}

// ============================================================================
// 实体创建封装
// ============================================================================
void CreateStaticMesh(const std::vector<float>& verts, const std::vector<uint32_t>& indices, uint32_t nv, uint32_t ni) {
    if (nv == 0 || ni < 3) {
        printf("[Physics] WARNING: CreateStaticMesh called with empty data (nv=%u, ni=%u), skipping.\n", nv, ni);
        return;
    }

    // 👈 加诊断：检查顶点里有没有 NaN
    int nanCount = 0;
    for (uint32_t i = 0; i < nv * 3; i++) {
        if (std::isnan(verts[i])) nanCount++;
    }
    if (nanCount > 0) {
        printf("[Physics] WARNING: %d NaN vertices in mesh! This will crash Jolt.\n", nanCount);
    }

    // 👈 加诊断：检查索引是否越界
    int oobCount = 0;
    for (uint32_t i = 0; i < ni; i++) {
        if (indices[i] >= nv) oobCount++;
    }
    if (oobCount > 0) {
        printf("[Physics] WARNING: %d out-of-bounds indices in mesh! This will crash Jolt.\n", oobCount);
    }

    JPH::VertexList vertices;
    vertices.reserve(nv);
    for (uint32_t i = 0; i < nv; i++) {
        vertices.push_back(JPH::Float3(verts[i*3+0], verts[i*3+1], verts[i*3+2]));
    }

    JPH::IndexedTriangleList triangles;
    triangles.reserve(ni / 3);

    // 👈 过滤退化三角形：面积接近 0 的三角形会让 Jolt 的 NarrowPhase 崩溃
    int degenerateCount = 0;
    for (uint32_t i = 0; i + 2 < ni; i += 3) {
        uint32_t a = indices[i], b = indices[i+1], c = indices[i+2];
        if (a >= nv || b >= nv || c >= nv) continue;
        
        glm::vec3 va(verts[a*3], verts[a*3+1], verts[a*3+2]);
        glm::vec3 vb(verts[b*3], verts[b*3+1], verts[b*3+2]);
        glm::vec3 vc(verts[c*3], verts[c*3+1], verts[c*3+2]);
        
        // 叉积长度 = 平行四边形面积，接近 0 说明三点共线
        glm::vec3 cross = glm::cross(vb - va, vc - va);
        float area2 = glm::length(cross);
        if (area2 < 1e-4f) {
            degenerateCount++;
            continue;  // 👈 跳过退化三角形
        }
        
        triangles.push_back(JPH::IndexedTriangle(a, b, c, 0));
    }
    if (degenerateCount > 0) {
        printf("[Physics] Filtered %d degenerate triangles\n", degenerateCount);
    }

    JPH::MeshShapeSettings mesh_settings(std::move(vertices), std::move(triangles));
    JPH::BodyCreationSettings bcs(
        mesh_settings.Create().Get(),
        JPH::RVec3::sZero(),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Layers::NON_MOVING);
    bcs.mEnhancedInternalEdgeRemoval = true;
    g_physics->GetBodyInterface().CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
    g_physics->OptimizeBroadPhase(); // <-- COMMITS STATIC MESH TO BROADPHASE IMMEDIATELY!
}

JPH::BodyID CreatePlayerBody(float x, float y, float z) {
    JPH::BoxShapeSettings shape(JPH::Vec3(16.0f, 36.0f, 16.0f));
    JPH::BodyCreationSettings settings(
        shape.Create().Get(),
        JPH::RVec3(x, y, z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Kinematic,
        Layers::MOVING
    );
    settings.mFriction = 0.0f;
    settings.mRestitution = 0.0f;
    settings.mAllowSleeping = false;
    return g_physics->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
}

JPH::BodyID CreatePhysBox(float x, float y, float z, float hx, float hy, float hz) {
    hx = std::max(hx, 0.5f);
    hy = std::max(hy, 0.5f);
    hz = std::max(hz, 0.5f);
    JPH::BoxShapeSettings shape(JPH::Vec3(hx, hy, hz));
    auto res = shape.Create();
    if (!res.IsValid()) return JPH::BodyID();
    JPH::BodyCreationSettings settings(
        res.Get(),
        JPH::RVec3(x, y, z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Layers::MOVING
    );
    settings.mLinearDamping = 0.2f;
    settings.mFriction = 1.0f;
    return g_physics->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
}

JPH::BodyID CreateDoorBody(float x, float y, float z, float hx, float hy, float hz) {
    JPH::BoxShapeSettings shape(JPH::Vec3(hx, hy, hz));
    JPH::BodyCreationSettings settings(
        shape.Create().Get(),
        JPH::RVec3(x, y, z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Kinematic,
        Layers::MOVING
    );
    settings.mFriction = 0.5f;
    return g_physics->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
}

JPH::BodyID CreateBarrelBody(float x, float y, float z) {
    JPH::BoxShapeSettings shape(JPH::Vec3(12.0f, 24.0f, 12.0f));
    JPH::BodyCreationSettings settings(
        shape.Create().Get(),
        JPH::RVec3(x, y, z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Layers::MOVING
    );
    settings.mLinearDamping = 0.3f;
    settings.mFriction = 0.8f;
    return g_physics->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
}

void MoveKinematicBody(JPH::BodyID id, float x, float y, float z) {
    g_physics->GetBodyInterface().SetPositionAndRotation(id, JPH::RVec3(x, y, z), JPH::Quat::sIdentity(), JPH::EActivation::Activate);
}

// ============================================================================
// CharacterVirtual 句柄封装
// ============================================================================
int CreateEnemyCharacter(float x, float y, float z) {
    auto char_ = new JPH::CharacterVirtual(g_enemyCharSettings, JPH::RVec3(x,y,z), JPH::Quat::sIdentity(), g_physics);
    char_->SetUp(JPH::Vec3::sAxisY());

    for (size_t i = 0; i < g_enemyChars.size(); ++i) {
        if (!g_enemyChars[i]) {
            g_enemyChars[i] = char_;
            return (int)i;
        }
    }
    g_enemyChars.push_back(char_);
    return (int)g_enemyChars.size() - 1;
}

glm::vec3 UpdateEnemyCharacter(int charId, const glm::vec3& vel, float dt, bool& outGrounded) {
    if (charId < 0 || charId >= (int)g_enemyChars.size() || !g_enemyChars[charId]) {
        outGrounded = false;
        return glm::vec3(0.0f);
    }
    auto* ch = g_enemyChars[charId].GetPtr();
    ch->UpdateGroundVelocity();

    float velY = ch->IsSupported() ? 0.0f : (ch->GetLinearVelocity().GetY() - 1200.0f * dt);
    ch->SetLinearVelocity(JPH::Vec3(vel.x, velY, vel.z));

    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mStickToFloorStepDown = JPH::Vec3(0.0f, -15.0f, 0.0f);
    settings.mWalkStairsStepUp = JPH::Vec3(0.0f, 18.0f, 0.0f);

    ch->ExtendedUpdate(dt, g_physics->GetGravity(), settings,
        g_physics->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        g_physics->GetDefaultLayerFilter(Layers::MOVING),
        {}, {}, *g_temp_allocator);

    outGrounded = ch->IsSupported();
    auto p = ch->GetPosition();
    return glm::vec3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ());
}

void DestroyEnemyCharacter(int charId) {
    if (charId >= 0 && charId < (int)g_enemyChars.size()) {
        g_enemyChars[charId] = nullptr;
    }
}

void SetBodyToDynamic(JPH::BodyID id) {
    g_physics->GetBodyInterface().SetMotionType(id, JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
}

JPH::BodyID CreateTrapBody(float x, float y, float z, float hx, float hy, float hz) {
    JPH::BoxShapeSettings shape(JPH::Vec3(hx, hy, hz));
    JPH::BodyCreationSettings settings(
        shape.Create().Get(),
        JPH::RVec3(x, y, z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Kinematic,
        Layers::MOVING
    );
    settings.mGravityFactor = 0.0f;
    settings.mAllowDynamicOrKinematic = true;
    settings.mRestitution = 0.0f;
    settings.mFriction = 1.0f;
    return g_physics->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
}

void DestroyBodySafe(JPH::BodyID id) {
    if (!g_physics || id.IsInvalid()) return;
    auto& bi = g_physics->GetBodyInterface();
    if (bi.IsAdded(id)) {
        bi.RemoveBody(id);
    }
    bi.DestroyBody(id);
}
