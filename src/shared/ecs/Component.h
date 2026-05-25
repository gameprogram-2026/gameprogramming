#pragma once
#include <cstdint>
#include <typeindex>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// Component base + type ID utility
// ─────────────────────────────────────────────────────────────────────────────

using ComponentTypeID = uint32_t;

namespace detail {
    inline ComponentTypeID nextComponentTypeID() noexcept {
        static ComponentTypeID counter = 0;
        return counter++;
    }
} // namespace detail

template<typename T>
inline ComponentTypeID componentTypeID() noexcept {
    static const ComponentTypeID id = detail::nextComponentTypeID();
    return id;
}

/// All components inherit from this so pools can be stored polymorphically.
struct ComponentBase {
    virtual ~ComponentBase() = default;
};

} // namespace dz
