#pragma once

#include "rocket_volley/MathTypes.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace rv {

struct ArenaGeometry {
    float halfWidth = 14.0F;
    float halfLength = 22.0F;
    float floorHeight = 0.0F;
    float netHeight = 2.56F;
    float netHalfThickness = 0.16F;
    float ballRadius = 1.08F;
    float wallHeight = 6.2F;
    float ballSpeedLimit = 0.0F; // 0 means no assisted limit (Pro).
};

struct BallKinematics {
    Vec3 position{};
    Vec3 velocity{};
};

// Render-frame edges survive until exactly one simulation step consumes them.
struct InputEdges {
    std::uint8_t pending = 0;
    void push(std::uint8_t pressed) { pending |= pressed; }
    std::uint8_t consume() { const auto value = pending; pending = 0; return value; }
};

struct BallFlight {
    std::array<BallKinematics, 241> samples{};
    std::size_t count = 0;
    float time = 0.0F;
    bool landed = false;
    bool escaped = false;
    [[nodiscard]] Vec3 landing() const { return count > 0 ? samples[count - 1].position : Vec3{}; }
};

struct TeamCar {
    Vec3 position{};
    Vec3 velocity{};
    float heading = 0.0F;
    float boost = 0.0F;
    bool available = false;
    bool human = false;
};

struct TeamPlan {
    int striker = -1;
    int support = -1;
    Vec3 intercept{};
    bool threatened = false;
};

[[nodiscard]] BallFlight predictBallFlight(BallKinematics initial, float gravity,
    float restitution, const ArenaGeometry &arena = {});
[[nodiscard]] TeamPlan planTeam(const std::array<TeamCar, 6> &cars, int team,
    const BallFlight &flight, int previousStriker, bool pro);
[[nodiscard]] float analogAxis(float value, float deadzone = 0.16F);
[[nodiscard]] bool ballEscaped(Vec3 position, const ArenaGeometry &arena = {});
// Rally finishes before the clock decides the winner. -1 means play another rally.
[[nodiscard]] int winnerAfterPoint(const std::array<int, 2> &score, int limit,
    float remainingTime, bool overtime);

enum class TargetGrade {
    Miss,
    InPlay,
    Good,
    Great,
    Bullseye,
};

struct TargetScore {
    TargetGrade grade = TargetGrade::Miss;
    int basePoints = 0;
    int awardedPoints = 0;
    int nextCombo = 0;
    float distance = 0.0F;
};

[[nodiscard]] BallKinematics predictBallMotion(
    BallKinematics initial,
    float timeSeconds,
    float gravityAcceleration,
    float restitution,
    const ArenaGeometry &arena = {});

[[nodiscard]] Vec3 directionalDodge(float heading, float throttle, float steer);
[[nodiscard]] TargetScore scoreTargetLanding(Vec3 landing, Vec3 target, bool clearedNet, int currentCombo);
[[nodiscard]] bool gameplayLogicSelfTest();

} // namespace rv
