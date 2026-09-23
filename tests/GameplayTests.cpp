#include "rocket_volley/GameplayLogic.hpp"
#include "rocket_volley/MultiplayerProtocol.hpp"
#include "rocket_volley/PlayerPreferences.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>

int main() {
    using namespace rv;
    int failures = 0;
    const auto check = [&](bool condition, const char *name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        if (!condition) ++failures;
    };
    check(gameplayLogicSelfTest(), "existing gameplay cases");
    check(net::protocolSelfTest(), "protocol v4 compatibility");
    PlayerPreferences preferences;
    check(preferences.musicGain() == 1.0F && preferences.effectsGain() == 1.0F
        && preferences.shakeGain() == 1.0F && !preferences.readSetting("version", 3),
        "old profiles preserve the original audio and camera defaults");
    preferences.readSetting("music_volume", -10);
    preferences.readSetting("effects_volume", 250);
    preferences.readSetting("camera_shake", 30);
    preferences.readSetting("point_replays", 0);
    check(preferences.musicGain() == 0.0F && preferences.effectsGain() == 1.0F
        && std::abs(preferences.shakeGain() - 0.3F) < 0.0001F, "preference values clamp independently");
    std::ostringstream serialized;
    preferences.writeSettings(serialized);
    PlayerPreferences restored;
    std::istringstream input(serialized.str());
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        restored.readSetting(line.substr(0, separator), std::stoi(line.substr(separator + 1)));
    }
    check(restored.musicVolume == 0 && restored.effectsVolume == 100 && restored.cameraShake == 30 && !restored.pointReplays,
        "audio and comfort settings round-trip in the existing text format");
    net::WorldSnapshotPacket invalidSnapshot;
    invalidSnapshot.state = 8;
    check(!net::decodeWorldSnapshot(net::encodeWorldSnapshot(invalidSnapshot)), "unknown snapshot state rejected");
    invalidSnapshot.state = 3;
    invalidSnapshot.matchTime = -1.0F;
    check(!net::decodeWorldSnapshot(net::encodeWorldSnapshot(invalidSnapshot)), "negative snapshot clock rejected");
    InputEdges edges;
    check(volleyLift({0.0F, 0.4F, 0.9F}, 12.0F, 3.0F) > 5.0F,
        "committed bumper hit receives shared upward lift");
    check(volleyLift({0.0F, 1.0F, 0.0F}, 12.0F, 3.0F) == 0.0F
        && volleyLift({0.0F, -0.4F, 0.9F}, 12.0F, 3.0F) == 0.0F
        && volleyLift({0.0F, 0.4F, 0.9F}, 1.0F, 3.0F) == 0.0F,
        "roof catches, underside contacts and soft taps gain no bumper energy");
    check(volleyLift({0.0F, 0.4F, 0.9F}, 100.0F, 17.0F) == 1.0F
        && volleyLift({0.0F, 0.4F, 0.9F}, 100.0F, 20.0F) == 0.0F,
        "bumper lift cannot grow vertical speed without bound");
    const auto carry = volleyVelocityChange({0.0F, 1.0F, 0.0F}, 8.0F, {0.0F, 4.0F, -10.0F}, {});
    const auto reverseCarry = volleyVelocityChange({0.0F, 1.0F, 0.0F}, 8.0F, {0.0F, 4.0F, 10.0F}, {});
    const auto stationary = volleyVelocityChange({0.0F, 1.0F, 0.0F}, 12.0F, {}, {});
    check(carry.z == -8.0F && reverseCarry.z == 8.0F && carry.y > 0.0F && carry.y == reverseCarry.y,
        "roof carry follows car momentum and caps added horizontal speed");
    check(stationary.x == 0.0F && stationary.y == 0.0F && stationary.z == 0.0F,
        "stationary roof catch retains the physical bounce without extra carry");
    edges.push(net::JumpPressed);
    edges.push(0); // a render-only frame cannot clear the press
    edges.push(net::DodgePressed);
    check(edges.consume() == (net::JumpPressed | net::DodgePressed), "edges survive zero-step frames");
    check(edges.consume() == 0, "edges consumed exactly once during catch-up");
    check(analogAxis(0.16F) == 0.0F && analogAxis(0.17F) < 0.02F
        && analogAxis(-1.0F) == -1.0F && analogAxis(1.0F) == 1.0F, "continuous analog deadzone");
    check(winnerAfterPoint({4, 3}, 7, 20.0F, false) == -1, "ordinary point continues match");
    check(winnerAfterPoint({7, 3}, 7, 20.0F, false) == 0, "score cap ends match");
    check(winnerAfterPoint({4, 3}, 7, 0.0F, false) == 0, "final rally resolves leading team");
    check(winnerAfterPoint({4, 4}, 7, 0.0F, false) == -1, "equalizer at zero forces overtime");
    check(winnerAfterPoint({4, 5}, 7, 0.0F, true) == 1, "sudden death point wins");

    const auto lowWall = predictBallMotion({{12.5F, 3.0F, 8.0F}, {12.0F, 0.0F, 0.0F}}, 0.1F, 0.0F, 0.6F);
    check(lowWall.velocity.x < 0.0F && std::abs(lowWall.velocity.x) < 7.3F, "wall restitution loses energy");
    const auto highWall = predictBallMotion({{12.5F, 10.0F, 8.0F}, {12.0F, 0.0F, 0.0F}}, 0.3F, 0.0F, 0.8F);
    check(highWall.position.x > 15.0F && highWall.velocity.x > 0.0F, "high lob clears finite wall");
    const auto netFace = predictBallMotion({{0.0F, 2.0F, 1.4F}, {0.0F, 0.0F, -8.0F}}, 0.04F, 0.0F, 0.8F);
    check(netFace.position.z >= 1.24F && netFace.velocity.z > 0.0F, "net face before center crossing");
    const auto netTop = predictBallMotion({{0.0F, 3.8F, 0.0F}, {0.0F, -5.0F, 0.0F}}, 0.08F, 0.0F, 0.8F);
    check(netTop.position.y >= 3.64F && netTop.velocity.y > 0.0F, "net top bounce");
    const auto flight = predictBallFlight({{0.0F, 7.0F, 10.0F}, {2.0F, 0.0F, 0.0F}}, 18.0F, 0.82F);
    check(flight.landed && flight.time > 0.7F && flight.time < 0.9F
        && std::abs(flight.landing().y - 1.08F) < 0.001F, "prediction ends at first floor contact");
    check(ballEscaped(highWall.position) && !ballEscaped({0.0F, 12.0F, 5.0F})
        && ballEscaped({0.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F}), "escape and invalid-state detection");

    std::array<TeamCar, 6> cars{};
    cars[0] = {{0.0F, 0.6F, 10.0F}, {}, 0.0F, 40.0F, false, true};
    cars[1] = {{1.0F, 0.6F, 12.0F}, {}, 3.14F, 50.0F, true, false};
    cars[2] = {{-6.0F, 0.6F, 18.0F}, {}, 3.14F, 50.0F, true, false};
    auto plan = planTeam(cars, 0, flight, 0, true);
    check(plan.striker == 1 && plan.support == 2 && plan.threatened, "unavailable human cannot own interception");
    cars[0].available = true;
    plan = planTeam(cars, 0, flight, 0, true);
    check(plan.striker >= 0 && plan.support >= 0 && plan.striker != plan.support, "three distinct team roles");
    cars[0] = cars[1];
    cars[0].recovering = true;
    plan = planTeam(cars, 0, flight, -1, true);
    check(plan.striker == 1 && plan.interceptTime > 0.0F,
        "upright teammate takes interception while equally placed car recovers");
    cars[2].available = false;
    plan = planTeam(cars, 0, flight, plan.striker, false);
    check(plan.striker < 2 && plan.support < 2 && plan.striker != plan.support, "2v2 excludes parked third slot");
    for (auto &car : cars) car.available = false;
    plan = planTeam(cars, 0, flight, 0, true);
    check(plan.striker == -1 && plan.support == -1, "fully demolished team has no assigned car");

    bool stable = true;
    for (int seed = 0; seed < 500; ++seed) {
        BallKinematics initial{{static_cast<float>(seed % 23 - 11), 2.0F + static_cast<float>(seed % 12),
            static_cast<float>(seed % 37 - 18)},
            {static_cast<float>(seed % 29 - 14), static_cast<float>(seed % 17 - 8), static_cast<float>(seed % 31 - 15)}};
        const auto result = predictBallFlight(initial, seed % 2 ? 18.0F : 10.44F, 0.55F + static_cast<float>(seed % 9) * 0.05F);
        stable = stable && result.count > 1 && result.count <= result.samples.size() && result.time <= 4.01F;
        for (std::size_t i = 0; i < result.count; ++i) {
            const auto &sample = result.samples[i];
            stable = stable && std::isfinite(sample.position.x) && std::isfinite(sample.position.y)
                && std::isfinite(sample.position.z) && std::isfinite(sample.velocity.y);
        }
    }
    check(stable, "500 varied trajectory simulations stay finite and bounded");
    return failures == 0 ? 0 : 1;
}
