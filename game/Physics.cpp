#include "Physics.h"

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
    JPH::VertexList vertices;
    vertices.reserve(nv);
    for (uint32_t i = 0; i < nv; i++) {
        // 🔥 注意：这里改为 i*3，因为我们传入的只有位置，没有法线
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

    // 简易静态地板
    JPH::BoxShapeSettings floor_shape(JPH::Vec3(1024.0f, 8.0f, 1024.0f));
    JPH::BodyCreationSettings floor_settings(floor_shape.Create().Get(), JPH::RVec3(0, -8, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);
    g_physics->GetBodyInterface().CreateAndAddBody(floor_settings, JPH::EActivation::DontActivate);
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

JPH::BodyID CreateEnemyBody(float x, float y, float z) {
    // 敌人的逻辑半径 20，高度 60，所以半高是 30，半宽是 20
    JPH::BoxShapeSettings shape(JPH::Vec3(20.0f, 30.0f, 20.0f));
    JPH::BodyCreationSettings settings(
        shape.Create().Get(),
        JPH::RVec3(x, y, z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Kinematic,
        Layers::MOVING
    );
    return g_physics->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
}