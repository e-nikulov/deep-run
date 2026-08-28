#pragma once

#include "Engine/Scene/Components.h"
#include "Engine/Scene/Entity.h"

#include <entt/entity/registry.hpp>

#include <cassert>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace DeepRun::Scene
{
class Scene final
{
public:
    [[nodiscard]] Entity CreateEntity(std::string name = {});
    void DestroyEntity(Entity entity);
    [[nodiscard]] bool IsValid(Entity entity) const noexcept;
    void Clear() noexcept;
    [[nodiscard]] std::size_t EntityCount() const noexcept;

    template <typename Component, typename... Arguments>
    Component& Add(Entity entity, Arguments&&... arguments)
    {
        assert(IsValid(entity));
        return registry_.emplace<Component>(ToNative(entity), std::forward<Arguments>(arguments)...);
    }

    template <typename Component>
    [[nodiscard]] bool Has(Entity entity) const
    {
        return IsValid(entity) && registry_.all_of<Component>(ToNative(entity));
    }

    template <typename Component>
    Component& Get(Entity entity)
    {
        assert(IsValid(entity));
        return registry_.get<Component>(ToNative(entity));
    }

    template <typename Component>
    const Component& Get(Entity entity) const
    {
        assert(IsValid(entity));
        return registry_.get<Component>(ToNative(entity));
    }

    template <typename Component, typename Function>
    void Each(Function&& function)
    {
        auto view = registry_.view<Component>();
        view.each(
            [&function](const entt::entity native, Component& component)
            {
                std::invoke(function, FromNative(native), component);
            });
    }

private:
    [[nodiscard]] static entt::entity ToNative(Entity entity) noexcept;
    [[nodiscard]] static Entity FromNative(entt::entity entity) noexcept;

    entt::registry registry_;
    std::size_t entityCount_ = 0;
};
}
