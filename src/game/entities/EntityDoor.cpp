#include "EntityDoor.h"
#include "../EntityFactory.h"
#include "../World.h"
#include "Physics.h"
#include <algorithm>
#include <cmath>

REGISTER_ENTITY("func_door", EntityDoor);

static glm::vec3 ParseMoveDir(const glm::vec3& angles) {
    // Hammer stores movedir as pitch yaw roll (-90 = up, 90 = down, otherwise yaw in XZ plane)
    if (angles.x <= -89.0f) return glm::vec3(0.0f, 1.0f, 0.0f);
    if (angles.x >= 89.0f)  return glm::vec3(0.0f, -1.0f, 0.0f);
    float yawRad = glm::radians(angles.y);
    return glm::normalize(glm::vec3(std::cos(yawRad), 0.0f, std::sin(yawRad)));
}

void EntityDoor::Spawn(World& world, const EntitySpawnData& data) {
    Entity::Spawn(world, data);

    m_closedMins = m_bboxMins;
    m_closedMaxs = m_bboxMaxs;
    m_closedCenter = (m_closedMins + m_closedMaxs) * 0.5f;
    m_position = m_closedCenter;

    glm::vec3 moveAngles = data.GetVec3("movedir", glm::vec3(0.0f));
    m_moveDir = ParseMoveDir(moveAngles);

    m_speed = std::max(1.0f, data.GetFloat("speed", 100.0f));
    m_waitTime = data.GetFloat("wait", 3.0f);
    float lip = data.GetFloat("lip", 8.0f);

    // Spawnflag 256 (0x100) in HL2 = "Use Opens"
    int spawnflags = data.GetInt("spawnflags", 256);
    m_useOpens = (spawnflags & 256) != 0 || m_targetname.empty();

    glm::vec3 size = m_closedMaxs - m_closedMins;
    float extentAlongDir = std::abs(glm::dot(size, m_moveDir));
    m_moveDist = std::max(8.0f, extentAlongDir - lip);

    glm::vec3 halfExtents = glm::max(size * 0.5f, glm::vec3(1.0f));
    JPH::BodyID id = CreateDoorBody(m_closedCenter.x, m_closedCenter.y, m_closedCenter.z,
                                    halfExtents.x, halfExtents.y, halfExtents.z);
    world.RegisterBody(id, this);
}

void EntityDoor::Think(World& world, float dt) {
    if (m_state == DoorState::Opening) {
        float step = (m_speed / m_moveDist) * dt;
        m_openFraction = std::min(1.0f, m_openFraction + step);

        glm::vec3 pos = m_closedCenter + m_moveDir * (m_moveDist * m_openFraction);
        m_position = pos;
        if (HasBody()) MoveKinematicBody(m_bodyId, pos.x, pos.y, pos.z);

        if (m_openFraction >= 1.0f) {
            m_state = DoorState::Open;
            m_openTimer = m_waitTime;
            FireOutput(world, "OnFullyOpen");
        }
    } else if (m_state == DoorState::Open) {
        if (m_waitTime >= 0.0f) {
            m_openTimer -= dt;
            if (m_openTimer <= 0.0f) {
                Close(world);
            }
        }
    } else if (m_state == DoorState::Closing) {
        float step = (m_speed / m_moveDist) * dt;
        m_openFraction = std::max(0.0f, m_openFraction - step);

        glm::vec3 pos = m_closedCenter + m_moveDir * (m_moveDist * m_openFraction);
        m_position = pos;
        if (HasBody()) MoveKinematicBody(m_bodyId, pos.x, pos.y, pos.z);

        if (m_openFraction <= 0.0f) {
            m_state = DoorState::Closed;
            FireOutput(world, "OnFullyClosed");
        }
    }
}

void EntityDoor::Destroy(World& world) {
    if (HasBody()) {
        DestroyBodySafe(m_bodyId);
    }
    Entity::Destroy(world);
}

void EntityDoor::Use(World& world, Entity* activator) {
    if (m_useOpens) {
        Toggle(world);
    }
}

void EntityDoor::AcceptInput(World& world, const std::string& inputName, Entity* activator, const std::string& param) {
    if (inputName == "Open") {
        Open(world);
    } else if (inputName == "Close") {
        Close(world);
    } else if (inputName == "Toggle") {
        Toggle(world);
    } else {
        Entity::AcceptInput(world, inputName, activator, param);
    }
}

void EntityDoor::Open(World& world) {
    if (m_state == DoorState::Open || m_state == DoorState::Opening) return;
    m_state = DoorState::Opening;
    FireOutput(world, "OnOpen");
}

void EntityDoor::Close(World& world) {
    if (m_state == DoorState::Closed || m_state == DoorState::Closing) return;
    m_state = DoorState::Closing;
    FireOutput(world, "OnClose");
}

void EntityDoor::Toggle(World& world) {
    if (m_state == DoorState::Closed || m_state == DoorState::Closing) {
        Open(world);
    } else {
        Close(world);
    }
}
