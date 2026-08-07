#include "rocket_volley/PhysicsWorld.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

namespace rv {
namespace {

namespace Layers {
constexpr JPH::ObjectLayer Static = 0;
constexpr JPH::ObjectLayer Dynamic = 1;
constexpr JPH::ObjectLayer Count = 2;
} // namespace Layers

namespace BroadLayers {
const JPH::BroadPhaseLayer Static{0};
const JPH::BroadPhaseLayer Dynamic{1};
constexpr unsigned Count = 2;
} // namespace BroadLayers

class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
public:
    BroadPhaseLayerMap() {
        map_[Layers::Static] = BroadLayers::Static;
        map_[Layers::Dynamic] = BroadLayers::Dynamic;
    }

    [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override {
        return BroadLayers::Count;
    }

    [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return map_[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    [[nodiscard]] const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadLayers::Static ? "STATIC" : "DYNAMIC";
    }
#endif

private:
    JPH::BroadPhaseLayer map_[Layers::Count]{};
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadLayer) const override {
        switch (layer) {
        case Layers::Static:
            return broadLayer == BroadLayers::Dynamic;
        case Layers::Dynamic:
            return true;
        default:
            return false;
        }
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override {
        if (first == Layers::Static) {
            return second == Layers::Dynamic;
        }
        return true;
    }
};

void traceImpl(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
}

#ifdef JPH_ENABLE_ASSERTS
bool assertFailedImpl(const char *expression, const char *message, const char *file, JPH::uint line) {
    std::fprintf(stderr, "Jolt assertion at %s:%u: %s (%s)\n", file, line, expression, message ? message : "");
    return true;
}
#endif

JPH::Vec3 toJolt(Vec3 value) {
    return {value.x, value.y, value.z};
}

JPH::RVec3 toJoltPosition(Vec3 value) {
    return {value.x, value.y, value.z};
}

JPH::Quat toJolt(Rotation value) {
    return {value.x, value.y, value.z, value.w};
}

Vec3 fromJolt(JPH::Vec3Arg value) {
    return {value.GetX(), value.GetY(), value.GetZ()};
}

Vec3 fromJoltPosition(JPH::RVec3Arg value) {
    return {static_cast<float>(value.GetX()), static_cast<float>(value.GetY()), static_cast<float>(value.GetZ())};
}

Rotation fromJolt(JPH::QuatArg value) {
    return {value.GetX(), value.GetY(), value.GetZ(), value.GetW()};
}

JPH::BodyID toBodyId(BodyHandle handle) {
    return JPH::BodyID(handle);
}

BodyHandle toHandle(const JPH::BodyID &id) {
    return id.GetIndexAndSequenceNumber();
}

} // namespace

struct PhysicsWorld::Impl {
    BroadPhaseLayerMap broadPhaseMap;
    ObjectVsBroadPhaseFilter objectVsBroadPhaseFilter;
    ObjectPairFilter objectPairFilter;
    JPH::TempAllocatorImpl allocator{16U * 1024U * 1024U};
    JPH::JobSystemThreadPool jobs{
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers,
        static_cast<int>(std::max(1U, std::thread::hardware_concurrency()) - 1U)};
    JPH::PhysicsSystem physics;
    std::vector<JPH::BodyID> bodies;

    Impl() {
        constexpr JPH::uint maxBodies = 256;
        constexpr JPH::uint bodyMutexes = 0;
        constexpr JPH::uint maxBodyPairs = 2048;
        constexpr JPH::uint maxContactConstraints = 2048;
        physics.Init(
            maxBodies,
            bodyMutexes,
            maxBodyPairs,
            maxContactConstraints,
            broadPhaseMap,
            objectVsBroadPhaseFilter,
            objectPairFilter);
        physics.SetGravity({0.0F, -18.0F, 0.0F});
    }

    ~Impl() {
        auto &bodyInterface = physics.GetBodyInterface();
        for (const JPH::BodyID id : bodies) {
            if (!id.IsInvalid()) {
                bodyInterface.RemoveBody(id);
                bodyInterface.DestroyBody(id);
            }
        }
    }

    BodyHandle addBody(JPH::BodyCreationSettings settings, JPH::EActivation activation) {
        JPH::BodyInterface &bodyInterface = physics.GetBodyInterface();
        const JPH::BodyID id = bodyInterface.CreateAndAddBody(settings, activation);
        if (id.IsInvalid()) {
            throw std::runtime_error("Jolt could not allocate a rigid body");
        }
        bodies.push_back(id);
        return toHandle(id);
    }
};

PhysicsWorld::PhysicsWorld() {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = traceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assertFailedImpl;)

    if (JPH::Factory::sInstance != nullptr) {
        throw std::runtime_error("A Jolt Physics factory is already active");
    }
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    impl_ = std::make_unique<Impl>();
}

PhysicsWorld::~PhysicsWorld() {
    impl_.reset();
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

BodyHandle PhysicsWorld::createStaticBox(Vec3 position, Vec3 halfExtents) {
    JPH::BodyCreationSettings settings(
        new JPH::BoxShape(toJolt(halfExtents)),
        toJoltPosition(position),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Layers::Static);
    settings.mFriction = 0.85F;
    settings.mRestitution = 0.1F;
    return impl_->addBody(settings, JPH::EActivation::DontActivate);
}

BodyHandle PhysicsWorld::createDynamicBox(Vec3 position, Vec3 halfExtents, float mass, float restitution) {
    JPH::BodyCreationSettings settings(
        new JPH::BoxShape(toJolt(halfExtents), 0.08F),
        toJoltPosition(position),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Layers::Dynamic);
    settings.mFriction = 0.85F;
    settings.mRestitution = restitution;
    settings.mLinearDamping = 0.22F;
    settings.mAngularDamping = 0.65F;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = mass;
    return impl_->addBody(settings, JPH::EActivation::Activate);
}

BodyHandle PhysicsWorld::createDynamicSphere(Vec3 position, float radius, float mass, float restitution) {
    JPH::BodyCreationSettings settings(
        new JPH::SphereShape(radius),
        toJoltPosition(position),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Layers::Dynamic);
    settings.mFriction = 0.28F;
    settings.mRestitution = restitution;
    settings.mLinearDamping = 0.035F;
    settings.mAngularDamping = 0.08F;
    settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = mass;
    return impl_->addBody(settings, JPH::EActivation::Activate);
}

void PhysicsWorld::destroyBody(BodyHandle body) {
    if (body == InvalidBody) {
        return;
    }
    const JPH::BodyID id = toBodyId(body);
    JPH::BodyInterface &bodyInterface = impl_->physics.GetBodyInterface();
    bodyInterface.RemoveBody(id);
    bodyInterface.DestroyBody(id);
    std::erase(impl_->bodies, id);
}

void PhysicsWorld::step(float deltaSeconds) {
    impl_->physics.Update(deltaSeconds, 1, &impl_->allocator, &impl_->jobs);
}

Transform PhysicsWorld::transform(BodyHandle body) const {
    JPH::RVec3 position;
    JPH::Quat rotation;
    impl_->physics.GetBodyInterface().GetPositionAndRotation(toBodyId(body), position, rotation);
    return {fromJoltPosition(position), fromJolt(rotation)};
}

Vec3 PhysicsWorld::linearVelocity(BodyHandle body) const {
    return fromJolt(impl_->physics.GetBodyInterface().GetLinearVelocity(toBodyId(body)));
}

Vec3 PhysicsWorld::angularVelocity(BodyHandle body) const {
    return fromJolt(impl_->physics.GetBodyInterface().GetAngularVelocity(toBodyId(body)));
}

void PhysicsWorld::setTransform(BodyHandle body, Vec3 position, Rotation rotation, bool activateBody) {
    impl_->physics.GetBodyInterface().SetPositionAndRotation(
        toBodyId(body),
        toJoltPosition(position),
        toJolt(rotation),
        activateBody ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
}

void PhysicsWorld::setLinearVelocity(BodyHandle body, Vec3 velocity) {
    impl_->physics.GetBodyInterface().SetLinearVelocity(toBodyId(body), toJolt(velocity));
}

void PhysicsWorld::setAngularVelocity(BodyHandle body, Vec3 velocity) {
    impl_->physics.GetBodyInterface().SetAngularVelocity(toBodyId(body), toJolt(velocity));
}

void PhysicsWorld::setRestitution(BodyHandle body, float restitution) {
    impl_->physics.GetBodyInterface().SetRestitution(toBodyId(body), restitution);
}

void PhysicsWorld::addImpulse(BodyHandle body, Vec3 impulse) {
    impl_->physics.GetBodyInterface().AddImpulse(toBodyId(body), toJolt(impulse));
}

void PhysicsWorld::activate(BodyHandle body) {
    impl_->physics.GetBodyInterface().ActivateBody(toBodyId(body));
}

} // namespace rv
