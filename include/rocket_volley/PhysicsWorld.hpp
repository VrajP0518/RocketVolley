#pragma once

#include "rocket_volley/MathTypes.hpp"

#include <cstdint>
#include <memory>

namespace rv {

using BodyHandle = std::uint32_t;
inline constexpr BodyHandle InvalidBody = 0xFFFFFFFFU;

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld &) = delete;
    PhysicsWorld &operator=(const PhysicsWorld &) = delete;

    BodyHandle createStaticBox(Vec3 position, Vec3 halfExtents);
    BodyHandle createDynamicBox(Vec3 position, Vec3 halfExtents, float mass, float restitution = 0.25F);
    BodyHandle createDynamicSphere(Vec3 position, float radius, float mass, float restitution);

    void destroyBody(BodyHandle body);
    void step(float deltaSeconds);

    [[nodiscard]] Transform transform(BodyHandle body) const;
    [[nodiscard]] Vec3 linearVelocity(BodyHandle body) const;
    [[nodiscard]] Vec3 angularVelocity(BodyHandle body) const;

    void setTransform(BodyHandle body, Vec3 position, Rotation rotation, bool activate = true);
    void setLinearVelocity(BodyHandle body, Vec3 velocity);
    void setAngularVelocity(BodyHandle body, Vec3 velocity);
    void setRestitution(BodyHandle body, float restitution);
    void addImpulse(BodyHandle body, Vec3 impulse);
    void activate(BodyHandle body);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rv
