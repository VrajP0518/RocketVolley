#include "rocket_volley/GameplayLogic.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rv {
namespace {

constexpr float SimulationStep = 1.0F / 240.0F;

void bounceAxis(float &position, float &velocity, float limit, float restitution) {
    if (position > limit) {
        position = limit;
        if (velocity > 0.0F) velocity *= -restitution;
    } else if (position < -limit) {
        position = -limit;
        if (velocity < 0.0F) velocity *= -restitution;
    }
}

bool advanceBall(BallKinematics &state, float step, float gravity, float restitution,
    const ArenaGeometry &arena) {
    const Vec3 previous = state.position;
    const float damping = std::max(0.0F, 1.0F - 0.035F * step);
    state.velocity.y -= gravity * step;
    state.velocity.x *= damping;
    state.velocity.y *= damping;
    state.velocity.z *= damping;
    const float speed = std::sqrt(state.velocity.x * state.velocity.x + state.velocity.y * state.velocity.y
        + state.velocity.z * state.velocity.z);
    if (arena.ballSpeedLimit > 0.0F && speed > arena.ballSpeedLimit) {
        const float scale = arena.ballSpeedLimit / speed;
        state.velocity.x *= scale; state.velocity.y *= scale; state.velocity.z *= scale;
    }
    state.position.x += state.velocity.x * step;
    state.position.y += state.velocity.y * step;
    state.position.z += state.velocity.z * step;
    const float radius = arena.ballRadius;
    // Finite cage walls: a high lob can leave the court instead of reflecting in midair.
    if (state.position.y - radius < arena.wallHeight) {
        if (std::abs(previous.x) <= arena.halfWidth - radius + 0.01F)
            bounceAxis(state.position.x, state.velocity.x, arena.halfWidth - radius, restitution);
        if (std::abs(previous.z) <= arena.halfLength - radius + 0.01F)
            bounceAxis(state.position.z, state.velocity.z, arena.halfLength - radius, restitution);
    }
    // Sphere against the actual net box, including the top tape and rounded corner contact.
    const Vec3 nearest{
        std::clamp(state.position.x, -arena.halfWidth + 0.25F, arena.halfWidth - 0.25F),
        std::clamp(state.position.y, arena.floorHeight, arena.netHeight),
        std::clamp(state.position.z, -arena.netHalfThickness, arena.netHalfThickness)};
    Vec3 normal{state.position.x - nearest.x, state.position.y - nearest.y, state.position.z - nearest.z};
    const float distance = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (distance < radius) {
        if (distance > 0.0001F) {
            normal.x /= distance; normal.y /= distance; normal.z /= distance;
        } else {
            normal = {0.0F, 0.0F, previous.z < 0.0F ? -1.0F : 1.0F};
        }
        state.position.x += normal.x * (radius - distance);
        state.position.y += normal.y * (radius - distance);
        state.position.z += normal.z * (radius - distance);
        const float inward = normal.x * state.velocity.x + normal.y * state.velocity.y + normal.z * state.velocity.z;
        if (inward < 0.0F) {
            state.velocity.x -= normal.x * (1.0F + restitution) * inward;
            state.velocity.y -= normal.y * (1.0F + restitution) * inward;
            state.velocity.z -= normal.z * (1.0F + restitution) * inward;
        }
    }
    const float floorY = arena.floorHeight + radius;
    if (state.position.y <= floorY && std::abs(state.position.x) <= arena.halfWidth
        && std::abs(state.position.z) <= arena.halfLength) {
        state.position.y = floorY;
        if (state.velocity.y < 0.0F) state.velocity.y *= -restitution;
        return true;
    }
    return false;
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
    if (!std::isfinite(timeSeconds) || !std::isfinite(gravityAcceleration)
        || !std::isfinite(restitution)) return state;
    float remaining = std::clamp(timeSeconds, 0.0F, 8.0F);
    while (remaining > 0.00001F) {
        const float step = std::min(SimulationStep, remaining);
        advanceBall(state, step, std::max(0.0F, gravityAcceleration),
            std::clamp(restitution, 0.0F, 1.0F), arena);
        remaining -= step;
    }
    return state;
}

BallFlight predictBallFlight(BallKinematics state, float gravity, float restitution, const ArenaGeometry &arena) {
    BallFlight flight;
    flight.samples[flight.count++] = state;
    constexpr float sampleTime = 1.0F / 60.0F;
    for (std::size_t sample = 1; sample < flight.samples.size(); ++sample) {
        for (int substep = 0; substep < 4; ++substep) {
            flight.landed = advanceBall(state, SimulationStep, gravity, std::clamp(restitution, 0.0F, 1.0F), arena);
            flight.time += SimulationStep;
            flight.escaped = ballEscaped(state.position, arena);
            if (flight.landed || flight.escaped) break;
        }
        flight.samples[flight.count++] = state;
        if (flight.landed || flight.escaped) break;
        flight.time = static_cast<float>(sample) * sampleTime;
    }
    return flight;
}

float analogAxis(float value, float deadzone) {
    if (!std::isfinite(value)) return 0.0F;
    deadzone = std::clamp(deadzone, 0.0F, 0.95F);
    return std::copysign(std::clamp((std::abs(value) - deadzone) / (1.0F - deadzone), 0.0F, 1.0F), value);
}

bool ballEscaped(Vec3 p, const ArenaGeometry &arena) {
    return !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
        || p.y < -4.0F || p.y > 60.0F
        || std::abs(p.x) > arena.halfWidth + arena.ballRadius + 1.0F
        || std::abs(p.z) > arena.halfLength + arena.ballRadius + 1.0F;
}

int winnerAfterPoint(const std::array<int, 2> &score, int limit, float remainingTime, bool overtime) {
    if (score[0] == score[1]) return -1;
    const int leader = score[0] > score[1] ? 0 : 1;
    return score[leader] >= limit || remainingTime <= 0.0F || overtime ? leader : -1;
}

TeamPlan planTeam(const std::array<TeamCar, 6> &cars, int team, const BallFlight &flight,
    int previousStriker, bool pro) {
    TeamPlan plan;
    plan.intercept = flight.landing();
    const float side = team == 0 ? 1.0F : -1.0F;
    plan.threatened = plan.intercept.z * side > 0.0F;
    float bestCost = 10000.0F;
    for (int index = team * 3; index < team * 3 + 3; ++index) {
        const TeamCar &car = cars[index];
        if (!car.available) continue;
        Vec3 candidate = flight.landing();
        float cost = 1000.0F;
        float candidateTime = flight.time;
        bool reachable = false;
        // Earliest reachable descending interception, accounting for facing, velocity and boost.
        for (std::size_t sample = 1; sample < flight.count; ++sample) {
            const auto &ball = flight.samples[sample];
            if (ball.position.z * side < 1.0F || ball.position.y > (pro ? 4.0F : 3.1F)
                || ball.velocity.y > 1.0F) continue;
            const Vec3 offset{ball.position.x - car.position.x, 0.0F, ball.position.z + side * 1.8F - car.position.z};
            const float distance = planarLength(offset);
            const float angle = std::remainder(std::atan2(offset.x, offset.z) - car.heading, 2.0F * std::numbers::pi_v<float>);
            const float toward = distance > 0.01F ? (car.velocity.x * offset.x + car.velocity.z * offset.z) / distance : 0.0F;
            const float speed = pro && car.boost > 12.0F ? 19.0F : 12.0F;
            const float eta = std::max(0.0F, distance - 1.65F) / speed
                + std::abs(angle) * 0.22F + std::max(0.0F, 6.0F - toward) * 0.018F;
            const float time = static_cast<float>(sample) / 60.0F;
            const float candidateCost = time + std::max(0.0F, eta - time) * 4.0F;
            if (candidateCost < cost) {
                cost = candidateCost;
                candidate = ball.position;
                candidateTime = time;
                reachable = eta <= time + 0.1F;
            }
        }
        if (cost >= 1000.0F) cost = planarLength({candidate.x - car.position.x, 0.0F, candidate.z - car.position.z}) / 12.0F;
        if (index == previousStriker) cost -= 0.22F;
        if (car.human) cost -= 0.22F;
        if (car.recovering) cost += 0.85F;
        if (cost < bestCost) {
            bestCost = cost;
            plan.striker = index;
            plan.intercept = candidate;
            plan.interceptTime = candidateTime;
            plan.reachable = reachable;
        }
    }
    float supportCost = 10000.0F;
    for (int index = team * 3; index < team * 3 + 3; ++index) {
        if (!cars[index].available || index == plan.striker) continue;
        const float cost = std::abs(cars[index].position.z - side * 12.5F);
        if (cost < supportCost) { supportCost = cost; plan.support = index; }
    }
    return plan;
}

float volleyLift(Vec3 contactNormal, float closingSpeed, float outgoingVerticalSpeed) {
    if (!std::isfinite(closingSpeed) || !std::isfinite(outgoingVerticalSpeed)
        || !std::isfinite(contactNormal.y) || contactNormal.y < 0.05F || contactNormal.y > 0.9F) return 0.0F;
    return std::min(std::clamp((closingSpeed - 2.0F) * 0.65F, 0.0F, 7.0F),
        std::max(0.0F, 18.0F - outgoingVerticalSpeed));
}

Vec3 volleyVelocityChange(Vec3 contactNormal, float closingSpeed, Vec3 carVelocity, Vec3 outgoing) {
    Vec3 change{0.0F, volleyLift(contactNormal, closingSpeed, outgoing.y), 0.0F};
    const float driveSpeed = planarLength(carVelocity);
    if (contactNormal.y > 0.15F && closingSpeed > 2.0F && driveSpeed > 2.0F) {
        // Roof/bonnet carry follows actual momentum, never a target or team direction.
        const float along = (outgoing.x * carVelocity.x + outgoing.z * carVelocity.z) / driveSpeed;
        const float carry = std::clamp(driveSpeed * 0.9F - along, 0.0F, 8.0F);
        change.x = carVelocity.x / driveSpeed * carry;
        change.z = carVelocity.z / driveSpeed * carry;
        if (contactNormal.y > 0.9F)
            change.y = std::min(driveSpeed * 0.45F, std::max(0.0F, 18.0F - outgoing.y));
    }
    return change;
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
