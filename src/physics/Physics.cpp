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
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include <thread>
#include <vector>
#include <cstdint>

// 👈 给 CharacterVirtual 一个基础设置
static JPH::Ref<JPH::CharacterVirtualSettings> g_enemyCharSettings;
static JPH::Ref<JPH::Shape> g_enemyShape;

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
    JPH::RegisterTypes();

    g_temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    g_job_system = new JPH::JobSystemThreadPool(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

    g_physics = new JPH::PhysicsSystem();
    g_physics->Init(1024, 0, 1024, 1024, g_bp_layer_interface, g_obj_vs_bp_filter, g_obj_vs_obj_filter);
    g_physics->SetGravity(JPH::Vec3(0.0f, -1200.0f, 0.0f));

    // 👈 初始化敌人碰撞形状（胶囊体）
    JPH::CapsuleShapeSettings capsuleSettings(17.0f, 14.0f); // 半高17，半径14（总高约62）
    g_enemyShape = capsuleSettings.Create().Get();
    
    g_enemyCharSettings = new JPH::CharacterVirtualSettings();
    g_enemyCharSettings->mShape = g_enemyShape;
    g_enemyCharSettings->mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);   // 最大爬坡 50 度
    g_enemyCharSettings->mMaxStrength = 100.0f;
    g_enemyCharSettings->mCharacterPadding = 0.02f;
    g_enemyCharSettings->mPenetrationRecoverySpeed = 1.0f;
    g_enemyCharSettings->mPredictiveContactDistance = 0.1f;
    g_enemyCharSettings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -17.0f); // 贴地探测    
}

void ShutdownPhysics() {
    delete g_physics;
    delete g_job_system;
    delete g_temp_allocator;
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
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
    JPH::VertexList vertices;
    vertices.reserve(nv);
    for (uint32_t i = 0; i < nv; i++) {
        vertices.push_back(JPH::Float3(verts[i*3+0], verts[i*3+1], verts[i*3+2]));
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
    g_physics->GetBodyInterface().CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
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
    JPH::BoxShapeSettings shape(JPH::Vec3(hx, hy, hz));
    JPH::BodyCreationSettings settings(
        shape.Create().Get(),
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

/*
JPH::BodyID CreateEnemyBody(float x, float y, float z) {
    // 👈 改小半径，从 20 变成 14，总宽 28。这样它就能钻过更窄的缝了
    JPH::BoxShapeSettings shape(JPH::Vec3(14.0f, 34.0f, 14.0f)); 
    JPH::BodyCreationSettings settings(
        shape.Create().Get(),
        JPH::RVec3(x, y, z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Layers::MOVING
    );
    settings.mFriction = 0.0f;
    settings.mAllowSleeping = false;
    settings.mLinearDamping = 0.0f;
    settings.mAngularDamping = 1.0f;
    settings.mGravityFactor = 1.0f;
    settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX |
                            JPH::EAllowedDOFs::TranslationY |
                            JPH::EAllowedDOFs::TranslationZ;
    return g_physics->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
}
*/

JPH::Ref<JPH::CharacterVirtual> CreateEnemyCharacter(float x, float y, float z) {
    JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
        g_enemyCharSettings, 
        JPH::RVec3(x, y, z), 
        JPH::Quat::sIdentity(), 
        g_physics
    );
    // 👈 启用“贴地”和“自动爬台阶”
    character->SetUp(JPH::Vec3::sAxisY());
    return character;
}

void UpdateEnemyCharacter(JPH::CharacterVirtual* character, 
                          const glm::vec3& velocity, 
                          float dt)
{
    if (!character) return;
    
    // 1. 设置速度
    character->SetLinearVelocity(JPH::Vec3(velocity.x, 0.0f, velocity.z));
    
    // 2. 重力
    JPH::Vec3 gravity(0, -1200.0f, 0);
    
    // 3. 更新设置（贴地 + 爬台阶）
    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mStickToFloorStepDown = -JPH::Vec3(0, 50.0f, 0);
    updateSettings.mWalkStairsStepUp = JPH::Vec3(0, 30.0f, 0);
    
    // 4. 执行更新（用 MOVING 层，只和地形碰撞）
    character->ExtendedUpdate(
        dt,
        gravity,
        updateSettings,
        g_physics->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        g_physics->GetDefaultLayerFilter(Layers::MOVING),
        {},
        {},
        *g_temp_allocator
    );
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