#include "rocket_volley/GameplayLogic.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rv {
namespace {

constexpr float SimulationStep = 1.0F / 240.0F;

void bounceAxis(float &position, float &velocity, float limit) {
    while (position > limit || position < -limit) {
        if (position > limit) {
            position = 2.0F * limit - position;
            velocity = -std::abs(velocity);
        } else {
            position = -2.0F * limit - position;
            velocity = std::abs(velocity);
        }
    }
}

float planarLength(Vec3 value) {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

bool nearlyEqual(float first, float second, float tolerance = 0.03F) {
    return std::abs(first - second) <= tolerance;
}

} // namespace

BallKinematics predictBallMotion(
    BallKinematics state,
    float timeSeconds,
    float gravityAcceleration,
    float restitution,
    const ArenaGeometry &arena) {
    float remaining = std::max(0.0F, timeSeconds);
    const float wallX = arena.halfWidth - arena.ballRadius - 0.25F;
    const float wallZ = arena.halfLength - arena.ballRadius - 0.25F;
    const float floorY = arena.floorHeight + arena.ballRadius;

    while (remaining > 0.0F) {
        const float step = std::min(SimulationStep, remaining);
        const float previousZ = state.position.z;
        state.velocity.y -= gravityAcceleration * step;
        state.position.x += state.velocity.x * step;
        state.position.y += state.velocity.y * step;
        state.position.z += state.velocity.z * step;

        bounceAxis(state.position.x, state.velocity.x, wallX);
        bounceAxis(state.position.z, state.velocity.z, wallZ);

        if (state.position.y < floorY) {
            state.position.y = floorY + (floorY - state.position.y);
            state.velocity.y = std::abs(state.velocity.y) * std::clamp(restitution, 0.0F, 1.0F);
        }

        const bool crossedNet = (previousZ < 0.0F && state.position.z >= 0.0F)
            || (previousZ > 0.0F && state.position.z <= 0.0F);
        const float netTop = arena.netHeight + arena.ballRadius;
        if (crossedNet && state.position.y < netTop) {
            const float side = previousZ < 0.0F ? -1.0F : 1.0F;
            state.position.z = side * (arena.netHalfThickness + arena.ballRadius);
            state.velocity.z = side * std::abs(state.velocity.z) * std::clamp(restitution, 0.0F, 1.0F);
        }

        remaining -= step;
    }
    return state;
}

Vec3 directionalDodge(float heading, float throttle, float steer) {
    float localForward = throttle;
    float localRight = -steer;
    const float inputLength = std::sqrt(localForward * localForward + localRight * localRight);
    if (inputLength < 0.2F) {
        localForward = 1.0F;
        localRight = 0.0F;
    } else {
        localForward /= inputLength;
        localRight /= inputLength;
    }

    const Vec3 forward{std::sin(heading), 0.0F, std::cos(heading)};
    const Vec3 right{std::cos(heading), 0.0F, -std::sin(heading)};
    Vec3 direction{
        forward.x * localForward + right.x * localRight,
        0.0F,
        forward.z * localForward + right.z * localRight};
    const float directionLength = std::max(0.001F, planarLength(direction));
    direction.x /= directionLength;
    direction.z /= directionLength;
    return direction;
}

TargetScore scoreTargetLanding(Vec3 landing, Vec3 target, bool clearedNet, int currentCombo) {
    TargetScore result;
    result.distance = planarLength({landing.x - target.x, 0.0F, landing.z - target.z});
    if (!clearedNet || landing.z >= 0.0F) {
        return result;
    }

    if (result.distance <= 1.6F) {
        result.grade = TargetGrade::Bullseye;
        result.basePoints = 100;
    } else if (result.distance <= 3.2F) {
        result.grade = TargetGrade::Great;
        result.basePoints = 65;
    } else if (result.distance <= 5.2F) {
        result.grade = TargetGrade::Good;
        result.basePoints = 35;
    } else {
        result.grade = TargetGrade::InPlay;
        result.basePoints = 15;
    }

    const bool targetHit = result.grade == TargetGrade::Good
        || result.grade == TargetGrade::Great
        || result.grade == TargetGrade::Bullseye;
    result.nextCombo = targetHit ? currentCombo + 1 : 0;
    const float multiplier = targetHit ? 1.0F + static_cast<float>(std::min(currentCombo, 4)) * 0.25F : 1.0F;
    result.awardedPoints = static_cast<int>(std::lround(static_cast<float>(result.basePoints) * multiplier));
    return result;
}

bool gameplayLogicSelfTest() {
    const ArenaGeometry arena{};
    const BallKinematics wallResult = predictBallMotion(
        {{12.0F, 4.0F, 0.0F}, {8.0F, 0.0F, 0.0F}},
        0.5F,
        18.0F,
        0.82F,
        arena);
    if (wallResult.position.x > arena.halfWidth - arena.ballRadius
        || wallResult.velocity.x >= 0.0F) {
        return false;
    }

    const BallKinematics floorResult = predictBallMotion(
        {{0.0F, 1.2F, 8.0F}, {0.0F, -5.0F, 0.0F}},
        0.2F,
        18.0F,
        0.82F,
        arena);
    if (floorResult.position.y < arena.ballRadius || floorResult.velocity.y <= 0.0F) {
        return false;
    }

    const BallKinematics netResult = predictBallMotion(
        {{0.0F, 2.0F, -1.5F}, {0.0F, 0.0F, 8.0F}},
        0.4F,
        18.0F,
        0.82F,
        arena);
    if (netResult.position.z >= 0.0F || netResult.velocity.z >= 0.0F) {
        return false;
    }

    const Vec3 forward = directionalDodge(0.0F, 0.0F, 0.0F);
    const Vec3 back = directionalDodge(0.0F, -1.0F, 0.0F);
    const Vec3 right = directionalDodge(0.0F, 0.0F, -1.0F);
    if (!nearlyEqual(forward.z, 1.0F)
        || !nearlyEqual(back.z, -1.0F)
        || !nearlyEqual(right.x, 1.0F)
        || !nearlyEqual(planarLength(right), 1.0F)) {
        return false;
    }

    const TargetScore bullseye = scoreTargetLanding({0.5F, 1.08F, -12.0F}, {0.0F, 0.0F, -12.0F}, true, 2);
    const TargetScore playable = scoreTargetLanding({8.0F, 1.08F, -4.0F}, {0.0F, 0.0F, -12.0F}, true, 3);
    const TargetScore miss = scoreTargetLanding({0.0F, 1.08F, 4.0F}, {0.0F, 0.0F, -12.0F}, false, 3);
    return bullseye.grade == TargetGrade::Bullseye && bullseye.awardedPoints == 150 && bullseye.nextCombo == 3
        && playable.grade == TargetGrade::InPlay && playable.awardedPoints == 15 && playable.nextCombo == 0
        && miss.grade == TargetGrade::Miss && miss.awardedPoints == 0 && miss.nextCombo == 0;
}

} // namespace rv
