#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "Entity.h"

class EntityFactory {
public:
    using CreatorFunc = std::function<std::unique_ptr<Entity>()>;

    static EntityFactory& Instance() {
        static EntityFactory instance;
        return instance;
    }

    void Register(const std::string& classname, CreatorFunc creator) {
        m_creators[classname] = std::move(creator);
    }

    std::unique_ptr<Entity> Create(const std::string& classname) const {
        auto it = m_creators.find(classname);
        if (it != m_creators.end()) {
            return it->second();
        }
        return nullptr;
    }

    bool IsRegistered(const std::string& classname) const {
        return m_creators.find(classname) != m_creators.end();
    }

private:
    std::unordered_map<std::string, CreatorFunc> m_creators;
};

struct EntityRegistrar {
    EntityRegistrar(const std::string& classname, EntityFactory::CreatorFunc creator) {
        EntityFactory::Instance().Register(classname, std::move(creator));
    }
};

#define ENTITY_CONCAT_INNER(a, b) a##b
#define ENTITY_CONCAT(a, b) ENTITY_CONCAT_INNER(a, b)

#define REGISTER_ENTITY(CLASSNAME_STR, CLASS_TYPE) \
    static EntityRegistrar ENTITY_CONCAT(g_reg_##CLASS_TYPE##_, __LINE__)(CLASSNAME_STR, []() -> std::unique_ptr<Entity> { \
        return std::make_unique<CLASS_TYPE>(); \
    })

