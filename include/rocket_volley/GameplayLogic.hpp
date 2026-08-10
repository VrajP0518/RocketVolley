#pragma once

#include "rocket_volley/MathTypes.hpp"

namespace rv {

struct ArenaGeometry {
    float halfWidth = 14.0F;
    float halfLength = 22.0F;
    float floorHeight = 0.0F;
    float netHeight = 2.56F;
    float netHalfThickness = 0.16F;
    float ballRadius = 1.08F;
};

struct BallKinematics {
    Vec3 position{};
    Vec3 velocity{};
};

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
