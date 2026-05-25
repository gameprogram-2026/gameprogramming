#pragma once
#include "Entity.h"
#include "Component.h"

#include <algorithm>
#include <cassert>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// ComponentPool  —  contiguous storage for one component type
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
class ComponentPool {
public:
    /// Returns nullptr if the entity has no T.
    T* get(EntityID id) noexcept {
        auto it = m_index.find(id);
        if (it == m_index.end()) return nullptr;
        return &m_data[it->second];
    }

    const T* get(EntityID id) const noexcept {
        auto it = m_index.find(id);
        if (it == m_index.end()) return nullptr;
        return &m_data[it->second];
    }

    template<typename... Args>
    T& emplace(EntityID id, Args&&... args) {
        assert(m_index.find(id) == m_index.end() && "Component already exists");
        m_index[id] = static_cast<uint32_t>(m_data.size());
        m_owners.push_back(id);
        return m_data.emplace_back(std::forward<Args>(args)...);
    }

    bool remove(EntityID id) {
        auto it = m_index.find(id);
        if (it == m_index.end()) return false;

        uint32_t idx  = it->second;
        uint32_t last = static_cast<uint32_t>(m_data.size()) - 1;

        if (idx != last) {
            m_data[idx]              = std::move(m_data[last]);
            EntityID movedOwner      = m_owners[last];
            m_owners[idx]            = movedOwner;
            m_index[movedOwner]      = idx;
        }

        m_data.pop_back();
        m_owners.pop_back();
        m_index.erase(it);
        return true;
    }

    bool has(EntityID id) const noexcept {
        return m_index.count(id) != 0;
    }

    std::vector<T>&        data()        noexcept { return m_data; }
    const std::vector<T>&  data() const  noexcept { return m_data; }
    const std::vector<EntityID>& owners() const noexcept { return m_owners; }

private:
    std::vector<T>                          m_data;
    std::vector<EntityID>                   m_owners;  // parallel to m_data
    std::unordered_map<EntityID, uint32_t>  m_index;
};

// ─────────────────────────────────────────────────────────────────────────────
// World  —  top-level ECS registry
// ─────────────────────────────────────────────────────────────────────────────

class World {
public:
    // ── Entity lifecycle ──────────────────────────────────────────────────────

    Entity createEntity() {
        EntityID id;
        if (!m_freeList.empty()) {
            id = m_freeList.front();
            m_freeList.pop();
        } else {
            id = m_nextID++;
        }
        m_alive.push_back(id);
        return Entity{id};
    }

    /// Marks entity for deferred destruction (safe to call during iteration).
    void destroyEntity(Entity e) {
        m_pending.push_back(e.id);
    }

    /// Flush pending destructions — call once per tick, outside system loops.
    void flushDestroyQueue() {
        for (EntityID id : m_pending) {
            for (auto& [typeID, pool] : m_pools) {
                pool->removeByID(id);
            }
            m_alive.erase(std::remove(m_alive.begin(), m_alive.end(), id),
                          m_alive.end());
            m_freeList.push(id);
        }
        m_pending.clear();
    }

    bool isAlive(Entity e) const noexcept {
        return std::find(m_alive.begin(), m_alive.end(), e.id) != m_alive.end();
    }

    const std::vector<EntityID>& alive() const noexcept { return m_alive; }

    // ── Component access ──────────────────────────────────────────────────────

    template<typename T, typename... Args>
    T& addComponent(Entity e, Args&&... args) {
        return pool<T>().emplace(e.id, std::forward<Args>(args)...);
    }

    template<typename T>
    void removeComponent(Entity e) {
        pool<T>().remove(e.id);
    }

    template<typename T>
    T* tryGet(Entity e) noexcept {
        return pool<T>().get(e.id);
    }

    template<typename T>
    const T* tryGet(Entity e) const noexcept {
        return pool<T>().get(e.id);
    }

    template<typename T>
    T& get(Entity e) {
        T* c = tryGet<T>(e);
        assert(c && "Entity does not have requested component");
        return *c;
    }

    template<typename T>
    bool has(Entity e) const noexcept {
        auto it = m_pools.find(componentTypeID<T>());
        if (it == m_pools.end()) return false;
        return static_cast<TypedPool<T>*>(it->second.get())->has(e.id);
    }

    template<typename T>
    ComponentPool<T>& pool() {
        ComponentTypeID tid = componentTypeID<T>();
        auto it = m_pools.find(tid);
        if (it == m_pools.end()) {
            auto p = std::make_unique<TypedPool<T>>();
            auto* raw = p.get();
            m_pools.emplace(tid, std::move(p));
            return raw->pool;
        }
        return static_cast<TypedPool<T>*>(it->second.get())->pool;
    }

private:
    // ── Type-erased pool wrapper ──────────────────────────────────────────────

    struct IPool {
        virtual ~IPool() = default;
        virtual void removeByID(EntityID id) = 0;
    };

    template<typename T>
    struct TypedPool : IPool {
        ComponentPool<T> pool;
        void removeByID(EntityID id) override { pool.remove(id); }
    };

    // ── State ─────────────────────────────────────────────────────────────────

    EntityID                                          m_nextID = 0;
    std::queue<EntityID>                              m_freeList;
    std::vector<EntityID>                             m_alive;
    std::vector<EntityID>                             m_pending;
    std::unordered_map<ComponentTypeID,
                       std::unique_ptr<IPool>>        m_pools;
};

} // namespace dz
