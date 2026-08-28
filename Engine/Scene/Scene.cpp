#include "Engine/Scene/Scene.h"

#include <entt/entity/entity.hpp>

namespace DeepRun::Scene
{
Entity Scene::CreateEntity(std::string name)
{
    const entt::entity native = registry_.create();
    registry_.emplace<Transform>(native);
    if (!name.empty())
    {
        registry_.emplace<Tag>(native, std::move(name));
    }
    ++entityCount_;
    return FromNative(native);
}

void Scene::DestroyEntity(const Entity entity)
{
    if (!IsValid(entity))
    {
        return;
    }
    registry_.destroy(ToNative(entity));
    --entityCount_;
}

bool Scene::IsValid(const Entity entity) const noexcept
{
    return static_cast<bool>(entity) && registry_.valid(ToNative(entity));
}

void Scene::Clear() noexcept
{
    registry_.clear();
    entityCount_ = 0;
}

std::size_t Scene::EntityCount() const noexcept
{
    return entityCount_;
}

entt::entity Scene::ToNative(const Entity entity) noexcept
{
    return static_cast<entt::entity>(entity.value);
}

Entity Scene::FromNative(const entt::entity entity) noexcept
{
    return Entity{entt::to_integral(entity)};
}
}
