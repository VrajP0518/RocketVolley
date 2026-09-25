#include "rocket_volley/Game.hpp"

#include "rocket_volley/GameplayLogic.hpp"
#include "rocket_volley/MathTypes.hpp"
#include "rocket_volley/MultiplayerProtocol.hpp"
#include "rocket_volley/PhysicsWorld.hpp"
#include "rocket_volley/SaveFile.hpp"
#include "rocket_volley/PlayerPreferences.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numbers>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rv {
namespace {

constexpr int ScreenWidth = 1280;
constexpr int ScreenHeight = 720;
constexpr float ArenaHalfWidth = 14.0F;
constexpr float ArenaHalfLength = 22.0F;
constexpr float BallRadius = 1.08F;
constexpr float BallMass = 4.2F;
constexpr float FixedStep = 1.0F / 120.0F;
constexpr float PowerupInitialDelay = 8.0F;
constexpr float PowerupRecharge = 15.0F;
constexpr float PowerupMaximumTimer = 20.0F;
constexpr float OutOfBoundsRespawnDelay = 5.0F;
constexpr float DemolitionRespawnDelay = 3.0F;
constexpr float DemolitionSpeedThreshold = 24.5F;
constexpr std::size_t MaximumCars = net::MaximumPlayerSlots;
constexpr int CarsPerTeam = 3;
constexpr int TargetChallengeShots = 10;
constexpr float Pi = std::numbers::pi_v<float>;
constexpr const char *RepositoryUrl = "https://github.com/VrajP0518/RocketVolley";
constexpr const char *IssuesUrl = "https://github.com/VrajP0518/RocketVolley/issues";
constexpr const char *RoadmapUrl = "https://github.com/VrajP0518/RocketVolley/blob/main/docs/RELEASE.md";

#ifndef ROCKET_VOLLEY_VERSION
#define ROCKET_VOLLEY_VERSION "dev"
#endif

float clamp(float value, float minimum, float maximum) {
    return std::max(minimum, std::min(value, maximum));
}

float length2D(Vec3 value) {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

float length(Vec3 value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

Vec3 subtract(Vec3 first, Vec3 second) {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

Vec3 forwardFromHeading(float heading) {
    return {std::sin(heading), 0.0F, std::cos(heading)};
}

float wrapAngle(float angle) {
    while (angle > Pi) {
        angle -= 2.0F * Pi;
    }
    while (angle < -Pi) {
        angle += 2.0F * Pi;
    }
    return angle;
}

Rotation yawRotation(float heading) {
    return {0.0F, std::sin(heading * 0.5F), 0.0F, std::cos(heading * 0.5F)};
}

Vector3 toRay(Vec3 value) {
    return {value.x, value.y, value.z};
}

Quaternion toRay(Rotation value) {
    return {value.x, value.y, value.z, value.w};
}

Color withAlpha(Color color, unsigned char alpha) {
    color.a = alpha;
    return color;
}

Color scaledColor(Color color, float scale, unsigned char alpha = 255) {
    return {
        static_cast<unsigned char>(clamp(static_cast<float>(color.r) * scale, 0.0F, 255.0F)),
        static_cast<unsigned char>(clamp(static_cast<float>(color.g) * scale, 0.0F, 255.0F)),
        static_cast<unsigned char>(clamp(static_cast<float>(color.b) * scale, 0.0F, 255.0F)),
        alpha};
}

void drawCentered(const std::string &text, int y, int size, Color color) {
    DrawText(text.c_str(), (ScreenWidth - MeasureText(text.c_str(), size)) / 2, y, size, color);
}

int fittedFontSize(const std::string &text, int maximumWidth, int preferredSize, int minimumSize) {
    int size = preferredSize;
    while (size > minimumSize && MeasureText(text.c_str(), size) > maximumWidth) {
        --size;
    }
    return size;
}

void drawCenteredFitted(
    const std::string &text,
    int centerX,
    int y,
    int maximumWidth,
    int preferredSize,
    int minimumSize,
    Color color) {
    const int size = fittedFontSize(text, maximumWidth, preferredSize, minimumSize);
    DrawText(text.c_str(), centerX - MeasureText(text.c_str(), size) / 2, y, size, color);
}

Sound synthesize(unsigned sampleRate, float duration, const std::function<float(float)> &sampleFunction) {
    const auto frameCount = static_cast<unsigned>(static_cast<float>(sampleRate) * duration);
    std::vector<std::int16_t> samples(frameCount);
    for (unsigned index = 0; index < frameCount; ++index) {
        const float time = static_cast<float>(index) / static_cast<float>(sampleRate);
        const float sample = clamp(sampleFunction(time), -1.0F, 1.0F);
        samples[index] = static_cast<std::int16_t>(sample * 28000.0F);
    }
    Wave wave{frameCount, sampleRate, 16, 1, samples.data()};
    return LoadSoundFromWave(wave);
}

struct AudioKit {
    bool ready = false;
    float musicGain = 1.0F;
    float effectsGain = 1.0F;
    Sound hit{};
    Sound jump{};
    Sound landing{};
    Sound score{};
    Sound boost{};
    Sound menuMove{};
    Sound countdown{};
    Sound menuMusic{};
    Sound gameMusic{};

    void initialize() {
        InitAudioDevice();
        ready = IsAudioDeviceReady();
        if (!ready) {
            return;
        }

        constexpr unsigned rate = 22050;
        hit = synthesize(rate, 0.16F, [](float t) {
            const float envelope = std::exp(-22.0F * t);
            return envelope * (0.62F * std::sin(2.0F * Pi * (145.0F + 620.0F * t) * t)
                + 0.25F * std::sin(2.0F * Pi * 73.0F * t));
        });
        jump = synthesize(rate, 0.22F, [](float t) {
            const float frequency = 180.0F + 720.0F * t;
            return std::exp(-7.5F * t) * 0.7F * std::sin(2.0F * Pi * frequency * t);
        });
        landing = synthesize(rate, 0.12F, [](float t) {
            return std::exp(-32.0F * t) * 0.6F * std::sin(2.0F * Pi * (95.0F - 160.0F * t) * t);
        });
        score = synthesize(rate, 0.75F, [](float t) {
            constexpr std::array<float, 4> notes{392.0F, 523.25F, 659.25F, 783.99F};
            const int note = std::min(3, static_cast<int>(t / 0.16F));
            const float local = std::fmod(t, 0.16F);
            return 0.58F * std::exp(-4.0F * local) * std::sin(2.0F * Pi * notes[note] * t);
        });
        boost = synthesize(rate, 0.3F, [](float t) {
            const float noise = std::sin(2.0F * Pi * 91.0F * t) * std::sin(2.0F * Pi * 1733.0F * t);
            return 0.35F * std::exp(-3.0F * t) * noise;
        });
        menuMove = synthesize(rate, 0.09F, [](float t) {
            return std::exp(-28.0F * t) * 0.42F * std::sin(2.0F * Pi * 620.0F * t);
        });
        countdown = synthesize(rate, 0.18F, [](float t) {
            return std::exp(-14.0F * t) * 0.55F * std::sin(2.0F * Pi * 440.0F * t);
        });
        menuMusic = synthesize(rate, 10.0F, [](float t) {
            constexpr std::array<float, 8> notes{130.81F, 164.81F, 196.0F, 246.94F, 146.83F, 174.61F, 220.0F, 261.63F};
            const int step = static_cast<int>(t / 1.25F) % static_cast<int>(notes.size());
            const float local = std::fmod(t, 1.25F);
            const float pad = 0.16F * std::sin(2.0F * Pi * notes[step] * t)
                + 0.08F * std::sin(2.0F * Pi * notes[step] * 0.5F * t);
            return pad * std::min(local * 4.0F, 1.0F) * std::min((1.25F - local) * 3.0F, 1.0F);
        });
        gameMusic = synthesize(rate, 8.0F, [](float t) {
            constexpr std::array<float, 16> melody{
                261.63F, 329.63F, 392.0F, 329.63F, 293.66F, 349.23F, 440.0F, 349.23F,
                261.63F, 329.63F, 493.88F, 392.0F, 293.66F, 349.23F, 440.0F, 523.25F};
            const int step = static_cast<int>(t / 0.5F) % static_cast<int>(melody.size());
            const float beat = std::fmod(t, 0.5F);
            const float square = std::sin(2.0F * Pi * melody[step] * t) >= 0.0F ? 1.0F : -1.0F;
            const float bassFrequency = (step % 8 < 4) ? 65.41F : 73.42F;
            const float bass = std::sin(2.0F * Pi * bassFrequency * t);
            const float kick = std::sin(2.0F * Pi * (85.0F - 55.0F * std::min(beat, 0.12F) / 0.12F) * t)
                * std::exp(-25.0F * std::fmod(t, 0.5F));
            return 0.18F * square + 0.18F * bass + 0.22F * kick;
        });
        applyMix(musicGain, effectsGain);
    }

    void applyMix(float music, float effects) {
        musicGain = clamp(music, 0.0F, 1.0F);
        effectsGain = clamp(effects, 0.0F, 1.0F);
        if (!ready) return;
        SetSoundVolume(hit, 0.72F * effectsGain);
        SetSoundVolume(jump, 0.55F * effectsGain);
        SetSoundVolume(landing, 0.28F * effectsGain);
        SetSoundVolume(score, 0.7F * effectsGain);
        SetSoundVolume(boost, 0.32F * effectsGain);
        SetSoundVolume(menuMove, 0.38F * effectsGain);
        SetSoundVolume(countdown, 0.5F * effectsGain);
        SetSoundVolume(menuMusic, 0.28F * musicGain);
        SetSoundVolume(gameMusic, 0.22F * musicGain);
        if (musicGain == 0.0F) {
            StopSound(menuMusic);
            StopSound(gameMusic);
        }
    }

    void updateMusic(bool menuActive) const {
        if (!ready || musicGain == 0.0F) {
            return;
        }
        if (menuActive) {
            if (IsSoundPlaying(gameMusic)) {
                StopSound(gameMusic);
            }
            if (!IsSoundPlaying(menuMusic)) {
                PlaySound(menuMusic);
            }
        } else {
            if (IsSoundPlaying(menuMusic)) {
                StopSound(menuMusic);
            }
            if (!IsSoundPlaying(gameMusic)) {
                PlaySound(gameMusic);
            }
        }
    }

    void play(const Sound &sound) const {
        if (ready && effectsGain > 0.0F) {
            PlaySound(sound);
        }
    }

    void playHit(float strength) const {
        if (!ready || effectsGain == 0.0F) {
            return;
        }
        const float normalized = clamp(strength / 18.0F, 0.0F, 1.0F);
        SetSoundVolume(hit, (0.24F + normalized * 0.66F) * effectsGain);
        SetSoundPitch(hit, 0.82F + normalized * 0.34F);
        PlaySound(hit);
    }

    void shutdown() {
        if (!ready) {
            return;
        }
        UnloadSound(hit);
        UnloadSound(jump);
        UnloadSound(landing);
        UnloadSound(score);
        UnloadSound(boost);
        UnloadSound(menuMove);
        UnloadSound(countdown);
        UnloadSound(menuMusic);
        UnloadSound(gameMusic);
        CloseAudioDevice();
        ready = false;
    }
};

enum class MatchState {
    Title,
    Loading,
    ServeCountdown,
    Playing,
    Paused,
    PointWon,
    GoalReplay,
    GameOver,
};

enum class CameraMode {
    Car,
    Ball,
};

enum class GameplayCameraLayout {
    Standard,
    WideTeam,
    SplitScreen,
};

struct GameplayCameraPose {
    Vector3 position{};
    Vector3 target{};
    float fov = 58.0F;
};

Rectangle displayViewport(int width, int height) {
    const float scale = std::min(static_cast<float>(std::max(1, width)) / ScreenWidth,
        static_cast<float>(std::max(1, height)) / ScreenHeight);
    return {(width - ScreenWidth * scale) * 0.5F, (height - ScreenHeight * scale) * 0.5F,
        ScreenWidth * scale, ScreenHeight * scale};
}

struct SceneryView {
    Vector3 eye{}, player{}, ball{};
    bool fading = false;
};

float sceneryOpacity(const SceneryView &view, Vector3 center, Vector3 size) {
    if (!view.fading) return 1.0F;
    const BoundingBox bounds{
        {center.x - size.x * 0.5F - 0.65F, center.y - size.y * 0.5F - 0.65F, center.z - size.z * 0.5F - 0.65F},
        {center.x + size.x * 0.5F + 0.65F, center.y + size.y * 0.5F + 0.65F, center.z + size.z * 0.5F + 0.65F}};
    for (Vector3 focus : {view.player, view.ball}) {
        const Vector3 offset = Vector3Subtract(focus, view.eye);
        const float distance = Vector3Length(offset);
        if (distance < 0.01F) continue;
        const RayCollision hit = GetRayCollisionBox({view.eye, Vector3Scale(offset, 1.0F / distance)}, bounds);
        if (hit.hit && hit.distance < distance) return 0.10F;
    }
    return 1.0F;
}

void drawSceneryCube(const SceneryView &view, Vector3 center, float width, float height, float depth, Color color) {
    const bool outsideCourt = std::abs(center.x) > ArenaHalfWidth || std::abs(center.z) > ArenaHalfLength;
    if (outsideCourt) color.a = static_cast<unsigned char>(color.a * sceneryOpacity(view, center, {width, height, depth}));
    // A translucent foreground object must not write opaque depth and hide the ball.
    if (color.a < 255) { rlDrawRenderBatchActive(); rlDisableDepthMask(); }
    DrawCube(center, width, height, depth, color);
    if (color.a < 255) { rlDrawRenderBatchActive(); rlEnableDepthMask(); }
}

Vector3 arenaCameraPosition(Vector3 position) {
    // Keep the orbit distance near the cage instead of squeezing the camera into the car.
    const float edge = std::max(std::abs(position.x) - (ArenaHalfWidth - 3.0F),
        std::abs(position.z) - (ArenaHalfLength - 3.0F));
    position.y += std::max(0.0F, 7.6F - position.y) * clamp(edge / 2.0F, 0.0F, 1.0F);
    return position;
}

GameplayCameraPose ballCameraFraming(Vector3 position, Vec3 car, Vec3 ball, float aspect, float baseFov) {
    const Vector3 towardCar = Vector3Normalize(Vector3Subtract({car.x, car.y + 1.0F, car.z}, position));
    const Vector3 towardBall = Vector3Normalize(Vector3Subtract({ball.x, ball.y + 0.18F, ball.z}, position));
    Vector3 direction = Vector3Add(towardCar, towardBall);
    if (Vector3Length(direction) < 0.001F) direction = towardCar;
    if (Vector3Length(direction) < 0.001F) direction = {0.0F, 0.0F, 1.0F};
    const float separation = std::acos(clamp(Vector3DotProduct(towardCar, towardBall), -1.0F, 1.0F));
    // raylib FOV is vertical. Portrait split views need extra vertical FOV to fit
    // the same horizontal angle; use the narrower dimension conservatively.
    const float halfAngle = std::min(separation * 0.5F, 1.45F);
    const float framingFov = 2.0F * std::atan(std::tan(halfAngle) / std::max(0.1F, std::min(1.0F, aspect)))
        * (180.0F / Pi) + 14.0F;
    return {position, Vector3Add(position, Vector3Scale(Vector3Normalize(direction), 10.0F)),
        clamp(std::max(baseFov, framingFov), baseFov, 82.0F)};
}

GameplayCameraPose gameplayCameraPose(
    Vec3 playerPosition,
    Vec3 playerVelocity,
    float playerHeading,
    Vec3 ballPosition,
    CameraMode mode,
    GameplayCameraLayout layout) {
    const bool wideTeamView = layout == GameplayCameraLayout::WideTeam;
    const bool splitScreen = layout == GameplayCameraLayout::SplitScreen;
    const float speedFraction = clamp(length(playerVelocity) / 26.0F, 0.0F, 1.0F);
    Vec3 viewDirection = forwardFromHeading(playerHeading);

    float followDistance = splitScreen ? 9.4F : (wideTeamView ? 11.2F : 8.8F);
    float cameraHeight = splitScreen ? 5.7F : (wideTeamView ? 6.4F : 5.2F);
    float lookAhead = splitScreen ? 3.8F : (wideTeamView ? 4.2F : 3.2F);
    float desiredFov = (splitScreen ? 68.0F : (wideTeamView ? 63.0F : 58.0F))
        + speedFraction * (splitScreen ? 8.0F : 9.0F);
    Vector3 desiredTarget{
        playerPosition.x + viewDirection.x * lookAhead,
        playerPosition.y + (splitScreen ? 1.15F : 1.25F),
        playerPosition.z + viewDirection.z * lookAhead};

    if (mode == CameraMode::Ball) {
        const Vec3 towardBall = subtract(ballPosition, playerPosition);
        const float planarDistance = length2D(towardBall);
        if (planarDistance > 0.35F) {
            viewDirection = {towardBall.x / planarDistance, 0.0F, towardBall.z / planarDistance};
        }

        // Rocket League-style Ball Cam: orbit around the player's car and aim at the ball.
        // The ball controls yaw/aim, but never becomes the camera's positional anchor.
        followDistance = splitScreen ? 9.8F : (wideTeamView ? 10.6F : 9.2F);
        cameraHeight = splitScreen ? 5.9F : (wideTeamView ? 6.2F : 5.4F);
        desiredFov = (splitScreen ? 70.0F : (wideTeamView ? 65.0F : 62.0F))
            + speedFraction * 7.0F;
    }

    const Vector3 desiredPosition = arenaCameraPosition({
        playerPosition.x - viewDirection.x * followDistance,
        playerPosition.y + cameraHeight,
        playerPosition.z - viewDirection.z * followDistance});
    if (mode == CameraMode::Ball) {
        return ballCameraFraming(desiredPosition, playerPosition, ballPosition,
            static_cast<float>(ScreenWidth) / ScreenHeight * (splitScreen ? 0.5F : 1.0F), desiredFov);
    }

    return {desiredPosition, desiredTarget, desiredFov};
}

enum class MenuPage {
    Main,
    MatchSetup,
    Customize,
    Controls,
    About,
    Preferences,
};

enum class Difficulty {
    Rookie,
    Pro,
};

enum class GameMode {
    Match,
    LocalCoop,
    Training,
    ThreeVsThree,
    TargetChallenge,
};

constexpr int ArcadeCupRoundCount = 3;
constexpr std::array<const char *, ArcadeCupRoundCount> ArcadeCupRoundNames{
    "NEON QUALIFIER",
    "CITY SEMIFINAL",
    "CANYON FINAL",
};
constexpr std::array<GameMode, ArcadeCupRoundCount> ArcadeCupRoundModes{
    GameMode::Match,
    GameMode::ThreeVsThree,
    GameMode::ThreeVsThree,
};
constexpr std::array<Difficulty, ArcadeCupRoundCount> ArcadeCupDifficulties{
    Difficulty::Rookie,
    Difficulty::Rookie,
    Difficulty::Pro,
};
constexpr std::array<int, ArcadeCupRoundCount> ArcadeCupDurations{120, 120, 180};
constexpr std::array<int, ArcadeCupRoundCount> ArcadeCupScoreLimits{3, 3, 5};
constexpr std::array<int, ArcadeCupRoundCount> ArcadeCupArenas{0, 2, 3};

enum class TrainingFeed {
    Mixed,
    Lob,
    Fast,
    CrossCourt,
};

enum class AcademyLesson : int {
    BoostGates,
    DoubleJump,
    AerialReturn,
    TargetLanding,
};

constexpr int AcademyLessonCount = 4;
constexpr std::array<const char *, AcademyLessonCount> AcademyLessonNames{
    "BOOST GATES",
    "DOUBLE-JUMP LAUNCH",
    "AERIAL RETURN",
    "TARGET LANDING",
};
constexpr std::array<float, AcademyLessonCount> AcademyLessonDurations{30.0F, 22.0F, 35.0F, 38.0F};
constexpr std::array<Vec3, 3> AcademyGatePositions{
    Vec3{0.0F, 0.08F, 13.0F}, Vec3{-5.0F, 0.08F, 5.0F}, Vec3{4.5F, 0.08F, 4.0F}};

constexpr int CareerMilestoneCount = 10;
constexpr std::array<const char *, CareerMilestoneCount> CareerMilestoneNames{
    "FIRST WHISTLE", "WIN COLUMN", "RALLY READY", "AIRBORNE ACE", "PAD RUNNER",
    "POWER PLAYER", "TARGET ACE", "ACADEMY GRAD", "GOLD STANDARD", "CUP CHAMPION"};
constexpr std::array<const char *, CareerMilestoneCount> CareerMilestoneDescriptions{
    "FINISH 1 MATCH", "WIN 1 MATCH", "REACH A 6-TOUCH RALLY", "MAKE 5 TEAM AERIAL TOUCHES",
    "COLLECT 25 TEAM BOOST PADS", "USE 5 TEAM POWER-UPS", "SCORE 500 IN TARGET RUN",
    "FINISH ROCKET ACADEMY", "EARN ACADEMY GOLD", "WIN THE ARCADE CUP"};
constexpr std::array<int, CareerMilestoneCount> CareerMilestoneXp{
    100, 150, 175, 200, 150, 150, 225, 200, 275, 400};
constexpr std::array<const char *, 6> CareerRankNames{
    "ROOKIE", "PROSPECT", "STRIKER", "AERIALIST", "ALL-STAR", "ROCKET LEGEND"};
constexpr std::array<int, 6> CareerRankThresholds{0, 300, 750, 1500, 2800, 4500};

enum class TouchResult {
    Normal,
    PowerReady,
    Fault,
};

enum class Powerup : std::uint8_t {
    None,
    Haymaker,
    Freezer,
    Magnetizer,
};

enum class RespawnCause : std::uint8_t {
    None,
    OutOfBounds,
    Demolition,
};

struct Controls {
    float throttle = 0.0F;
    float steer = 0.0F;
    bool jumpPressed = false;
    bool dodgePressed = false;
    bool boostHeld = false;
    bool powerupPressed = false;
};

enum class BindAction : std::size_t {
    Forward,
    Reverse,
    SteerLeft,
    SteerRight,
    Jump,
    Boost,
    Dodge,
    Powerup,
    Camera,
    Pause,
    Restart,
    MainMenu,
    Count,
};

constexpr std::size_t BindingCount = static_cast<std::size_t>(BindAction::Count);
constexpr std::array<const char *, BindingCount> BindingLabels{
    "DRIVE FORWARD",
    "REVERSE / NOSE UP",
    "STEER LEFT",
    "STEER RIGHT",
    "JUMP / DOUBLE JUMP",
    "BOOST",
    "DIRECTIONAL DODGE",
    "USE POWER-UP",
    "TOGGLE CAMERA",
    "PAUSE",
    "RESTART MATCH",
    "MAIN MENU",
};
constexpr std::array<const char *, BindingCount> BindingSettingNames{
    "forward", "reverse", "steer_left", "steer_right", "jump", "boost",
    "dodge", "powerup", "camera", "pause", "restart", "main_menu"};
constexpr std::array<int, BindingCount> DefaultBindings{
    KEY_W,
    KEY_S,
    KEY_A,
    KEY_D,
    KEY_SPACE,
    KEY_LEFT_SHIFT,
    KEY_E,
    KEY_Q,
    KEY_C,
    KEY_ESCAPE,
    KEY_R,
    KEY_M,
};

struct Car {
    BodyHandle body = InvalidBody;
    int team = 0;
    int slot = 0;
    bool human = false;
    float heading = 0.0F;
    float boost = 100.0F;
    float jumpCooldown = 0.0F;
    float airborneTime = 0.0F;
    float landingImpact = 0.0F;
    float aiThinkTimer = 0.0F;
    float contactSeparation = 1.0F;
    float recoveryTimer = 0.0F;
    float dodgeTimer = 0.0F;
    float groundedGrace = 0.0F;
    float jumpBuffer = 0.0F;
    Vec3 aiTarget{};
    bool aiShotCommitted = false;
    bool aiYielding = false;
    float touchRecovery = 0.0F;
    int jumpsUsed = 0;
    bool dodgeAvailable = true;
    Powerup heldPowerup = Powerup::None;
    float powerupGrantTimer = PowerupInitialDelay;
    float magnetTimer = 0.0F;
    float respawnTimer = 0.0F;
    RespawnCause respawnCause = RespawnCause::None;
    int demolishedBy = -1;
    int bodyStyle = 0;
    Color paint = WHITE;
    Color wheelColor{18, 20, 25, 255};
    Color spoilerColor = GOLD;
    Color decalColor = RAYWHITE;
    Color boostColor = GOLD;
};

struct ColorChoice {
    const char *name;
    Color color;
};

struct BodyStyleChoice {
    const char *name;
    Vector3 bodyScale;
    Vector3 cabinScale;
    Vector3 cabinOffset;
};

struct ArenaTheme {
    const char *name;
    Color skyTop;
    Color skyBottom;
    Color floor;
    Color blueCourt;
    Color orangeCourt;
    Color accent;
    Color cage;
};

constexpr std::array<ArenaTheme, 4> ArenaThemes{{
    {"PINEWOOD PARK", {16, 48, 42, 255}, {42, 91, 59, 255}, {34, 64, 48, 255},
        {27, 112, 103, 255}, {127, 73, 50, 255}, {245, 211, 84, 255}, {101, 195, 139, 115}},
    {"SUNSPLASH BEACH CLUB", {44, 151, 196, 255}, {248, 183, 103, 255}, {164, 135, 82, 255},
        {27, 149, 185, 255}, {230, 104, 66, 255}, {255, 230, 112, 255}, {97, 225, 219, 115}},
    {"MIDNIGHT CITY COURT", {9, 15, 35, 255}, {35, 28, 66, 255}, {38, 43, 62, 255},
        {35, 105, 163, 255}, {132, 52, 121, 255}, {146, 241, 112, 255}, {118, 148, 236, 115}},
    {"EMBER CANYON STADIUM", {91, 36, 22, 255}, {218, 104, 48, 255}, {89, 53, 39, 255},
        {44, 91, 119, 255}, {154, 54, 28, 255}, {255, 199, 78, 255}, {255, 128, 74, 115}},
}};

constexpr std::array<ColorChoice, 8> BodyColors{{
    {"NEON CYAN", {43, 199, 255, 255}},
    {"ARCADE BLUE", {80, 112, 255, 255}},
    {"HOT PINK", {255, 76, 174, 255}},
    {"LIME SHOCK", {132, 238, 76, 255}},
    {"SUNSET", {255, 132, 54, 255}},
    {"VIOLET", {157, 91, 255, 255}},
    {"PEARL", {232, 238, 245, 255}},
    {"MIDNIGHT", {35, 45, 68, 255}},
}};

constexpr std::array<ColorChoice, 6> WheelColors{{
    {"GRAPHITE", {18, 20, 25, 255}},
    {"CHROME", {185, 196, 205, 255}},
    {"GOLD", {255, 202, 44, 255}},
    {"CYAN", {35, 222, 255, 255}},
    {"MAGENTA", {255, 70, 190, 255}},
    {"WHITE", {245, 247, 250, 255}},
}};

constexpr std::array<ColorChoice, 6> SpoilerColors{{
    {"GOLD", {255, 202, 44, 255}},
    {"BODY MATCH", {43, 199, 255, 255}},
    {"CARBON", {28, 32, 40, 255}},
    {"WHITE", {245, 247, 250, 255}},
    {"ORANGE", {255, 132, 54, 255}},
    {"PURPLE", {172, 92, 255, 255}},
}};

constexpr std::array<BodyStyleChoice, 5> BodyStyles{{
    {"STRIKER", {1.84F, 0.90F, 2.84F}, {1.35F, 0.58F, 1.25F}, {0.0F, 0.58F, -0.12F}},
    {"RALLY", {1.90F, 1.00F, 2.66F}, {1.48F, 0.72F, 1.35F}, {0.0F, 0.66F, -0.08F}},
    {"WEDGE", {1.94F, 0.72F, 3.02F}, {1.22F, 0.50F, 1.18F}, {0.0F, 0.47F, -0.30F}},
    {"MUSCLE", {2.02F, 0.94F, 2.94F}, {1.42F, 0.54F, 1.10F}, {0.0F, 0.59F, -0.38F}},
    {"BUGGY", {1.72F, 0.78F, 2.62F}, {1.14F, 0.66F, 1.02F}, {0.0F, 0.58F, 0.05F}},
}};

constexpr std::array<ColorChoice, 8> DecalColors{{
    {"ARCTIC", {225, 245, 255, 255}},
    {"GOLD", {255, 202, 44, 255}},
    {"MAGENTA", {255, 70, 190, 255}},
    {"LIME", {132, 238, 76, 255}},
    {"EMBER", {255, 104, 54, 255}},
    {"VIOLET", {172, 92, 255, 255}},
    {"BLACK", {18, 22, 31, 255}},
    {"BODY MATCH", {43, 199, 255, 255}},
}};

constexpr std::array<ColorChoice, 7> BoostColors{{
    {"SOLAR", {255, 202, 44, 255}},
    {"ION BLUE", {43, 199, 255, 255}},
    {"PLASMA", {221, 92, 255, 255}},
    {"LIME", {132, 238, 76, 255}},
    {"EMBER", {255, 104, 54, 255}},
    {"WHITE HOT", {245, 247, 250, 255}},
    {"CRIMSON", {255, 55, 76, 255}},
}};

constexpr std::array<Vec3, 8> BoostPadPositions{{
    {-8.4F, 0.04F, -16.2F}, {8.4F, 0.04F, -16.2F}, {-8.4F, 0.04F, 16.2F}, {8.4F, 0.04F, 16.2F},
    {-7.0F, 0.04F, -7.2F}, {7.0F, 0.04F, -7.2F}, {-7.0F, 0.04F, 7.2F}, {7.0F, 0.04F, 7.2F}}};
constexpr std::size_t LargeBoostPadCount = 4;
constexpr float LargeBoostPadRespawn = 10.0F;
constexpr float SmallBoostPadRespawn = 4.0F;
constexpr float SmallBoostPickup = 28.0F;

struct Particle {
    Vec3 position{};
    Vec3 velocity{};
    float life = 0.0F;
    float initialLife = 0.0F;
    Color color = WHITE;
    float size = 0.12F;
};

struct TrailPoint {
    Vec3 position{};
    float life = 0.0F;
    float initialLife = 0.0F;
    float size = 0.1F;
    Color color = GOLD;
};

struct ReplayFrame {
    Transform ball{};
    std::array<Transform, MaximumCars> cars{};
    std::uint8_t visibleCars = 0;
};

struct AcademyGhostFrame {
    float time = 0.0F;
    AcademyLesson lesson = AcademyLesson::BoostGates;
    Transform car{};
};

constexpr float AcademyGhostCaptureInterval = 1.0F / 20.0F;
constexpr std::size_t AcademyGhostMaximumFrames = 12000;

} // namespace

struct Game::Impl {
    PhysicsWorld physics;
    AudioKit audio;
    PlayerPreferences preferences;
    bool preferencesFromPause = false;
    Model cubeModel{};
    Model ballModel{};
    RenderTexture2D previewTarget{};
    RenderTexture2D sceneTarget{};
    std::array<RenderTexture2D, 2> localCoopTargets{};
    BodyHandle ball = InvalidBody;
    BodyHandle courtFloor = InvalidBody;
    std::array<InputEdges, 2> inputEdges{};
    BallFlight predictedFlight{};
    std::array<TeamPlan, 2> teamPlans{};
    float tacticsTimer = 0.0F;
    bool academyAerialTouch = false;
    CameraMode playerTwoCameraMode = CameraMode::Car;
    std::array<Car, MaximumCars> cars{};
    std::vector<Particle> particles;
    std::vector<TrailPoint> ballTrail;
    std::deque<ReplayFrame> replayFrames;
    std::vector<AcademyGhostFrame> academyCurrentGhost;
    std::vector<AcademyGhostFrame> academyBestGhost;
    Camera3D camera{};
    std::array<Camera3D, 2> localCoopCameras{};
    MatchState state = MatchState::Title;
    MatchState pausedFrom = MatchState::Playing;
    MenuPage menuPage = MenuPage::Main;
    Difficulty difficulty = Difficulty::Pro;
    GameMode gameMode = GameMode::Match;
    GameMode pendingGameMode = GameMode::Match;
    TrainingFeed trainingFeed = TrainingFeed::Mixed;
    TrainingFeed currentTrainingFeed = TrainingFeed::Mixed;
    AcademyLesson academyLesson = AcademyLesson::BoostGates;
    CameraMode cameraMode = CameraMode::Car;
    std::array<int, 2> score{0, 0};
    std::array<int, 2> teamTouches{0, 0};
    std::array<int, 2> matchTouches{0, 0};
    std::array<int, 2> matchAerialTouches{0, 0};
    std::array<int, 2> matchBoostPickups{0, 0};
    std::array<float, 2> matchBoostSpent{0.0F, 0.0F};
    std::array<int, 2> matchPowerupsUsed{0, 0};
    std::array<int, 2> matchDemolitions{0, 0};
    std::array<float, BoostPadPositions.size()> boostPadRespawnTimers{};
    float fastestBallSpeed = 0.0F;
    float matchTime = 180.0F;
    float rallyTime = 0.0F;
    float pointTimer = 0.0F;
    float replayPlaybackFrame = 0.0F;
    float trainingResetTimer = 0.0F;
    float academyLessonTimer = 0.0F;
    float academyRunTimer = 0.0F;
    float academyTransitionTimer = 0.0F;
    float academyPeakHeight = 0.0F;
    float academyGhostCaptureTimer = 0.0F;
    float academyLastSplitDelta = 0.0F;
    float academyResultDelta = 0.0F;
    float ballTrailTimer = 0.0F;
    float serveCountdown = 3.0F;
    float loadingTimer = 0.0F;
    float cameraModeNotice = 0.0F;
    float accumulator = 0.0F;
    float totalTime = 0.0F;
    float shake = 0.0F;
    float hitSoundCooldown = 0.0F;
    float boostSoundCooldown = 0.0F;
    float ballElasticity = 0.82F;
    float ballFreezeTimer = 0.0F;
    Vec3 frozenBallVelocity{};
    int matchDurationSeconds = 180;
    int scoreLimit = 7;
    int powerupSequence = 0;
    Vec3 servePosition{};
    Vec3 serveVelocity{};
    Vec3 serveLandingTarget{};
    Vec3 challengeTarget{};
    Vec3 previousBallVelocity{};
    int mainMenuIndex = 0;
    int matchSetupMenuIndex = 0;
    int customizeMenuIndex = 0;
    int controlsMenuIndex = 0;
    int aboutMenuIndex = 0;
    int preferencesMenuIndex = 0;
    int bodyColorIndex = 0;
    int bodyStyleIndex = 0;
    int wheelColorIndex = 0;
    int spoilerColorIndex = 0;
    int decalColorIndex = 0;
    int boostColorIndex = 0;
    int arenaSelection = 0;
    int activeArenaIndex = 0;
    int countdownCue = 3;
    int scoringTeam = 0;
    int winner = 0;
    int receivingTeam = 0;
    int receivingCar = 0;
    int servingTeam = 1;
    int servingCar = 3;
    std::array<int, 2> rotationStriker{0, 3};
    std::array<int, 2> serveRotation{-1, -1};
    std::array<int, 2> receiveRotation{-1, -1};
    bool receivePending = false;
    int rallyTouches = 0;
    int bestRallyTouches = 0;
    int trainingAttempts = 0;
    int trainingReturns = 0;
    int trainingCompleted = 0;
    int trainingStreak = 0;
    int trainingBestStreak = 0;
    bool trainingRepeatShot = false;
    int challengeScore = 0;
    int challengeCombo = 0;
    int challengeBestCombo = 0;
    int challengeTargetsHit = 0;
    int challengeBestScore = 0;
    int challengeRecordCombo = 0;
    int academyGateIndex = 0;
    int academyLessonsCompleted = 0;
    int academyRetries = 0;
    int academyRunMedal = 0;
    int academyBestMedal = 0;
    int academyBestTimeCentiseconds = 0;
    int academyCompletions = 0;
    int careerXp = 0;
    int careerMatches = 0;
    int careerWins = 0;
    int careerPoints = 0;
    int careerTouches = 0;
    int careerAerials = 0;
    int careerBoostPads = 0;
    int careerPowerups = 0;
    int careerBestRally = 0;
    int careerMilestones = 0;
    int careerLastXpAward = 0;
    int arcadeCupRound = 0;
    int arcadeCupMatchWins = 0;
    int arcadeCupBestRound = 0;
    int arcadeCupTitles = 0;
    Difficulty arcadeCupSavedDifficulty = Difficulty::Pro;
    int arcadeCupSavedDuration = 180;
    int arcadeCupSavedScoreLimit = 7;
    int arcadeCupSavedArenaSelection = 0;
    int arcadeCupSavedActiveArena = 0;
    std::array<int, BindingCount> bindings = DefaultBindings;
    int bindingCaptureIndex = -1;
    std::string settingsNotice;
    float settingsNoticeTimer = 0.0F;
    float touchNoticeTimer = 0.0F;
    float thirdTouchBoostTimer = 0.0F;
    std::string touchNotice;
    std::string careerUnlockNotice;
    float careerUnlockTimer = 0.0F;
    int lastTouchTeam = -1;
    int pendingThirdTouchBoostTeam = -1;
    float previousBallZ = 0.0F;
    std::uint32_t simulationTick = 0;
    std::uint32_t snapshotSequence = 0;
    std::uint32_t localInputSequence = 0;
    bool pendingGameOver = false;
    bool overtime = false;
    bool showHelp = false;
    bool helpPausedGame = false;
    std::string pauseNotice;
    bool controllerWasConnected = false;
    bool shouldExit = false;
    bool automatedPlayer = false;
    bool serveInProgress = false;
    bool trainingBallHasTouchedGround = false;
    bool trainingReturnSuccessful = false;
    bool challengeNewRecord = false;
    bool academyActive = false;
    bool academyNewRecord = false;
    bool academyBoostUsed = false;
    bool academyGhostEnabled = true;
    bool academyGhostSaved = false;
    bool academyRecordEligible = true;
    bool powerupsEnabled = false;
    bool arcadeCupActive = false;
    bool arcadeCupResultRecorded = false;
    bool careerMatchRecorded = false;
    bool smokeTestMode = false;
    bool headlessTestMode = false;
    std::mt19937 cosmeticGenerator{0x524F434BU};
    bool scriptedAerialTest = false;
    bool aerialFirstJumpTriggered = false;
    bool aerialSecondJumpTriggered = false;
    bool aerialTestComplete = false;
    bool scriptedSprintTest = false;
    bool sprintTestComplete = false;
    bool sprintCompletedCourse = false;
    float aerialTestTimer = 0.0F;
    float aerialPeakHeight = 0.0F;
    float aerialPeakForwardY = 0.0F;
    int aerialMaxJumpsUsed = 0;
    float sprintTestTimer = 0.0F;
    float sprintFinishTime = 0.0F;

    explicit Impl(bool smokeTest, bool headlessTest) : smokeTestMode(smokeTest || headlessTest), headlessTestMode(headlessTest) {
        if (headlessTestMode) {
            SetRandomSeed(20260917);
            createArena();
            createActors();
            resetRound(1);
            return;
        }
        unsigned int windowFlags = FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE;
        if (smokeTestMode) {
            windowFlags |= FLAG_WINDOW_HIDDEN;
        }
        SetConfigFlags(windowFlags);
        InitWindow(ScreenWidth, ScreenHeight, "Rocket Volley");
        if (!IsWindowReady()) {
            throw std::runtime_error("raylib could not create a window");
        }
        if (smokeTestMode) {
            TraceLog(LOG_INFO, "SMOKE: hidden window=%s", IsWindowHidden() ? "true" : "false");
        }
        SetExitKey(KEY_NULL);
        SetWindowMinSize(640, 360);
        SetTargetFPS(120);
        audio.initialize();

        cubeModel = LoadModelFromMesh(GenMeshCube(1.0F, 1.0F, 1.0F));
        ballModel = LoadModelFromMesh(GenMeshSphere(BallRadius, 12, 8));
        previewTarget = LoadRenderTexture(560, 380);
        sceneTarget = LoadRenderTexture(ScreenWidth, ScreenHeight);
        SetTextureFilter(sceneTarget.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(previewTarget.texture, TEXTURE_FILTER_BILINEAR);
        for (RenderTexture2D &target : localCoopTargets) {
            target = LoadRenderTexture(ScreenWidth / 2, ScreenHeight);
            SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
        }

        camera.position = {0.0F, 8.0F, 18.0F};
        camera.target = {0.0F, 1.5F, 2.0F};
        camera.up = {0.0F, 1.0F, 0.0F};
        camera.fovy = 58.0F;
        camera.projection = CAMERA_PERSPECTIVE;
        for (Camera3D &localCamera : localCoopCameras) {
            localCamera = camera;
            localCamera.fovy = 66.0F;
        }

        loadSettings();
        loadAcademyGhost();
        createArena();
        createActors();
        resetRound(1);
    }

    ~Impl() {
        if (IsWindowReady()) {
            for (RenderTexture2D &target : localCoopTargets) {
                UnloadRenderTexture(target);
            }
            UnloadRenderTexture(previewTarget);
            UnloadRenderTexture(sceneTarget);
            UnloadModel(ballModel);
            UnloadModel(cubeModel);
            audio.shutdown();
            CloseWindow();
        }
    }

    static std::size_t bindingIndex(BindAction action) {
        return static_cast<std::size_t>(action);
    }

    int boundKey(BindAction action) const {
        return bindings[bindingIndex(action)];
    }

    std::string keyName(int key) const {
        switch (key) {
        case KEY_SPACE: return "SPACE";
        case KEY_ESCAPE: return "ESC";
        case KEY_ENTER: return "ENTER";
        case KEY_TAB: return "TAB";
        case KEY_BACKSPACE: return "BACKSPACE";
        case KEY_INSERT: return "INSERT";
        case KEY_DELETE: return "DELETE";
        case KEY_RIGHT: return "RIGHT ARROW";
        case KEY_LEFT: return "LEFT ARROW";
        case KEY_DOWN: return "DOWN ARROW";
        case KEY_UP: return "UP ARROW";
        case KEY_PAGE_UP: return "PAGE UP";
        case KEY_PAGE_DOWN: return "PAGE DOWN";
        case KEY_HOME: return "HOME";
        case KEY_END: return "END";
        case KEY_CAPS_LOCK: return "CAPS LOCK";
        case KEY_SCROLL_LOCK: return "SCROLL LOCK";
        case KEY_NUM_LOCK: return "NUM LOCK";
        case KEY_PRINT_SCREEN: return "PRINT SCREEN";
        case KEY_PAUSE: return "PAUSE";
        case KEY_LEFT_SHIFT: return "LEFT SHIFT";
        case KEY_LEFT_CONTROL: return "LEFT CTRL";
        case KEY_LEFT_ALT: return "LEFT ALT";
        case KEY_LEFT_SUPER: return "LEFT SUPER";
        case KEY_RIGHT_SHIFT: return "RIGHT SHIFT";
        case KEY_RIGHT_CONTROL: return "RIGHT CTRL";
        case KEY_RIGHT_ALT: return "RIGHT ALT";
        case KEY_RIGHT_SUPER: return "RIGHT SUPER";
        default: break;
        }

        std::string name = GetKeyName(key);
        if (name.empty()) {
            name = TextFormat("KEY %d", key);
        }
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
        return name;
    }

    int careerRankIndexForXp(int xp) const {
        int rank = 0;
        for (int index = 1; index < static_cast<int>(CareerRankThresholds.size()); ++index) {
            if (xp < CareerRankThresholds[static_cast<std::size_t>(index)]) {
                break;
            }
            rank = index;
        }
        return rank;
    }

    int careerRankIndex() const {
        return careerRankIndexForXp(careerXp);
    }

    const char *careerRankName() const {
        return CareerRankNames[static_cast<std::size_t>(careerRankIndex())];
    }

    int unlockedCareerMilestoneCount() const {
        int count = 0;
        for (int index = 0; index < CareerMilestoneCount; ++index) {
            count += (careerMilestones & (1 << index)) != 0 ? 1 : 0;
        }
        return count;
    }

    bool careerMilestoneCondition(int index) const {
        switch (index) {
        case 0: return careerMatches >= 1;
        case 1: return careerWins >= 1;
        case 2: return careerBestRally >= 6;
        case 3: return careerAerials >= 5;
        case 4: return careerBoostPads >= 25;
        case 5: return careerPowerups >= 5;
        case 6: return challengeBestScore >= 500;
        case 7: return academyCompletions >= 1;
        case 8: return academyBestMedal >= 3;
        case 9: return arcadeCupTitles >= 1;
        default: return false;
        }
    }

    int unlockCareerMilestones(std::string *firstUnlock = nullptr, int *unlockCount = nullptr) {
        int awardedXp = 0;
        int count = 0;
        for (int index = 0; index < CareerMilestoneCount; ++index) {
            const int bit = 1 << index;
            if ((careerMilestones & bit) != 0 || !careerMilestoneCondition(index)) {
                continue;
            }
            careerMilestones |= bit;
            awardedXp += CareerMilestoneXp[static_cast<std::size_t>(index)];
            if (count == 0 && firstUnlock != nullptr) {
                *firstUnlock = CareerMilestoneNames[static_cast<std::size_t>(index)];
            }
            ++count;
        }
        careerXp += awardedXp;
        if (unlockCount != nullptr) {
            *unlockCount = count;
        }
        return awardedXp;
    }

    void commitCareerProgress(int baseXp, const std::string &source) {
        const int oldRank = careerRankIndex();
        const int safeBaseXp = std::max(0, baseXp);
        careerXp += safeBaseXp;
        std::string firstUnlock;
        int unlockCount = 0;
        const int milestoneXp = unlockCareerMilestones(&firstUnlock, &unlockCount);
        careerLastXpAward += safeBaseXp + milestoneXp;
        const int newRank = careerRankIndex();
        if (newRank > oldRank) {
            careerUnlockNotice = std::string("RANK UP  /  ") + CareerRankNames[static_cast<std::size_t>(newRank)];
            if (unlockCount > 0) {
                careerUnlockNotice += TextFormat("  /  %d MILESTONE%s", unlockCount, unlockCount == 1 ? "" : "S");
            }
        } else if (unlockCount > 0) {
            careerUnlockNotice = std::string("MILESTONE  /  ") + firstUnlock;
            if (unlockCount > 1) {
                careerUnlockNotice += TextFormat("  +%d MORE", unlockCount - 1);
            }
        } else {
            careerUnlockNotice = source + TextFormat("  /  +%d XP", safeBaseXp);
        }
        careerUnlockTimer = 4.0F;
        saveSettings(source + TextFormat("  /  +%d XP", safeBaseXp + milestoneXp));
    }

    void recordCareerMatch() {
        if (careerMatchRecorded || !competitiveMode()) {
            return;
        }
        careerMatchRecorded = true;
        ++careerMatches;
        if (winner == 0) {
            ++careerWins;
        }
        careerPoints += score[0];
        careerTouches += matchTouches[0];
        careerAerials += matchAerialTouches[0];
        careerBoostPads += matchBoostPickups[0];
        careerPowerups += matchPowerupsUsed[0];
        careerBestRally = std::max(careerBestRally, bestRallyTouches);
        const int matchXp = 80 + (winner == 0 ? 50 : 0) + score[0] * 12
            + std::min(matchTouches[0], 20) * 3 + matchAerialTouches[0] * 12
            + std::min(bestRallyTouches, 12) * 4;
        commitCareerProgress(matchXp, winner == 0 ? "MATCH WIN" : "MATCH COMPLETE");
    }

    std::filesystem::path settingsPath() const {
#if defined(_WIN32)
        if (const char *appData = std::getenv("APPDATA")) {
            return std::filesystem::path(appData) / "RocketVolley" / "settings.cfg";
        }
#else
        if (const char *xdgConfig = std::getenv("XDG_CONFIG_HOME")) {
            return std::filesystem::path(xdgConfig) / "RocketVolley" / "settings.cfg";
        }
        if (const char *userHome = std::getenv("HOME")) {
            return std::filesystem::path(userHome) / ".config" / "RocketVolley" / "settings.cfg";
        }
#endif
        return std::filesystem::path(GetApplicationDirectory()) / "settings.cfg";
    }

    std::filesystem::path academyGhostPath() const {
        return settingsPath().parent_path() / "academy_best.ghost";
    }

    std::string encodeAcademyGhost(const std::vector<AcademyGhostFrame> &frames, int timeCentiseconds) const {
        if (frames.empty() || frames.size() > AcademyGhostMaximumFrames
            || timeCentiseconds <= 0 || timeCentiseconds > 60000) {
            return {};
        }
        std::ostringstream output;
        output.precision(9);
        output << "RVGHOST 1 " << timeCentiseconds << ' ' << frames.size() << '\n';
        for (const AcademyGhostFrame &frame : frames) {
            output << static_cast<int>(frame.lesson) << ' ' << frame.time << ' '
                << frame.car.position.x << ' ' << frame.car.position.y << ' ' << frame.car.position.z << ' '
                << frame.car.rotation.x << ' ' << frame.car.rotation.y << ' '
                << frame.car.rotation.z << ' ' << frame.car.rotation.w << '\n';
        }
        return output.str();
    }

    bool decodeAcademyGhost(
        const std::string &encoded,
        std::vector<AcademyGhostFrame> &decodedFrames,
        int &decodedTimeCentiseconds) const {
        std::istringstream input(encoded);
        std::string magic;
        int version = 0;
        int timeCentiseconds = 0;
        std::size_t frameCount = 0;
        if (!(input >> magic >> version >> timeCentiseconds >> frameCount)
            || magic != "RVGHOST" || version != 1
            || timeCentiseconds <= 0 || timeCentiseconds > 60000
            || frameCount == 0 || frameCount > AcademyGhostMaximumFrames) {
            return false;
        }

        std::vector<AcademyGhostFrame> frames;
        frames.reserve(frameCount);
        float previousTime = -1.0F;
        int previousLesson = -1;
        for (std::size_t index = 0; index < frameCount; ++index) {
            int lesson = -1;
            AcademyGhostFrame frame;
            if (!(input >> lesson >> frame.time
                    >> frame.car.position.x >> frame.car.position.y >> frame.car.position.z
                    >> frame.car.rotation.x >> frame.car.rotation.y
                    >> frame.car.rotation.z >> frame.car.rotation.w)) {
                return false;
            }
            const float rotationLength = std::sqrt(
                frame.car.rotation.x * frame.car.rotation.x
                + frame.car.rotation.y * frame.car.rotation.y
                + frame.car.rotation.z * frame.car.rotation.z
                + frame.car.rotation.w * frame.car.rotation.w);
            const bool finite = std::isfinite(frame.time)
                && std::isfinite(frame.car.position.x) && std::isfinite(frame.car.position.y)
                && std::isfinite(frame.car.position.z) && std::isfinite(rotationLength);
            if (!finite || lesson < 0 || lesson >= AcademyLessonCount
                || lesson < previousLesson || frame.time < 0.0F || frame.time < previousTime
                || frame.time > static_cast<float>(timeCentiseconds) * 0.01F + 0.02F
                || std::abs(frame.car.position.x) > 60.0F
                || frame.car.position.y < -10.0F || frame.car.position.y > 40.0F
                || std::abs(frame.car.position.z) > 60.0F
                || rotationLength < 0.5F || rotationLength > 1.5F) {
                return false;
            }
            frame.lesson = static_cast<AcademyLesson>(lesson);
            frame.car.rotation.x /= rotationLength;
            frame.car.rotation.y /= rotationLength;
            frame.car.rotation.z /= rotationLength;
            frame.car.rotation.w /= rotationLength;
            previousTime = frame.time;
            previousLesson = lesson;
            frames.push_back(frame);
        }
        std::string trailing;
        if (input >> trailing) {
            return false;
        }
        decodedFrames = std::move(frames);
        decodedTimeCentiseconds = timeCentiseconds;
        return true;
    }

    bool saveAcademyGhost() {
        if (smokeTestMode) {
            academyGhostSaved = !academyBestGhost.empty();
            return academyGhostSaved;
        }
        const std::string encoded = encodeAcademyGhost(academyBestGhost, academyBestTimeCentiseconds);
        if (encoded.empty()) {
            academyGhostSaved = false;
            return false;
        }
        academyGhostSaved = writeSaveFile(academyGhostPath(), encoded);
        return academyGhostSaved;
    }

    void loadAcademyGhost() {
        academyBestGhost.clear();
        academyGhostSaved = false;
        if (smokeTestMode || academyBestTimeCentiseconds <= 0) {
            return;
        }
        std::ifstream input(academyGhostPath(), std::ios::binary);
        if (!input) {
            return;
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        int ghostTimeCentiseconds = 0;
        std::vector<AcademyGhostFrame> frames;
        if (decodeAcademyGhost(buffer.str(), frames, ghostTimeCentiseconds)
            && ghostTimeCentiseconds == academyBestTimeCentiseconds) {
            academyBestGhost = std::move(frames);
            academyGhostSaved = true;
        }
    }

    bool bindingsAreUnique(const std::array<int, BindingCount> &candidate) const {
        for (std::size_t first = 0; first < candidate.size(); ++first) {
            if (candidate[first] < KEY_SPACE || candidate[first] > KEY_KB_MENU
                || candidate[first] == KEY_F1 || candidate[first] == KEY_F11 || candidate[first] == KEY_ENTER) {
                return false;
            }
            for (std::size_t second = first + 1; second < candidate.size(); ++second) {
                if (candidate[first] == candidate[second]) {
                    return false;
                }
            }
        }
        return true;
    }

    void loadSettings() {
        if (smokeTestMode) {
            return;
        }

        std::ifstream input(settingsPath());
        if (!input) {
            return;
        }

        readSettings(input);
    }

    void readSettings(std::istream &input) {
        auto loadedBindings = bindings;
        std::string line;
        while (std::getline(input, line)) {
            const std::size_t separator = line.find('=');
            if (separator == std::string::npos) {
                continue;
            }
            const std::string name = line.substr(0, separator);
            std::istringstream valueStream(line.substr(separator + 1));
            int value = 0;
            std::string trailing;
            if (!(valueStream >> value) || (valueStream >> trailing)) {
                continue; // Ignore malformed values instead of accepting a valid numeric prefix.
            }
            preferences.readSetting(name, value);
            for (std::size_t index = 0; index < BindingCount; ++index) {
                if (name == BindingSettingNames[index]) {
                    loadedBindings[index] = value;
                }
            }
            if (name == "body") bodyColorIndex = value;
            if (name == "body_style") bodyStyleIndex = value;
            if (name == "wheels") wheelColorIndex = value;
            if (name == "spoiler") spoilerColorIndex = value;
            if (name == "decal") decalColorIndex = value;
            if (name == "boost_fx") boostColorIndex = value;
            if (name == "arena") arenaSelection = value;
            if (name == "difficulty") difficulty = value == 0 ? Difficulty::Rookie : Difficulty::Pro;
            if (name == "ball_bounce") ballElasticity = clamp(static_cast<float>(value) / 100.0F, 0.55F, 0.95F);
            if (name == "match_seconds" && (value == 120 || value == 180 || value == 300)) matchDurationSeconds = value;
            if (name == "score_limit" && (value == 3 || value == 5 || value == 7 || value == 9)) scoreLimit = value;
            if (name == "powerups") powerupsEnabled = value != 0;
            if (name == "challenge_best") challengeBestScore = std::max(0, value);
            if (name == "challenge_combo") challengeRecordCombo = std::max(0, value);
            if (name == "arcade_cup_best") arcadeCupBestRound = std::clamp(value, 0, ArcadeCupRoundCount);
            if (name == "arcade_cup_titles") arcadeCupTitles = std::max(0, value);
            if (name == "academy_medal") academyBestMedal = std::clamp(value, 0, 3);
            if (name == "academy_time_cs") academyBestTimeCentiseconds = std::max(0, value);
            if (name == "academy_completions") academyCompletions = std::max(0, value);
            if (name == "academy_ghost_enabled") academyGhostEnabled = value != 0;
            if (name == "career_xp") careerXp = std::max(0, value);
            if (name == "career_matches") careerMatches = std::max(0, value);
            if (name == "career_wins") careerWins = std::max(0, value);
            if (name == "career_points") careerPoints = std::max(0, value);
            if (name == "career_touches") careerTouches = std::max(0, value);
            if (name == "career_aerials") careerAerials = std::max(0, value);
            if (name == "career_boost_pads") careerBoostPads = std::max(0, value);
            if (name == "career_powerups") careerPowerups = std::max(0, value);
            if (name == "career_best_rally") careerBestRally = std::max(0, value);
            if (name == "career_milestones") careerMilestones = std::clamp(value, 0, (1 << CareerMilestoneCount) - 1);
        }

        audio.applyMix(preferences.musicGain(), preferences.effectsGain());
        bodyColorIndex = std::clamp(bodyColorIndex, 0, static_cast<int>(BodyColors.size()) - 1);
        bodyStyleIndex = std::clamp(bodyStyleIndex, 0, static_cast<int>(BodyStyles.size()) - 1);
        wheelColorIndex = std::clamp(wheelColorIndex, 0, static_cast<int>(WheelColors.size()) - 1);
        spoilerColorIndex = std::clamp(spoilerColorIndex, 0, static_cast<int>(SpoilerColors.size()) - 1);
        decalColorIndex = std::clamp(decalColorIndex, 0, static_cast<int>(DecalColors.size()) - 1);
        boostColorIndex = std::clamp(boostColorIndex, 0, static_cast<int>(BoostColors.size()) - 1);
        arenaSelection = std::clamp(arenaSelection, 0, static_cast<int>(ArenaThemes.size()));
        if (arenaSelection > 0) {
            activeArenaIndex = arenaSelection - 1;
        }
        bindings = bindingsAreUnique(loadedBindings) ? loadedBindings : DefaultBindings;
        const int previousMilestones = careerMilestones;
        unlockCareerMilestones();
        if (careerMilestones != previousMilestones) {
            saveSettings("PILOT RECORD MIGRATED");
        }
    }

    void saveSettings(const std::string &notice = "SETTINGS SAVED") {
        settingsNotice = notice;
        settingsNoticeTimer = 2.2F;
        if (smokeTestMode) {
            return;
        }

        const std::string encoded = encodeSettings();
        if (encoded.empty() || !writeSaveFile(settingsPath(), encoded)) {
            settingsNotice = "COULD NOT SAVE SETTINGS";
        }
    }

    std::string encodeSettings() const {
        std::ostringstream output;
        output << "version=3\n";
        preferences.writeSettings(output);
        for (std::size_t index = 0; index < BindingCount; ++index) {
            output << BindingSettingNames[index] << '=' << bindings[index] << '\n';
        }
        output << "body=" << bodyColorIndex << '\n';
        output << "body_style=" << bodyStyleIndex << '\n';
        output << "wheels=" << wheelColorIndex << '\n';
        output << "spoiler=" << spoilerColorIndex << '\n';
        output << "decal=" << decalColorIndex << '\n';
        output << "boost_fx=" << boostColorIndex << '\n';
        output << "arena=" << (arcadeCupActive ? arcadeCupSavedArenaSelection : arenaSelection) << '\n';
        const Difficulty savedDifficulty = arcadeCupActive ? arcadeCupSavedDifficulty : difficulty;
        output << "difficulty=" << (savedDifficulty == Difficulty::Pro ? 1 : 0) << '\n';
        output << "ball_bounce=" << static_cast<int>(std::round(ballElasticity * 100.0F)) << '\n';
        output << "match_seconds=" << (arcadeCupActive ? arcadeCupSavedDuration : matchDurationSeconds) << '\n';
        output << "score_limit=" << (arcadeCupActive ? arcadeCupSavedScoreLimit : scoreLimit) << '\n';
        output << "powerups=" << (powerupsEnabled ? 1 : 0) << '\n';
        output << "challenge_best=" << challengeBestScore << '\n';
        output << "challenge_combo=" << challengeRecordCombo << '\n';
        output << "arcade_cup_best=" << arcadeCupBestRound << '\n';
        output << "arcade_cup_titles=" << arcadeCupTitles << '\n';
        output << "academy_medal=" << academyBestMedal << '\n';
        output << "academy_time_cs=" << academyBestTimeCentiseconds << '\n';
        output << "academy_completions=" << academyCompletions << '\n';
        output << "academy_ghost_enabled=" << (academyGhostEnabled ? 1 : 0) << '\n';
        output << "career_xp=" << careerXp << '\n';
        output << "career_matches=" << careerMatches << '\n';
        output << "career_wins=" << careerWins << '\n';
        output << "career_points=" << careerPoints << '\n';
        output << "career_touches=" << careerTouches << '\n';
        output << "career_aerials=" << careerAerials << '\n';
        output << "career_boost_pads=" << careerBoostPads << '\n';
        output << "career_powerups=" << careerPowerups << '\n';
        output << "career_best_rally=" << careerBestRally << '\n';
        output << "career_milestones=" << careerMilestones << '\n';
        return output ? output.str() : std::string{};
    }

    bool assignBinding(std::size_t selectedIndex, int newKey) {
        if (selectedIndex >= BindingCount || newKey < KEY_SPACE || newKey > KEY_KB_MENU) {
            settingsNotice = "THAT KEY CANNOT BE ASSIGNED";
            settingsNoticeTimer = 2.2F;
            return false;
        }
        if (newKey == KEY_F1 || newKey == KEY_F11 || newKey == KEY_ENTER) {
            settingsNotice = "F1, F11 AND ENTER ARE RESERVED";
            settingsNoticeTimer = 2.2F;
            return false;
        }

        const int oldKey = bindings[selectedIndex];
        for (std::size_t index = 0; index < BindingCount; ++index) {
            if (index != selectedIndex && bindings[index] == newKey) {
                bindings[index] = oldKey;
                settingsNotice = std::string("SWAPPED WITH ") + BindingLabels[index];
                bindings[selectedIndex] = newKey;
                saveSettings(settingsNotice);
                return true;
            }
        }
        bindings[selectedIndex] = newKey;
        saveSettings();
        return true;
    }

    void resetBindings() {
        bindings = DefaultBindings;
        saveSettings("DEFAULT CONTROLS RESTORED");
    }

    const ArenaTheme &activeArena() const {
        return ArenaThemes[static_cast<std::size_t>(activeArenaIndex)];
    }

    std::string arenaSelectionName() const {
        return arenaSelection == 0
            ? "RANDOM EACH SESSION"
            : ArenaThemes[static_cast<std::size_t>(arenaSelection - 1)].name;
    }

    void cycleArena(int direction) {
        arenaSelection = (arenaSelection + direction + static_cast<int>(ArenaThemes.size()) + 1)
            % (static_cast<int>(ArenaThemes.size()) + 1);
        if (arenaSelection > 0) {
            activeArenaIndex = arenaSelection - 1;
        }
        saveSettings("ARENA SELECTION SAVED");
        audio.play(audio.menuMove);
    }

    void chooseArenaForSession() {
        if (arenaSelection > 0) {
            activeArenaIndex = arenaSelection - 1;
            return;
        }
        if (ArenaThemes.size() > 1) {
            const int previous = activeArenaIndex;
            do {
                activeArenaIndex = GetRandomValue(0, static_cast<int>(ArenaThemes.size()) - 1);
            } while (activeArenaIndex == previous);
        }
    }

    void applyDifficultyPhysics() {
        if (ball != InvalidBody) {
            physics.setGravityFactor(ball, difficulty == Difficulty::Rookie ? 0.58F : 1.0F);
        }
    }

    void createArena() {
        courtFloor = physics.createStaticBox({0.0F, -0.5F, 0.0F}, {ArenaHalfWidth, 0.5F, ArenaHalfLength});
        physics.createStaticBox({-ArenaHalfWidth - 0.45F, 3.1F, 0.0F}, {0.45F, 3.1F, ArenaHalfLength + 0.45F});
        physics.createStaticBox({ArenaHalfWidth + 0.45F, 3.1F, 0.0F}, {0.45F, 3.1F, ArenaHalfLength + 0.45F});
        physics.createStaticBox({0.0F, 3.1F, -ArenaHalfLength - 0.45F}, {ArenaHalfWidth + 0.45F, 3.1F, 0.45F});
        physics.createStaticBox({0.0F, 3.1F, ArenaHalfLength + 0.45F}, {ArenaHalfWidth + 0.45F, 3.1F, 0.45F});
        physics.createStaticBox({0.0F, 1.28F, 0.0F}, {ArenaHalfWidth - 0.25F, 1.28F, 0.16F});
    }

    void createActors() {
        ball = physics.createDynamicSphere({0.0F, 5.0F, 0.0F}, BallRadius, BallMass, ballElasticity);
        physics.setGravityFactor(ball, difficulty == Difficulty::Rookie ? 0.58F : 1.0F);

        constexpr std::array<Color, MaximumCars> colors{
            Color{43, 199, 255, 255}, Color{80, 112, 255, 255}, Color{94, 224, 208, 255},
            Color{255, 76, 87, 255}, Color{255, 154, 54, 255}, Color{221, 92, 208, 255}};
        for (int index = 0; index < static_cast<int>(cars.size()); ++index) {
            Car &car = cars[index];
            car.body = physics.createDynamicBox({0.0F, 0.58F, 0.0F}, {0.92F, 0.45F, 1.42F}, 140.0F, 0.22F);
            physics.setGravityFactor(car.body, 0.72F);
            car.team = index < CarsPerTeam ? 0 : 1;
            car.slot = index;
            car.human = index == 0;
            car.paint = colors[index];
            car.wheelColor = Color{18, 20, 25, 255};
            car.spoilerColor = index < CarsPerTeam ? SKYBLUE : ORANGE;
            car.bodyStyle = index % static_cast<int>(BodyStyles.size());
            car.decalColor = index < CarsPerTeam
                ? DecalColors[static_cast<std::size_t>((index + 1) % 6)].color
                : DecalColors[static_cast<std::size_t>((index + 3) % 6)].color;
            car.boostColor = index < CarsPerTeam
                ? BoostColors[static_cast<std::size_t>((index + 1) % BoostColors.size())].color
                : BoostColors[static_cast<std::size_t>((index + 4) % BoostColors.size())].color;
        }
        applyPlayerCustomization();
    }

    void resetCar(Car &car, Vec3 position, float heading) {
        car.heading = heading;
        car.boost = practiceMode() ? 100.0F : 65.0F;
        car.jumpCooldown = 0.0F;
        car.airborneTime = 0.0F;
        car.landingImpact = 0.0F;
        car.aiThinkTimer = 0.0F;
        car.contactSeparation = 1.0F;
        car.recoveryTimer = 0.0F;
        car.dodgeTimer = 0.0F;
        car.groundedGrace = 0.0F;
        car.jumpBuffer = 0.0F;
        car.aiTarget = position;
        car.aiShotCommitted = false;
        car.aiYielding = false;
        car.touchRecovery = 0.0F;
        car.jumpsUsed = 0;
        car.dodgeAvailable = true;
        car.respawnTimer = 0.0F;
        car.respawnCause = RespawnCause::None;
        car.demolishedBy = -1;
        physics.setGravityFactor(car.body, 0.72F);
        physics.setTransform(car.body, position, yawRotation(heading));
        physics.setLinearVelocity(car.body, {});
        physics.setAngularVelocity(car.body, {});
    }

    int activeCarsPerTeam() const {
        return gameMode == GameMode::ThreeVsThree ? 3 : 2;
    }

    int firstCarForTeam(int team) const {
        return team * CarsPerTeam;
    }

    bool practiceMode() const {
        return gameMode == GameMode::Training || gameMode == GameMode::TargetChallenge;
    }

    bool isCarActive(int index) const {
        if (practiceMode()) {
            return index == 0;
        }
        const int team = index < CarsPerTeam ? 0 : 1;
        return index - firstCarForTeam(team) < activeCarsPerTeam();
    }

    bool isCarAvailable(int index) const {
        return isCarActive(index) && cars[static_cast<std::size_t>(index)].respawnTimer <= 0.0F;
    }

    Vec3 carRespawnPosition(const Car &car) const {
        const int rank = car.slot - firstCarForTeam(car.team);
        const int activeCount = activeCarsPerTeam();
        const float laneX = activeCount == 2
            ? (rank == 0 ? -5.2F : 5.2F)
            : static_cast<float>(rank - 1) * 6.4F;
        const float teamSide = car.team == 0 ? 1.0F : -1.0F;
        return {laneX, 0.62F, teamSide * 16.8F};
    }

    float carRespawnHeading(const Car &car) const {
        return car.team == 0 ? Pi : 0.0F;
    }

    void beginCarRespawn(Car &car, RespawnCause cause, int attackerSlot = -1) {
        if (car.respawnTimer > 0.0F || !isCarActive(car.slot)) {
            return;
        }

        const Vec3 impactPosition = physics.transform(car.body).position;
        car.respawnCause = cause;
        car.respawnTimer = cause == RespawnCause::Demolition
            ? DemolitionRespawnDelay
            : OutOfBoundsRespawnDelay;
        car.demolishedBy = attackerSlot;
        car.magnetTimer = 0.0F;
        car.heldPowerup = Powerup::None;
        car.powerupGrantTimer = PowerupInitialDelay;
        physics.setGravityFactor(car.body, 0.0F);
        physics.setTransform(
            car.body,
            {38.0F + static_cast<float>(car.slot) * 3.0F, -12.0F, 34.0F},
            yawRotation(carRespawnHeading(car)));
        physics.setLinearVelocity(car.body, {});
        physics.setAngularVelocity(car.body, {});

        if (cause == RespawnCause::Demolition) {
            emitBurst(impactPosition, Color{255, 202, 44, 255}, 58, 9.0F, 0.22F);
            emitBurst(impactPosition, Color{255, 76, 54, 255}, 36, 6.5F, 0.18F);
            audio.playHit(26.0F);
            shake = std::max(shake, 0.9F);
            if (attackerSlot >= 0) {
                ++matchDemolitions[cars[static_cast<std::size_t>(attackerSlot)].team];
            }
            if (car.human) {
                touchNotice = "DEMOLISHED  /  RESPAWN IN 3";
                touchNoticeTimer = DemolitionRespawnDelay;
            } else if (attackerSlot >= 0 && cars[static_cast<std::size_t>(attackerSlot)].human) {
                touchNotice = "DEMOLITION!  /  OPPONENT DOWN 3s";
                touchNoticeTimer = 1.8F;
            }
        } else {
            emitBurst(impactPosition, car.team == 0 ? SKYBLUE : ORANGE, 26, 5.5F, 0.15F);
            if (car.human) {
                touchNotice = "OUT OF BOUNDS  /  RECOVERING";
                touchNoticeTimer = OutOfBoundsRespawnDelay;
            }
        }
    }

    void tickCarRespawns(float deltaSeconds) {
        for (Car &car : cars) {
            if (!isCarActive(car.slot) || car.respawnTimer <= 0.0F) {
                continue;
            }
            car.respawnTimer = std::max(0.0F, car.respawnTimer - deltaSeconds);
            if (car.respawnTimer > 0.0F) {
                continue;
            }

            const RespawnCause cause = car.respawnCause;
            resetCar(car, carRespawnPosition(car), carRespawnHeading(car));
            car.boost = cause == RespawnCause::Demolition ? 33.0F : 50.0F;
            emitBurst(physics.transform(car.body).position, car.team == 0 ? SKYBLUE : ORANGE, 30, 5.0F, 0.14F);
            if (car.human) {
                touchNotice = cause == RespawnCause::Demolition ? "BACK IN PLAY  /  33 BOOST" : "RECOVERED  /  BACK IN PLAY";
                touchNoticeTimer = 1.5F;
            }
        }
    }

    void checkCarOutOfBounds() {
        for (Car &car : cars) {
            if (!isCarAvailable(car.slot)) {
                continue;
            }
            const Vec3 position = physics.transform(car.body).position;
            if (position.y < -5.0F || position.y > 34.0F
                || std::abs(position.x) > ArenaHalfWidth + 8.0F
                || std::abs(position.z) > ArenaHalfLength + 8.0F) {
                beginCarRespawn(car, RespawnCause::OutOfBounds);
            }
        }
    }

    bool canDemolish(const Car &attacker, const Car &victim) const {
        if (attacker.team == victim.team || attacker.respawnTimer > 0.0F || victim.respawnTimer > 0.0F) {
            return false;
        }
        const Vec3 attackerPosition = physics.transform(attacker.body).position;
        const Vec3 victimPosition = physics.transform(victim.body).position;
        const Vec3 toVictim = subtract(victimPosition, attackerPosition);
        const float planarDistance = length2D(toVictim);
        if (planarDistance < 0.1F || planarDistance > 3.15F || std::abs(toVictim.y) > 1.35F) {
            return false;
        }

        const Vec3 impactDirection{toVictim.x / planarDistance, 0.0F, toVictim.z / planarDistance};
        const Quaternion attackerRotation = toRay(physics.transform(attacker.body).rotation);
        const Vector3 bodyForward = Vector3RotateByQuaternion({0.0F, 0.0F, 1.0F}, attackerRotation);
        const float forwardLength = std::sqrt(bodyForward.x * bodyForward.x + bodyForward.z * bodyForward.z);
        const Vec3 forward = forwardLength > 0.1F
            ? Vec3{bodyForward.x / forwardLength, 0.0F, bodyForward.z / forwardLength}
            : forwardFromHeading(attacker.heading);
        const Vec3 attackerVelocity = physics.linearVelocity(attacker.body);
        const Vec3 victimVelocity = physics.linearVelocity(victim.body);
        const float attackerSpeed = length2D(attackerVelocity);
        const float forwardContact = forward.x * impactDirection.x + forward.z * impactDirection.z;
        const float velocityContact = attackerSpeed > 0.1F
            ? (attackerVelocity.x * impactDirection.x + attackerVelocity.z * impactDirection.z) / attackerSpeed
            : 0.0F;
        const float closingSpeed = (attackerVelocity.x - victimVelocity.x) * impactDirection.x
            + (attackerVelocity.z - victimVelocity.z) * impactDirection.z;
        return attackerSpeed >= DemolitionSpeedThreshold
            && forwardContact >= 0.70F
            && velocityContact >= 0.72F
            && closingSpeed >= 3.5F;
    }

    void checkCarDemolitions() {
        if (!competitiveMode() || state != MatchState::Playing) {
            return;
        }
        for (int first = 0; first < static_cast<int>(cars.size()); ++first) {
            if (!isCarAvailable(first)) {
                continue;
            }
            for (int second = first + 1; second < static_cast<int>(cars.size()); ++second) {
                if (!isCarAvailable(second) || cars[first].team == cars[second].team) {
                    continue;
                }
                const bool firstWins = canDemolish(cars[first], cars[second]);
                const bool secondWins = canDemolish(cars[second], cars[first]);
                if (firstWins && secondWins) {
                    const float firstSpeed = length2D(physics.linearVelocity(cars[first].body));
                    const float secondSpeed = length2D(physics.linearVelocity(cars[second].body));
                    beginCarRespawn(firstSpeed >= secondSpeed ? cars[second] : cars[first], RespawnCause::Demolition,
                        firstSpeed >= secondSpeed ? first : second);
                } else if (firstWins) {
                    beginCarRespawn(cars[second], RespawnCause::Demolition, first);
                } else if (secondWins) {
                    beginCarRespawn(cars[first], RespawnCause::Demolition, second);
                }
            }
        }
    }

    bool largeBoostPad(std::size_t index) const {
        return index < LargeBoostPadCount;
    }

    float boostPadRespawnDuration(std::size_t index) const {
        return largeBoostPad(index) ? LargeBoostPadRespawn : SmallBoostPadRespawn;
    }

    void resetBoostPads() {
        boostPadRespawnTimers.fill(0.0F);
    }

    void tickBoostPads(float deltaSeconds) {
        for (float &timer : boostPadRespawnTimers) {
            timer = std::max(0.0F, timer - deltaSeconds);
        }
    }

    void parkInactiveCars() {
        for (int index = 0; index < static_cast<int>(cars.size()); ++index) {
            if (!isCarActive(index)) {
                resetCar(cars[index], {34.0F + static_cast<float>(index) * 3.0F, -8.0F, 28.0F}, 0.0F);
                physics.setGravityFactor(cars[index].body, 0.0F);
            }
        }
    }

    void resetRound(int nextServingTeam) {
        resetBoostPads();
        inputEdges = {};
        tacticsTimer = 0.0F;
        ballFreezeTimer = 0.0F;
        frozenBallVelocity = {};
        physics.setGravityFactor(ball, difficulty == Difficulty::Rookie ? 0.58F : 1.0F);
        for (Car &car : cars) {
            car.magnetTimer = 0.0F;
        }
        servingTeam = nextServingTeam;
        receivingTeam = 1 - servingTeam;
        const int activeCount = activeCarsPerTeam();
        serveRotation[servingTeam] = (serveRotation[servingTeam] + 1 + activeCount) % activeCount;
        servingCar = firstCarForTeam(servingTeam) + serveRotation[servingTeam];
        receiveRotation[receivingTeam] = (receiveRotation[receivingTeam] + 1 + activeCount) % activeCount;
        receivingCar = firstCarForTeam(receivingTeam) + receiveRotation[receivingTeam];
        rotationStriker = {firstCarForTeam(0), firstCarForTeam(1)};
        rotationStriker[servingTeam] = servingCar;
        rotationStriker[receivingTeam] = receivingCar;
        const float servingSide = servingTeam == 0 ? 1.0F : -1.0F;
        const float receivingSide = -servingSide;
        const float targetX = static_cast<float>(GetRandomValue(-25, 25)) * 0.1F;
        const float serveX = static_cast<float>(GetRandomValue(-22, 22)) * 0.1F;
        serveLandingTarget = {targetX, BallRadius + 0.08F, receivingSide * 10.6F};
        const float supportX = targetX >= 0.0F ? -6.0F : 6.0F;

        for (int team = 0; team < 2; ++team) {
            const float side = team == 0 ? 1.0F : -1.0F;
            const float heading = team == 0 ? Pi : 0.0F;
            int supportOrdinal = 0;
            for (int rank = 0; rank < activeCount; ++rank) {
                const int index = firstCarForTeam(team) + rank;
                if (index == servingCar) {
                    resetCar(cars[index], {serveX, 0.62F, side * 14.3F}, heading);
                } else if (team == receivingTeam && index == receivingCar) {
                    resetCar(cars[index], {targetX, 0.62F, side * 9.8F}, heading);
                } else {
                    const float laneX = supportOrdinal == 0 ? supportX : -supportX * 0.78F;
                    const float depth = supportOrdinal == 0 ? 14.0F : 18.0F;
                    resetCar(cars[index], {laneX, 0.62F, side * depth}, heading);
                    ++supportOrdinal;
                }
            }
        }
        parkInactiveCars();

        servePosition = {
            serveX,
            BallRadius + 1.55F,
            servingSide * 11.0F};
        serveVelocity = {};
        physics.setTransform(ball, servePosition, {});
        physics.setLinearVelocity(ball, {});
        physics.setAngularVelocity(ball, {});
        previousBallVelocity = {};
        bestRallyTouches = std::max(bestRallyTouches, rallyTouches);
        rallyTouches = 0;
        rallyTime = 0.0F;
        replayFrames.clear();
        replayPlaybackFrame = 0.0F;
        ballTrail.clear();
        ballTrailTimer = 0.0F;
        resetTouchSequence();
        previousBallZ = servePosition.z;
        receivePending = true;
        serveInProgress = true;
        serveCountdown = 3.0F;
        countdownCue = 4;
        accumulator = 0.0F;
        updateTactics(0.0F, true);
    }

    void launchServe() {
        physics.setTransform(ball, servePosition, {});
        if (practiceMode()) {
            physics.setLinearVelocity(ball, serveVelocity);
            physics.setAngularVelocity(ball, {0.0F, 3.2F, 1.4F});
            previousBallVelocity = serveVelocity;
            serveInProgress = false;
        } else {
            const float servingSide = servingTeam == 0 ? 1.0F : -1.0F;
            const Vec3 tossVelocity{0.0F, 5.2F, -servingSide * 0.45F};
            physics.setLinearVelocity(ball, tossVelocity);
            physics.setAngularVelocity(ball, {0.0F, 1.6F, 0.0F});
            previousBallVelocity = tossVelocity;
            serveInProgress = true;
        }
        rallyTime = 0.0F;
        previousBallZ = servePosition.z;
        accumulator = 0.0F;
        state = MatchState::Playing;
        updateTactics(0.0F, true);
    }

    void startMatch(GameMode mode = GameMode::Match) {
        academyActive = false;
        gameMode = mode;
        careerMatchRecorded = false;
        careerLastXpAward = 0;
        for (Car &car : cars) {
            car.human = false;
            car.heldPowerup = Powerup::None;
            car.powerupGrantTimer = PowerupInitialDelay + static_cast<float>(car.slot % CarsPerTeam) * 1.25F;
            car.magnetTimer = 0.0F;
        }
        cars[0].human = true;
        cars[1].human = mode == GameMode::LocalCoop;
        score = {0, 0};
        matchTime = static_cast<float>(matchDurationSeconds);
        overtime = false;
        pendingGameOver = false;
        arcadeCupResultRecorded = false;
        serveRotation = {-1, -1};
        receiveRotation = {-1, -1};
        scoringTeam = 0;
        winner = 0;
        rallyTouches = 0;
        bestRallyTouches = 0;
        matchTouches = {0, 0};
        matchAerialTouches = {0, 0};
        matchBoostPickups = {0, 0};
        matchBoostSpent = {0.0F, 0.0F};
        matchPowerupsUsed = {0, 0};
        matchDemolitions = {0, 0};
        ballFreezeTimer = 0.0F;
        frozenBallVelocity = {};
        powerupSequence = 0;
        fastestBallSpeed = 0.0F;
        particles.clear();
        resetRound(1);
        state = MatchState::ServeCountdown;
        if (gameMode == GameMode::LocalCoop) {
            updateLocalCoopCameras(1.0F);
        }
    }

    void resetTrainingServe(bool repeat = false) {
        const Vec3 previousServePosition = servePosition;
        const Vec3 previousServeVelocity = serveVelocity;
        const Vec3 previousLandingTarget = serveLandingTarget;
        const TrainingFeed previousFeed = currentTrainingFeed;
        if (gameMode != GameMode::TargetChallenge) {
            gameMode = GameMode::Training;
        }
        applyDifficultyPhysics();
        ballFreezeTimer = 0.0F;
        resetBoostPads();
        servingTeam = 1;
        receivingTeam = 0;
        servingCar = -1;
        receivingCar = 0;
        resetCar(cars[0], {0.0F, 0.62F, 10.8F}, Pi);
        for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
            resetCar(cars[index], {32.0F + static_cast<float>(index) * 3.0F, -8.0F, 28.0F}, 0.0F);
            physics.setGravityFactor(cars[index].body, 0.0F);
        }

        TrainingFeed activeFeed = trainingFeed;
        if (gameMode == GameMode::TargetChallenge) {
            constexpr std::array<TrainingFeed, TargetChallengeShots> challengeFeeds{
                TrainingFeed::Lob, TrainingFeed::Fast, TrainingFeed::CrossCourt, TrainingFeed::Lob,
                TrainingFeed::Fast, TrainingFeed::CrossCourt, TrainingFeed::Fast, TrainingFeed::Lob,
                TrainingFeed::CrossCourt, TrainingFeed::Fast};
            activeFeed = challengeFeeds[static_cast<std::size_t>(trainingAttempts % TargetChallengeShots)];
            constexpr std::array<float, TargetChallengeShots> targetLanes{
                -5.2F, 0.0F, 5.4F, 3.2F, -4.0F, 0.8F, 5.8F, -5.6F, -1.8F, 3.8F};
            challengeTarget = {
                targetLanes[static_cast<std::size_t>(trainingAttempts % TargetChallengeShots)],
                0.06F,
                -8.0F - static_cast<float>(trainingAttempts % 3) * 4.0F};
        } else if (activeFeed == TrainingFeed::Mixed) {
            activeFeed = static_cast<TrainingFeed>(GetRandomValue(
                static_cast<int>(TrainingFeed::Lob),
                static_cast<int>(TrainingFeed::CrossCourt)));
        }
        currentTrainingFeed = activeFeed;
        float targetX = static_cast<float>(GetRandomValue(-32, 32)) * 0.1F;
        float serveX = static_cast<float>(GetRandomValue(-22, 22)) * 0.1F;
        float serveHeight = 7.2F;
        float flightTime = difficulty == Difficulty::Rookie ? 2.2F : 1.72F;
        if (activeFeed == TrainingFeed::Lob) {
            serveHeight = 9.0F;
            flightTime = difficulty == Difficulty::Rookie ? 2.65F : 2.25F;
        } else if (activeFeed == TrainingFeed::Fast) {
            serveHeight = 5.25F;
            flightTime = difficulty == Difficulty::Rookie ? 1.72F : 1.35F;
            targetX = static_cast<float>(GetRandomValue(-24, 24)) * 0.1F;
            serveX = targetX * 0.35F;
        } else if (activeFeed == TrainingFeed::CrossCourt) {
            const float side = GetRandomValue(0, 1) == 0 ? -1.0F : 1.0F;
            serveX = side * 4.6F;
            targetX = -side * 5.2F;
            flightTime = difficulty == Difficulty::Rookie ? 2.3F : 1.82F;
        }
        servePosition = {serveX, serveHeight, -15.0F};
        serveLandingTarget = {targetX, BallRadius + 0.08F, 8.8F};
        const float halfGravity = difficulty == Difficulty::Rookie ? 5.22F : 9.0F;
        serveVelocity = {
            (serveLandingTarget.x - servePosition.x) / flightTime,
            (serveLandingTarget.y - servePosition.y + halfGravity * flightTime * flightTime) / flightTime,
            (serveLandingTarget.z - servePosition.z) / flightTime};
        if (repeat && trainingAttempts > 0 && gameMode == GameMode::Training) {
            servePosition = previousServePosition;
            serveVelocity = previousServeVelocity;
            serveLandingTarget = previousLandingTarget;
            currentTrainingFeed = previousFeed;
        }
        physics.setTransform(ball, servePosition, {});
        physics.setLinearVelocity(ball, {});
        physics.setAngularVelocity(ball, {});
        previousBallVelocity = {};
        particles.clear();
        replayFrames.clear();
        replayPlaybackFrame = 0.0F;
        ballTrail.clear();
        ballTrailTimer = 0.0F;
        rallyTouches = 0;
        rallyTime = 0.0F;
        resetTouchSequence();
        previousBallZ = servePosition.z;
        serveInProgress = false;
        trainingBallHasTouchedGround = false;
        trainingReturnSuccessful = false;
        trainingResetTimer = 0.0F;
        ++trainingAttempts;
        inputEdges = {};
        tacticsTimer = 0.0F;
        serveCountdown = trainingAttempts > 1 ? 1.0F : 3.0F;
        countdownCue = 4;
        accumulator = 0.0F;
        state = MatchState::ServeCountdown;
    }

    void startTraining() {
        academyActive = false;
        gameMode = GameMode::Training;
        score = {0, 0};
        bestRallyTouches = 0;
        trainingAttempts = 0;
        trainingReturns = 0;
        trainingCompleted = 0;
        trainingStreak = 0;
        trainingBestStreak = 0;
        trainingRepeatShot = false;
        matchTime = 180.0F;
        overtime = false;
        pendingGameOver = false;
        resetTrainingServe();
    }

    void handleTrainingCommands(bool nextFeed, bool toggleRepeat, bool retry) {
        if (gameMode != GameMode::Training || academyActive
            || (state != MatchState::Playing && state != MatchState::ServeCountdown)) return;
        if (nextFeed) {
            trainingFeed = static_cast<TrainingFeed>((static_cast<int>(trainingFeed) + 1) % 4);
            resetTrainingServe();
            touchNotice = std::string("FEED: ") + trainingFeedName();
        } else if (toggleRepeat) {
            trainingRepeatShot = !trainingRepeatShot;
            touchNotice = trainingRepeatShot ? "SHOT LOCKED / AUTO REPEAT" : "SHOT UNLOCKED / VARIED FEEDS";
        } else if (retry) {
            resetTrainingServe(true);
            touchNotice = "SAME SHOT / TRY AGAIN";
        } else return;
        touchNoticeTimer = 1.8F;
        audio.play(audio.menuMove);
    }

    int academyLessonIndex() const {
        return static_cast<int>(academyLesson);
    }

    const char *academyLessonName() const {
        return AcademyLessonNames[static_cast<std::size_t>(academyLessonIndex())];
    }

    const char *academyLessonInstruction() const {
        switch (academyLesson) {
        case AcademyLesson::BoostGates: return "DRIVE THROUGH ALL 3 GATES";
        case AcademyLesson::DoubleJump: return "DOUBLE-JUMP + BOOST ABOVE 5.5m";
        case AcademyLesson::AerialReturn: return "MEET THE LOB AND SEND IT BACK";
        case AcademyLesson::TargetLanding: return "LAND YOUR RETURN INSIDE THE TARGET";
        }
        return "COMPLETE THE LESSON";
    }

    const char *academyMedalName(int medal) const {
        return medal >= 3 ? "GOLD" : (medal == 2 ? "SILVER" : (medal == 1 ? "BRONZE" : "UNRANKED"));
    }

    Color academyMedalColor(int medal) const {
        if (medal >= 3) return GOLD;
        if (medal == 2) return Color{198, 214, 226, 255};
        if (medal == 1) return Color{205, 126, 72, 255};
        return Color{135, 153, 173, 255};
    }

    void captureAcademyGhostFrame(bool force = false) {
        if (!academyActive || state != MatchState::Playing
            || academyCurrentGhost.size() >= AcademyGhostMaximumFrames) {
            return;
        }
        academyGhostCaptureTimer -= FixedStep;
        if (!force && academyGhostCaptureTimer > 0.0F) {
            return;
        }
        academyGhostCaptureTimer = AcademyGhostCaptureInterval;
        academyCurrentGhost.push_back({academyRunTimer, academyLesson, physics.transform(cars[0].body)});
    }

    float academyGhostLessonEndTime(AcademyLesson lesson) const {
        float endTime = -1.0F;
        for (const AcademyGhostFrame &frame : academyBestGhost) {
            if (frame.lesson == lesson) {
                endTime = std::max(endTime, frame.time);
            }
        }
        return endTime;
    }

    bool academyGhostTransform(Transform &ghostTransform) const {
        if (!academyActive || !academyGhostEnabled || academyBestGhost.empty()) {
            return false;
        }
        std::size_t first = academyBestGhost.size();
        std::size_t last = academyBestGhost.size();
        for (std::size_t index = 0; index < academyBestGhost.size(); ++index) {
            if (academyBestGhost[index].lesson != academyLesson) {
                continue;
            }
            if (first == academyBestGhost.size()) {
                first = index;
            }
            last = index;
        }
        if (first == academyBestGhost.size()) {
            return false;
        }
        if (academyRunTimer <= academyBestGhost[first].time || first == last) {
            ghostTransform = academyBestGhost[first].car;
            return true;
        }
        if (academyRunTimer >= academyBestGhost[last].time) {
            ghostTransform = academyBestGhost[last].car;
            return true;
        }
        for (std::size_t index = first; index < last; ++index) {
            const AcademyGhostFrame &from = academyBestGhost[index];
            const AcademyGhostFrame &to = academyBestGhost[index + 1];
            if (to.lesson != academyLesson || academyRunTimer > to.time) {
                continue;
            }
            const float duration = std::max(0.0001F, to.time - from.time);
            const float amount = clamp((academyRunTimer - from.time) / duration, 0.0F, 1.0F);
            ghostTransform.position = {
                from.car.position.x + (to.car.position.x - from.car.position.x) * amount,
                from.car.position.y + (to.car.position.y - from.car.position.y) * amount,
                from.car.position.z + (to.car.position.z - from.car.position.z) * amount};
            const Quaternion rotation = QuaternionSlerp(toRay(from.car.rotation), toRay(to.car.rotation), amount);
            ghostTransform.rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
            return true;
        }
        return false;
    }

    void setupAcademyLesson() {
        gameMode = GameMode::Training;
        applyDifficultyPhysics();
        resetBoostPads();
        resetTouchSequence();
        rallyTouches = 0;
        rallyTime = 0.0F;
        trainingResetTimer = 0.0F;
        trainingBallHasTouchedGround = false;
        trainingReturnSuccessful = false;
        academyTransitionTimer = 0.0F;
        academyPeakHeight = 0.0F;
        academyAerialTouch = false;
        inputEdges = {};
        tacticsTimer = 0.0F;
        academyGhostCaptureTimer = 0.0F;
        academyBoostUsed = false;
        academyLessonTimer = AcademyLessonDurations[static_cast<std::size_t>(academyLessonIndex())];
        serveInProgress = false;
        ballFreezeTimer = 0.0F;
        physics.setGravityFactor(ball, difficulty == Difficulty::Rookie ? 0.58F : 1.0F);

        resetCar(cars[0], {0.0F, 0.62F, academyLesson == AcademyLesson::BoostGates ? 19.0F : 10.8F}, Pi);
        for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
            resetCar(cars[index], {32.0F + static_cast<float>(index) * 3.0F, -8.0F, 28.0F}, 0.0F);
            physics.setGravityFactor(cars[index].body, 0.0F);
        }

        if (academyLesson == AcademyLesson::BoostGates) {
            academyGateIndex = 0;
            servePosition = {30.0F, -8.0F, 30.0F};
            serveVelocity = {};
        } else if (academyLesson == AcademyLesson::DoubleJump) {
            servePosition = {30.0F, -8.0F, 30.0F};
            serveVelocity = {};
        } else if (academyLesson == AcademyLesson::AerialReturn) {
            servePosition = {-3.8F, 8.5F, -15.8F};
            serveLandingTarget = {0.0F, BallRadius, 9.0F};
        } else {
            servePosition = {5.2F, 7.2F, -15.8F};
            serveLandingTarget = {0.8F, BallRadius, 8.8F};
            challengeTarget = {-4.0F, 0.08F, -10.5F};
        }
        if (academyLesson == AcademyLesson::AerialReturn || academyLesson == AcademyLesson::TargetLanding) {
            const float flightTime = difficulty == Difficulty::Rookie ? 2.4F : 1.85F;
            const float halfGravity = difficulty == Difficulty::Rookie ? 5.22F : 9.0F;
            serveVelocity = {(serveLandingTarget.x - servePosition.x) / flightTime,
                (serveLandingTarget.y - servePosition.y + halfGravity * flightTime * flightTime) / flightTime,
                (serveLandingTarget.z - servePosition.z) / flightTime};
        }
        physics.setTransform(ball, servePosition, {});
        physics.setLinearVelocity(ball, {});
        physics.setAngularVelocity(ball, {});
        previousBallVelocity = {};
        previousBallZ = servePosition.z;
        ballTrail.clear();
        particles.clear();
        serveCountdown = 2.5F;
        countdownCue = 4;
        accumulator = 0.0F;
        state = MatchState::ServeCountdown;
    }

    void startAcademy() {
        academyActive = true;
        cameraMode = CameraMode::Car;
        careerLastXpAward = 0;
        academyLesson = AcademyLesson::BoostGates;
        academyLessonsCompleted = 0;
        academyRetries = 0;
        academyRunMedal = 0;
        academyRunTimer = 0.0F;
        academyLastSplitDelta = 0.0F;
        academyResultDelta = 0.0F;
        academyCurrentGhost.clear();
        academyNewRecord = false;
        academyRecordEligible = true;
        score = {0, 0};
        matchTime = 0.0F;
        setupAcademyLesson();
    }

    void beginLoadingAcademy() {
        academyActive = true;
        beginLoading(GameMode::Training);
    }

    void completeAcademyRun() {
        captureAcademyGhostFrame(true);
        academyRunMedal = academyRecordEligible
            ? (academyRetries == 0 && academyRunTimer <= 75.0F
                    ? 3
                    : (academyRunTimer <= 110.0F && academyRetries <= 3 ? 2 : 1))
            : 0;
        const int timeCentiseconds = static_cast<int>(std::lround(academyRunTimer * 100.0F));
        const int previousBestTimeCentiseconds = academyBestTimeCentiseconds;
        academyResultDelta = previousBestTimeCentiseconds > 0
            ? academyRunTimer - static_cast<float>(previousBestTimeCentiseconds) / 100.0F
            : 0.0F;
        academyNewRecord = academyRecordEligible
            && (previousBestTimeCentiseconds == 0 || timeCentiseconds < previousBestTimeCentiseconds);
        if (academyNewRecord) {
            academyBestTimeCentiseconds = timeCentiseconds;
            academyBestGhost = academyCurrentGhost;
            saveAcademyGhost();
        }
        if (academyRecordEligible) {
            academyBestMedal = std::max(academyBestMedal, academyRunMedal);
            ++academyCompletions;
        }
        academyLessonsCompleted = AcademyLessonCount;
        if (academyRecordEligible) {
            commitCareerProgress(160 + academyRunMedal * 40, "ACADEMY COMPLETE");
        }
        touchNoticeTimer = 0.0F;
        state = MatchState::GameOver;
        saveSettings(academyNewRecord ? "NEW ACADEMY RECORD" : "ACADEMY COMPLETE");
        audio.play(audio.score);
        emitBurst(physics.transform(cars[0].body).position, academyMedalColor(academyRunMedal), 46, 6.2F, 0.16F);
    }

    void advanceAcademyLesson(bool skipped = false) {
        if (!academyActive) {
            return;
        }
        if (skipped) {
            ++academyRetries;
            academyRecordEligible = false;
        } else {
            ++academyLessonsCompleted;
        }
        if (academyLessonIndex() + 1 >= AcademyLessonCount) {
            completeAcademyRun();
            return;
        }
        academyLesson = static_cast<AcademyLesson>(academyLessonIndex() + 1);
        setupAcademyLesson();
    }

    void completeAcademyLesson() {
        if (academyTransitionTimer > 0.0F) {
            return;
        }
        const float bestSplit = academyGhostLessonEndTime(academyLesson);
        academyLastSplitDelta = bestSplit >= 0.0F ? academyRunTimer - bestSplit : 0.0F;
        academyTransitionTimer = 1.25F;
        physics.setLinearVelocity(ball, {});
        touchNotice = std::string(academyLessonName()) + "  /  COMPLETE";
        touchNoticeTimer = 1.25F;
        emitBurst(physics.transform(cars[0].body).position, SKYBLUE, 24, 4.8F, 0.13F);
        audio.play(audio.score);
    }

    void retryAcademyLesson(const char *reason) {
        ++academyRetries;
        touchNotice = std::string(reason) + "  /  RETRY";
        touchNoticeTimer = 1.4F;
        audio.play(audio.menuMove);
        setupAcademyLesson();
    }

    void startTargetChallenge() {
        academyActive = false;
        gameMode = GameMode::TargetChallenge;
        careerLastXpAward = 0;
        score = {0, 0};
        bestRallyTouches = 0;
        trainingAttempts = 0;
        trainingReturns = 0;
        challengeScore = 0;
        challengeCombo = 0;
        challengeBestCombo = 0;
        challengeTargetsHit = 0;
        challengeNewRecord = false;
        matchTime = 180.0F;
        overtime = false;
        pendingGameOver = false;
        resetTrainingServe();
    }

    void beginLoading(GameMode mode) {
        pendingGameMode = mode;
        chooseArenaForSession();
        loadingTimer = 0.0F;
        accumulator = 0.0F;
        showHelp = false;
        helpPausedGame = false;
        state = MatchState::Loading;
    }

    void beginLoadingMatch() {
        beginLoading(GameMode::Match);
    }

    void beginLoadingTraining() {
        academyActive = false;
        beginLoading(GameMode::Training);
    }

    void beginLoadingTargetChallenge() {
        academyActive = false;
        beginLoading(GameMode::TargetChallenge);
    }

    void beginLoadingLocalCoop() {
        beginLoading(GameMode::LocalCoop);
    }

    void beginLoadingThreeVsThree() {
        beginLoading(GameMode::ThreeVsThree);
    }

    const char *arcadeCupRoundName() const {
        return ArcadeCupRoundNames[static_cast<std::size_t>(std::clamp(
            arcadeCupRound, 0, ArcadeCupRoundCount - 1))];
    }

    void configureArcadeCupRound() {
        const std::size_t round = static_cast<std::size_t>(std::clamp(
            arcadeCupRound, 0, ArcadeCupRoundCount - 1));
        difficulty = ArcadeCupDifficulties[round];
        matchDurationSeconds = ArcadeCupDurations[round];
        scoreLimit = ArcadeCupScoreLimits[round];
        activeArenaIndex = ArcadeCupArenas[round];
        arenaSelection = activeArenaIndex + 1;
        applyDifficultyPhysics();
    }

    void beginArcadeCupRound() {
        configureArcadeCupRound();
        arcadeCupResultRecorded = false;
        beginLoading(ArcadeCupRoundModes[static_cast<std::size_t>(arcadeCupRound)]);
    }

    void startArcadeCupRun() {
        if (!arcadeCupActive) {
            arcadeCupSavedDifficulty = difficulty;
            arcadeCupSavedDuration = matchDurationSeconds;
            arcadeCupSavedScoreLimit = scoreLimit;
            arcadeCupSavedArenaSelection = arenaSelection;
            arcadeCupSavedActiveArena = activeArenaIndex;
        }
        arcadeCupActive = true;
        arcadeCupRound = 0;
        arcadeCupMatchWins = 0;
        beginArcadeCupRound();
    }

    void recordArcadeCupResult() {
        if (!arcadeCupActive || arcadeCupResultRecorded) {
            return;
        }
        arcadeCupResultRecorded = true;
        if (winner == 0) {
            arcadeCupMatchWins = arcadeCupRound + 1;
            arcadeCupBestRound = std::max(arcadeCupBestRound, arcadeCupMatchWins);
            if (arcadeCupRound == ArcadeCupRoundCount - 1) {
                ++arcadeCupTitles;
                commitCareerProgress(300, "CUP CHAMPION");
                saveSettings("ARCADE CUP CHAMPIONS");
            } else {
                saveSettings("ARCADE CUP ROUND WON");
            }
        } else {
            arcadeCupBestRound = std::max(arcadeCupBestRound, arcadeCupRound);
            saveSettings("ARCADE CUP RUN COMPLETE");
        }
    }

    void advanceOrRestartArcadeCup() {
        if (!arcadeCupActive) {
            return;
        }
        if (winner == 0 && arcadeCupRound < ArcadeCupRoundCount - 1) {
            ++arcadeCupRound;
            beginArcadeCupRound();
        } else {
            startArcadeCupRun();
        }
    }

    void leaveArcadeCup() {
        if (!arcadeCupActive) {
            return;
        }
        difficulty = arcadeCupSavedDifficulty;
        matchDurationSeconds = arcadeCupSavedDuration;
        scoreLimit = arcadeCupSavedScoreLimit;
        arenaSelection = arcadeCupSavedArenaSelection;
        activeArenaIndex = arcadeCupSavedActiveArena;
        arcadeCupActive = false;
        arcadeCupResultRecorded = false;
        applyDifficultyPhysics();
    }

    bool competitiveMode() const {
        return !practiceMode();
    }

    bool powerVolleyActive() const {
        return powerupsEnabled && competitiveMode() && !arcadeCupActive;
    }

    const char *feedName(TrainingFeed feed) const {
        switch (feed) {
        case TrainingFeed::Lob: return "LOB";
        case TrainingFeed::Fast: return "FAST";
        case TrainingFeed::CrossCourt: return "CROSS-COURT";
        default: return "MIXED";
        }
    }

    const char *trainingFeedName() const {
        return feedName(trainingFeed);
    }

    const char *currentTrainingFeedName() const {
        return feedName(currentTrainingFeed);
    }

    void resetTouchSequence() {
        receivePending = false;
        teamTouches = {0, 0};
        lastTouchTeam = -1;
        pendingThirdTouchBoostTeam = -1;
        thirdTouchBoostTimer = 0.0F;
        touchNotice.clear();
        touchNoticeTimer = 0.0F;
    }

    TouchResult registerTeamTouch(int team) {
        if (team < 0 || team > 1) {
            return TouchResult::Normal;
        }
        ++matchTouches[team];
        if (team == receivingTeam) receivePending = false;
        if (lastTouchTeam != team) {
            teamTouches = {0, 0};
            teamTouches[team] = 1;
            lastTouchTeam = team;
            pendingThirdTouchBoostTeam = -1;
        } else if (teamTouches[team] >= 3) {
            if (practiceMode()) {
                teamTouches[team] = 1;
                pendingThirdTouchBoostTeam = -1;
                touchNotice = "TRAINING TOUCH COUNT RESTARTED";
                touchNoticeTimer = 1.4F;
                return TouchResult::Normal;
            }
            touchNotice = team == 0 ? "BLUE FOUR-TOUCH FAULT" : "ORANGE FOUR-TOUCH FAULT";
            touchNoticeTimer = 2.0F;
            return TouchResult::Fault;
        } else {
            ++teamTouches[team];
        }

        if (teamTouches[team] == 3) {
            pendingThirdTouchBoostTeam = team;
            touchNotice = "THREE TOUCH POWER READY";
            touchNoticeTimer = 1.7F;
            return TouchResult::PowerReady;
        }
        touchNotice = TextFormat("%s TOUCH %d / 3", team == 0 ? "BLUE" : "ORANGE", teamTouches[team]);
        touchNoticeTimer = 1.1F;
        return TouchResult::Normal;
    }

    void applyThirdTouchBoostIfCrossed() {
        if (pendingThirdTouchBoostTeam < 0) {
            return;
        }
        const Transform ballTransform = physics.transform(ball);
        const float teamSide = pendingThirdTouchBoostTeam == 0 ? 1.0F : -1.0F;
        const bool crossedNet = previousBallZ * teamSide >= -0.1F
            && ballTransform.position.z * teamSide < -0.1F;
        if (!crossedNet) {
            return;
        }

        Vec3 velocity = physics.linearVelocity(ball);
        const float speed = length(velocity);
        if (speed > 0.1F) {
            const float maximum = difficulty == Difficulty::Rookie ? 18.0F : 28.0F;
            const float boostedSpeed = std::min(maximum, speed * 1.22F);
            const float scale = boostedSpeed / speed;
            velocity.x *= scale;
            velocity.y *= scale;
            velocity.z *= scale;
            physics.setLinearVelocity(ball, velocity);
        }
        thirdTouchBoostTimer = 1.1F;
        touchNotice = "THREE TOUCH POWER!";
        touchNoticeTimer = 1.8F;
        audio.play(audio.boost);
        emitBurst(ballTransform.position, pendingThirdTouchBoostTeam == 0 ? SKYBLUE : ORANGE, 26, 5.4F, 0.16F);
        pendingThirdTouchBoostTeam = -1;
    }

    net::PlayerInputPacket makeInputPacket(Controls controls, std::uint8_t playerSlot) {
        net::PlayerInputPacket packet;
        packet.sequence = ++localInputSequence;
        packet.clientTick = simulationTick;
        packet.playerSlot = playerSlot;
        packet.throttle = clamp(controls.throttle, -1.0F, 1.0F);
        packet.steer = clamp(controls.steer, -1.0F, 1.0F);
        if (controls.jumpPressed) packet.flags |= net::JumpPressed;
        if (controls.dodgePressed) packet.flags |= net::DodgePressed;
        if (controls.boostHeld) packet.flags |= net::BoostHeld;
        if (controls.powerupPressed) packet.flags |= net::PowerupPressed;
        return packet;
    }

    Controls controlsFromInputPacket(const net::PlayerInputPacket &packet) const {
        Controls controls;
        controls.throttle = clamp(packet.throttle, -1.0F, 1.0F);
        controls.steer = clamp(packet.steer, -1.0F, 1.0F);
        controls.jumpPressed = (packet.flags & net::JumpPressed) != 0;
        controls.dodgePressed = (packet.flags & net::DodgePressed) != 0;
        controls.boostHeld = (packet.flags & net::BoostHeld) != 0;
        controls.powerupPressed = (packet.flags & net::PowerupPressed) != 0;
        return controls;
    }

    net::BodySnapshot bodySnapshot(BodyHandle body, float heading = 0.0F) const {
        const Transform transform = physics.transform(body);
        const Vec3 velocity = physics.linearVelocity(body);
        return {{transform.position.x, transform.position.y, transform.position.z},
            {velocity.x, velocity.y, velocity.z}, heading};
    }

    net::WorldSnapshotPacket makeWorldSnapshot() {
        net::WorldSnapshotPacket snapshot;
        snapshot.sequence = ++snapshotSequence;
        snapshot.serverTick = simulationTick;
        snapshot.arenaIndex = static_cast<std::uint8_t>(activeArenaIndex);
        snapshot.score = {
            static_cast<std::uint8_t>(std::clamp(score[0], 0, 255)),
            static_cast<std::uint8_t>(std::clamp(score[1], 0, 255))};
        snapshot.state = static_cast<std::uint8_t>(state);
        snapshot.gameMode = static_cast<std::uint8_t>(gameMode);
        snapshot.matchTime = matchTime;
        snapshot.possessionTeam = static_cast<std::int8_t>(lastTouchTeam);
        snapshot.teamTouches = {
            static_cast<std::uint8_t>(teamTouches[0]),
            static_cast<std::uint8_t>(teamTouches[1])};
        snapshot.scoreLimit = static_cast<std::uint8_t>(std::clamp(scoreLimit, 1, 15));
        snapshot.powerupsEnabled = powerVolleyActive() ? 1U : 0U;
        snapshot.ballFreezeTimer = static_cast<std::uint8_t>(std::lround(
            clamp(ballFreezeTimer, 0.0F, 1.0F) * 255.0F));
        snapshot.ball = bodySnapshot(ball);
        for (std::size_t index = 0; index < cars.size(); ++index) {
            snapshot.cars[index] = bodySnapshot(cars[index].body, cars[index].heading);
            snapshot.carBoost[index] = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(std::lround(cars[index].boost)), 0, 100));
            snapshot.carPowerup[index] = static_cast<std::uint8_t>(cars[index].heldPowerup);
            snapshot.carPowerupCooldown[index] = static_cast<std::uint8_t>(std::lround(
                clamp(cars[index].powerupGrantTimer / PowerupMaximumTimer, 0.0F, 1.0F) * 255.0F));
            snapshot.carPowerupActive[index] = static_cast<std::uint8_t>(std::lround(
                clamp(cars[index].magnetTimer / 3.0F, 0.0F, 1.0F) * 255.0F));
        }
        for (std::size_t index = 0; index < boostPadRespawnTimers.size(); ++index) {
            const float normalized = clamp(boostPadRespawnTimers[index] / boostPadRespawnDuration(index), 0.0F, 1.0F);
            snapshot.boostPadCooldown[index] = static_cast<std::uint8_t>(std::lround(normalized * 255.0F));
        }
        return snapshot;
    }

    bool multiplayerProtocolSelfTest() {
        Controls source;
        source.throttle = 0.75F;
        source.steer = -0.4F;
        source.jumpPressed = true;
        source.boostHeld = true;
        source.powerupPressed = true;
        const net::PlayerInputPacket input = makeInputPacket(source, 1);
        const std::vector<std::uint8_t> inputBytes = net::encodePlayerInput(input);
        const auto decodedInput = net::decodePlayerInput(inputBytes);
        if (!decodedInput || decodedInput->sequence != input.sequence
            || decodedInput->playerSlot != 1 || decodedInput->throttle != 0.75F
            || decodedInput->steer != -0.4F || (decodedInput->flags & net::JumpPressed) == 0
            || (decodedInput->flags & net::BoostHeld) == 0
            || (decodedInput->flags & net::PowerupPressed) == 0) {
            return false;
        }

        const net::WorldSnapshotPacket snapshot = makeWorldSnapshot();
        const std::vector<std::uint8_t> snapshotBytes = net::encodeWorldSnapshot(snapshot);
        const auto decodedSnapshot = net::decodeWorldSnapshot(snapshotBytes);
        if (!decodedSnapshot || decodedSnapshot->sequence != snapshot.sequence
            || decodedSnapshot->serverTick != simulationTick
            || decodedSnapshot->arenaIndex != static_cast<std::uint8_t>(activeArenaIndex)
            || decodedSnapshot->gameMode != static_cast<std::uint8_t>(gameMode)
            || decodedSnapshot->scoreLimit != snapshot.scoreLimit
            || decodedSnapshot->carBoost != snapshot.carBoost
            || decodedSnapshot->powerupsEnabled != snapshot.powerupsEnabled
            || decodedSnapshot->carPowerup != snapshot.carPowerup
            || decodedSnapshot->carPowerupCooldown != snapshot.carPowerupCooldown
            || decodedSnapshot->carPowerupActive != snapshot.carPowerupActive
            || decodedSnapshot->ballFreezeTimer != snapshot.ballFreezeTimer
            || decodedSnapshot->boostPadCooldown != snapshot.boostPadCooldown
            || decodedSnapshot->ball.position != snapshot.ball.position) {
            return false;
        }

        std::vector<std::uint8_t> corrupt = inputBytes;
        corrupt[0] ^= 0xFFU;
        return !net::decodePlayerInput(corrupt).has_value();
    }

    Controls keyboardControls() const {
        Controls controls;
        controls.throttle = static_cast<float>(IsKeyDown(boundKey(BindAction::Forward)))
            - static_cast<float>(IsKeyDown(boundKey(BindAction::Reverse)));
        controls.steer = static_cast<float>(IsKeyDown(boundKey(BindAction::SteerLeft)))
            - static_cast<float>(IsKeyDown(boundKey(BindAction::SteerRight)));
        controls.jumpPressed = IsKeyPressed(boundKey(BindAction::Jump));
        controls.dodgePressed = IsKeyPressed(boundKey(BindAction::Dodge));
        controls.boostHeld = IsKeyDown(boundKey(BindAction::Boost));
        controls.powerupPressed = IsKeyPressed(boundKey(BindAction::Powerup));
        return controls;
    }

    Controls gamepadControls() const {
        Controls controls;
        if (!IsGamepadAvailable(0)) {
            return controls;
        }
        const float stickX = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
        const float stickY = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
        controls.steer = -analogAxis(stickX);
        controls.throttle = -analogAxis(stickY);
        controls.jumpPressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        controls.dodgePressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
        controls.boostHeld = IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)
            || GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.25F;
        controls.powerupPressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1);
        return controls;
    }

    Controls playerControls() const {
        Controls controls = keyboardControls();
        const Controls gamepad = gamepadControls();
        if (std::abs(gamepad.steer) > 0.0F) controls.steer = gamepad.steer;
        if (std::abs(gamepad.throttle) > 0.0F) controls.throttle = gamepad.throttle;
        controls.jumpPressed = controls.jumpPressed || gamepad.jumpPressed;
        controls.dodgePressed = controls.dodgePressed || gamepad.dodgePressed;
        controls.boostHeld = controls.boostHeld || gamepad.boostHeld;
        controls.powerupPressed = controls.powerupPressed || gamepad.powerupPressed;
        return controls;
    }

    int cosmeticRandom(int minimum, int maximum) {
        return std::uniform_int_distribution<int>(minimum, maximum)(cosmeticGenerator);
    }

    void emitBurst(Vec3 position, Color color, int count, float speed, float size = 0.12F) {
        for (int index = 0; index < count; ++index) {
            const float angle = static_cast<float>(cosmeticRandom(0, 628)) * 0.01F;
            const float magnitude = speed * static_cast<float>(cosmeticRandom(45, 100)) * 0.01F;
            Particle particle;
            particle.position = position;
            particle.velocity = {
                std::cos(angle) * magnitude,
                static_cast<float>(cosmeticRandom(20, 100)) * 0.01F * magnitude,
                std::sin(angle) * magnitude};
            particle.life = static_cast<float>(cosmeticRandom(35, 85)) * 0.01F;
            particle.initialLife = particle.life;
            particle.color = color;
            particle.size = size * static_cast<float>(cosmeticRandom(70, 140)) * 0.01F;
            particles.push_back(particle);
        }
    }

    void recordBallTrail() {
        ballTrailTimer -= FixedStep;
        const Vec3 velocity = physics.linearVelocity(ball);
        const float speed = length(velocity);
        if (ballTrailTimer > 0.0F || speed < 6.0F) {
            return;
        }
        ballTrailTimer = clamp(0.07F - speed * 0.0018F, 0.022F, 0.055F);
        TrailPoint point;
        point.position = physics.transform(ball).position;
        point.life = clamp(0.28F + speed * 0.012F, 0.32F, 0.58F);
        point.initialLife = point.life;
        point.size = clamp(0.07F + speed * 0.007F, 0.11F, 0.27F);
        point.color = lastTouchTeam == 0 ? SKYBLUE : (lastTouchTeam == 1 ? ORANGE : GOLD);
        ballTrail.push_back(point);
        if (ballTrail.size() > 42) {
            ballTrail.erase(ballTrail.begin());
        }
    }

    const char *powerupName(Powerup powerup) const {
        switch (powerup) {
        case Powerup::Haymaker: return "HAYMAKER";
        case Powerup::Freezer: return "FREEZE";
        case Powerup::Magnetizer: return "MAGNET";
        default: return "CHARGING";
        }
    }

    Color powerupColor(Powerup powerup) const {
        switch (powerup) {
        case Powerup::Haymaker: return ORANGE;
        case Powerup::Freezer: return Color{91, 218, 255, 255};
        case Powerup::Magnetizer: return Color{215, 95, 255, 255};
        default: return Color{116, 138, 160, 255};
        }
    }

    void grantPowerup(Car &car) {
        const int selection = (powerupSequence++ + car.slot * 2) % 3;
        car.heldPowerup = static_cast<Powerup>(selection + 1);
        car.powerupGrantTimer = 0.0F;
        if (car.human && !automatedPlayer) {
            touchNotice = std::string("POWER READY  /  ") + powerupName(car.heldPowerup);
            touchNoticeTimer = 1.8F;
            audio.play(audio.menuMove);
        }
    }

    bool usePowerup(Car &car) {
        if (!powerVolleyActive() || state != MatchState::Playing || car.heldPowerup == Powerup::None) {
            return false;
        }
        const Transform carTransform = physics.transform(car.body);
        const Transform ballTransform = physics.transform(ball);
        const Vec3 offset = subtract(ballTransform.position, carTransform.position);
        const float distance = length(offset);
        const Powerup used = car.heldPowerup;
        bool activated = false;

        if (used == Powerup::Haymaker && distance <= 18.0F) {
            const float inverseDistance = 1.0F / std::max(distance, 0.01F);
            const Vec3 direction{
                offset.x * inverseDistance,
                std::max(0.18F, offset.y * inverseDistance),
                offset.z * inverseDistance};
            const float horizontalLength = std::max(0.01F, std::sqrt(
                direction.x * direction.x + direction.z * direction.z));
            const Vec3 launchVelocity{
                direction.x / horizontalLength * 25.5F,
                5.8F + std::max(0.0F, direction.y) * 5.0F,
                direction.z / horizontalLength * 25.5F};
            physics.setLinearVelocity(ball, launchVelocity);
            physics.setAngularVelocity(ball, {direction.z * 7.0F, 3.0F, -direction.x * 7.0F});
            lastTouchTeam = car.team;
            activated = true;
            shake = std::max(shake, 0.58F);
        } else if (used == Powerup::Freezer && distance <= 22.0F && ballFreezeTimer <= 0.0F) {
            frozenBallVelocity = physics.linearVelocity(ball);
            ballFreezeTimer = 1.0F;
            physics.setGravityFactor(ball, 0.0F);
            physics.setLinearVelocity(ball, {});
            physics.setAngularVelocity(ball, {});
            previousBallVelocity = {};
            activated = true;
        } else if (used == Powerup::Magnetizer) {
            car.magnetTimer = 3.0F;
            activated = true;
        }

        if (!activated) {
            if (car.human && !automatedPlayer) {
                touchNotice = used == Powerup::Freezer ? "FREEZE BLOCKED" : "BALL OUT OF RANGE";
                touchNoticeTimer = 1.2F;
            }
            return false;
        }

        car.heldPowerup = Powerup::None;
        car.powerupGrantTimer = PowerupRecharge;
        ++matchPowerupsUsed[car.team];
        const Color color = powerupColor(used);
        emitBurst(ballTransform.position, color, used == Powerup::Haymaker ? 36 : 24,
            used == Powerup::Haymaker ? 7.2F : 4.4F, 0.16F);
        touchNotice = TextFormat("%s %s!", car.team == 0 ? "BLUE" : "ORANGE", powerupName(used));
        touchNoticeTimer = 1.65F;
        audio.play(used == Powerup::Haymaker ? audio.hit : audio.boost);
        return true;
    }

    void tickPowerupSystem(float deltaSeconds) {
        if (!powerVolleyActive()) {
            return;
        }

        if (ballFreezeTimer > 0.0F) {
            ballFreezeTimer = std::max(0.0F, ballFreezeTimer - deltaSeconds);
            physics.setLinearVelocity(ball, {});
            physics.setAngularVelocity(ball, {});
            if (ballFreezeTimer <= 0.0F) {
                physics.setGravityFactor(ball, difficulty == Difficulty::Rookie ? 0.58F : 1.0F);
                const Vec3 releaseVelocity{
                    frozenBallVelocity.x * 0.72F,
                    frozenBallVelocity.y * 0.55F,
                    frozenBallVelocity.z * 0.72F};
                physics.setLinearVelocity(ball, releaseVelocity);
                previousBallVelocity = releaseVelocity;
                emitBurst(physics.transform(ball).position, powerupColor(Powerup::Freezer), 20, 4.2F, 0.13F);
            }
        }

        for (Car &car : cars) {
            if (!isCarAvailable(car.slot)) {
                continue;
            }
            if (car.heldPowerup == Powerup::None) {
                car.powerupGrantTimer = std::max(0.0F, car.powerupGrantTimer - deltaSeconds);
                if (car.powerupGrantTimer <= 0.0F) {
                    grantPowerup(car);
                }
            }
            if (car.magnetTimer <= 0.0F) {
                continue;
            }
            car.magnetTimer = std::max(0.0F, car.magnetTimer - deltaSeconds);
            if (ballFreezeTimer > 0.0F) {
                continue;
            }
            const Transform carTransform = physics.transform(car.body);
            const Vec3 forward = forwardFromHeading(car.heading);
            const Vec3 target{
                carTransform.position.x + forward.x * 2.4F,
                carTransform.position.y + 1.15F,
                carTransform.position.z + forward.z * 2.4F};
            const Vec3 toTarget = subtract(target, physics.transform(ball).position);
            const float distance = length(toTarget);
            if (distance > 0.2F && distance < 14.0F) {
                Vec3 velocity = physics.linearVelocity(ball);
                const float acceleration = 20.0F * (1.0F - distance / 18.0F);
                velocity.x += toTarget.x / distance * acceleration * deltaSeconds;
                velocity.y += toTarget.y / distance * acceleration * deltaSeconds;
                velocity.z += toTarget.z / distance * acceleration * deltaSeconds;
                const float speed = length(velocity);
                if (speed > 26.0F) {
                    const float scale = 26.0F / speed;
                    velocity.x *= scale;
                    velocity.y *= scale;
                    velocity.z *= scale;
                }
                physics.setLinearVelocity(ball, velocity);
            }
        }
    }

    void driveCar(Car &car, Controls controls, float deltaSeconds) {
        if (car.respawnTimer > 0.0F) {
            return;
        }
        if (controls.powerupPressed) {
            usePowerup(car);
        }
        Transform transform = physics.transform(car.body);
        Vec3 velocity = physics.linearVelocity(car.body);
        const Quaternion initialRotation = toRay(transform.rotation);
        const Vector3 bodyUp = Vector3RotateByQuaternion({0.0F, 1.0F, 0.0F}, initialRotation);
        const Vector3 bodyForward = Vector3RotateByQuaternion({0.0F, 0.0F, 1.0F}, initialRotation);
        const bool grounded = transform.position.y < 0.72F && std::abs(velocity.y) < 2.2F && bodyUp.y > 0.45F;
        car.dodgeTimer = std::max(0.0F, car.dodgeTimer - deltaSeconds);
        // Physical yaw stays authoritative in the air, including after a dodge or collision.
        if (!grounded && bodyForward.x * bodyForward.x + bodyForward.z * bodyForward.z > 0.04F)
            car.heading = std::atan2(bodyForward.x, bodyForward.z);
        const bool overturned = !grounded && transform.position.y < 1.65F
            && std::abs(velocity.y) < 1.5F && bodyUp.y < 0.45F;
        car.recoveryTimer = overturned ? car.recoveryTimer + deltaSeconds : 0.0F;
        if (overturned && (controls.jumpPressed || car.recoveryTimer > 0.65F)) {
            physics.setTransform(car.body, {transform.position.x, 0.75F, transform.position.z}, yawRotation(car.heading));
            physics.setAngularVelocity(car.body, {});
            physics.setLinearVelocity(car.body, {velocity.x * 0.8F, 2.0F, velocity.z * 0.8F});
            car.recoveryTimer = 0.0F;
            car.jumpsUsed = 0;
            car.jumpBuffer = 0.0F;
            return;
        }
        car.jumpCooldown = std::max(0.0F, car.jumpCooldown - deltaSeconds);
        car.touchRecovery = std::max(0.0F, car.touchRecovery - deltaSeconds);
        car.jumpBuffer = controls.jumpPressed ? 0.12F : std::max(0.0F, car.jumpBuffer - deltaSeconds);

        if (grounded) {
            if (car.airborneTime > 0.18F && car.landingImpact > 4.0F) {
                emitBurst({transform.position.x, 0.12F, transform.position.z}, LIGHTGRAY,
                    car.landingImpact > 9.0F ? 10 : 6, clamp(car.landingImpact * 0.2F, 1.0F, 2.5F), 0.08F);
                if (car.human) audio.play(audio.landing);
            }
            car.landingImpact = 0.0F;
            car.groundedGrace = 0.09F;
            car.airborneTime = 0.0F;
            car.jumpsUsed = 0;
            car.dodgeAvailable = true;
        } else {
            car.landingImpact = std::max(car.landingImpact, -velocity.y);
            car.groundedGrace = std::max(0.0F, car.groundedGrace - deltaSeconds);
            car.airborneTime += deltaSeconds;
        }

        const float planarSpeed = length2D(velocity);
        const float turnFactor = clamp(0.36F + planarSpeed / 14.0F, 0.36F, 1.0F);
        const float turnRate = 2.35F;
        const Vec3 oldForward = forwardFromHeading(car.heading);
        const float forwardSpeed = velocity.x * oldForward.x + velocity.z * oldForward.z;
        const float steeringDirection = forwardSpeed < -0.5F || (std::abs(forwardSpeed) < 0.5F && controls.throttle < -0.1F) ? -1.0F : 1.0F;
        if (grounded)
            car.heading = wrapAngle(car.heading + controls.steer * steeringDirection * turnRate * turnFactor * deltaSeconds);
        const Vec3 forward = forwardFromHeading(car.heading);

        const Quaternion bodyRotation = toRay(transform.rotation);
        const Vector3 rotatedForward = Vector3RotateByQuaternion({0.0F, 0.0F, 1.0F}, bodyRotation);
        const Vector3 rotatedRight = Vector3RotateByQuaternion({1.0F, 0.0F, 0.0F}, bodyRotation);
        const Vector3 rotatedUp = Vector3RotateByQuaternion({0.0F, 1.0F, 0.0F}, bodyRotation);
        const Vec3 aerialForward{rotatedForward.x, rotatedForward.y, rotatedForward.z};

        if (!grounded && car.dodgeTimer <= 0.0F) {
            Vec3 angularVelocity = physics.angularVelocity(car.body);
            const Vec3 desiredAngular{
                rotatedRight.x * controls.throttle * 4.7F + rotatedUp.x * controls.steer * 2.7F,
                rotatedRight.y * controls.throttle * 4.7F + rotatedUp.y * controls.steer * 2.7F,
                rotatedRight.z * controls.throttle * 4.7F + rotatedUp.z * controls.steer * 2.7F};
            const float aerialResponse = clamp(7.5F * deltaSeconds, 0.0F, 1.0F);
            angularVelocity.x += (desiredAngular.x - angularVelocity.x) * aerialResponse;
            angularVelocity.y += (desiredAngular.y - angularVelocity.y) * aerialResponse;
            angularVelocity.z += (desiredAngular.z - angularVelocity.z) * aerialResponse;
            physics.setAngularVelocity(car.body, angularVelocity);
        }

        const bool boosting = controls.boostHeld && car.boost > 0.0F && (!grounded || controls.throttle > -0.1F);
        const float targetSpeed = boosting ? 21.5F : 13.5F;
        const float driveThrottle = boosting && grounded ? std::max(0.8F, controls.throttle) : controls.throttle;
        const float desiredX = grounded ? forward.x * driveThrottle * targetSpeed : velocity.x;
        const float desiredZ = grounded ? forward.z * driveThrottle * targetSpeed : velocity.z;
        const float traction = grounded ? 6.5F : 0.18F;
        velocity.x += (desiredX - velocity.x) * clamp(traction * deltaSeconds, 0.0F, 1.0F);
        velocity.z += (desiredZ - velocity.z) * clamp(traction * deltaSeconds, 0.0F, 1.0F);

        if (std::abs(controls.throttle) < 0.05F && grounded && !boosting) {
            const float coast = std::max(0.0F, 1.0F - 2.2F * deltaSeconds);
            velocity.x *= coast;
            velocity.z *= coast;
        }

        if (boosting) {
            if (academyActive) {
                academyBoostUsed = true;
            }
            const Vec3 boostDirection = grounded ? forward : aerialForward;
            const float boostAcceleration = grounded ? 15.5F : 22.5F;
            velocity.x += boostDirection.x * boostAcceleration * deltaSeconds;
            velocity.y += boostDirection.y * boostAcceleration * deltaSeconds;
            velocity.z += boostDirection.z * boostAcceleration * deltaSeconds;
            car.boost = std::max(0.0F, car.boost - 28.0F * deltaSeconds);
            matchBoostSpent[car.team] += 28.0F * deltaSeconds;
            if (car.human && boostSoundCooldown <= 0.0F) {
                audio.play(audio.boost);
                boostSoundCooldown = 0.24F;
            }
            if (cosmeticRandom(0, 7) == 0) {
                emitBurst(
                    {transform.position.x - boostDirection.x * 1.5F,
                        transform.position.y - boostDirection.y * 1.5F,
                        transform.position.z - boostDirection.z * 1.5F},
                    car.boostColor,
                    1,
                    2.0F,
                    0.09F);
            }
        } else if (practiceMode() || difficulty == Difficulty::Rookie) {
            const float assistedRecharge = practiceMode() ? 9.0F : 4.0F;
            car.boost = std::min(100.0F, car.boost + assistedRecharge * deltaSeconds);
        }

        if (grounded && car.boost < 99.5F) {
            for (std::size_t index = 0; index < BoostPadPositions.size(); ++index) {
                if (boostPadRespawnTimers[index] > 0.0F) {
                    continue;
                }
                const Vec3 pad = BoostPadPositions[index];
                const float pickupRadius = largeBoostPad(index) ? 1.35F : 1.05F;
                if (length2D(subtract(transform.position, pad)) < pickupRadius) {
                    const float previousBoost = car.boost;
                    car.boost = largeBoostPad(index)
                        ? 100.0F
                        : std::min(100.0F, car.boost + SmallBoostPickup);
                    boostPadRespawnTimers[index] = boostPadRespawnDuration(index);
                    if (competitiveMode()) {
                        ++matchBoostPickups[car.team];
                    }
                    emitBurst({pad.x, 0.12F, pad.z}, largeBoostPad(index) ? GOLD
                        : (car.team == 0 ? SKYBLUE : ORANGE), largeBoostPad(index) ? 22 : 12, 3.4F, 0.11F);
                    if (car.human) {
                        audio.play(audio.boost);
                        const int collected = static_cast<int>(std::round(car.boost - previousBoost));
                        touchNotice = largeBoostPad(index)
                            ? "FULL BOOST  /  LARGE PAD"
                            : TextFormat("BOOST +%d  /  SMALL PAD", collected);
                        touchNoticeTimer = 1.15F;
                    }
                    break;
                }
            }
        }

        const float speed = length(velocity);
        if (speed > 26.0F) {
            const float scale = 26.0F / speed;
            velocity.x *= scale;
            velocity.y *= scale;
            velocity.z *= scale;
        }
        physics.setLinearVelocity(car.body, velocity);

        if (car.jumpBuffer > 0.0F && car.jumpCooldown <= 0.0F) {
            if ((grounded || car.groundedGrace > 0.0F) && car.jumpsUsed == 0) {
                physics.addImpulse(car.body, {0.0F, 900.0F, 0.0F});
                car.jumpsUsed = 1;
                car.jumpCooldown = 0.16F;
                car.jumpBuffer = 0.0F;
                car.groundedGrace = 0.0F;
                emitBurst({transform.position.x, 0.1F, transform.position.z}, LIGHTGRAY, 9, 2.2F, 0.13F);
                if (car.human) {
                    audio.play(audio.jump);
                }
            } else if (car.jumpsUsed == 1 && car.airborneTime < 1.35F) {
                physics.addImpulse(car.body, {0.0F, 720.0F, 0.0F});
                car.jumpsUsed = 2;
                car.dodgeAvailable = false;
                car.jumpCooldown = 0.18F;
                car.jumpBuffer = 0.0F;
                if (car.human) {
                    audio.play(audio.jump);
                }
            }
        }

        if (controls.dodgePressed && !grounded && car.dodgeAvailable && car.airborneTime < 1.35F) {
            const Vec3 dodgeDirection = directionalDodge(car.heading, controls.throttle, controls.steer);
            physics.addImpulse(car.body, {dodgeDirection.x * 570.0F, 145.0F, dodgeDirection.z * 570.0F});
            physics.setAngularVelocity(car.body, {dodgeDirection.z * 9.2F, 0.0F, -dodgeDirection.x * 9.2F});
            car.jumpsUsed = 2;
            car.dodgeAvailable = false;
            car.jumpCooldown = 0.25F;
            car.dodgeTimer = 0.22F;
            if (car.human) {
                audio.play(audio.jump);
            }
        }

        if (grounded && car.jumpCooldown <= 0.0F) {
            transform.position.y = std::max(transform.position.y, 0.5F);
            physics.setTransform(car.body, transform.position, yawRotation(car.heading));
            physics.setAngularVelocity(car.body, {});
        }
    }

    void updateTactics(float elapsed, bool force = false) {
        tacticsTimer -= elapsed;
        if (!force && tacticsTimer > 0.0F) return;
        tacticsTimer = difficulty == Difficulty::Pro ? 0.05F : 0.12F;
        predictedFlight = predictBallFlight({physics.transform(ball).position, physics.linearVelocity(ball)},
            ballFreezeTimer > 0.0F ? 0.0F : (difficulty == Difficulty::Rookie ? 10.44F : 18.0F), ballElasticity,
            {ArenaHalfWidth, ArenaHalfLength, 0.0F, 2.56F, 0.16F, BallRadius, 6.2F,
                difficulty == Difficulty::Rookie ? (thirdTouchBoostTimer > 0.0F ? 18.0F : 15.0F) : 0.0F});
        std::array<TeamCar, 6> teamCars{};
        for (std::size_t i = 0; i < cars.size(); ++i) {
            const auto transform = physics.transform(cars[i].body);
            const auto up = Vector3RotateByQuaternion({0.0F, 1.0F, 0.0F}, toRay(transform.rotation));
            teamCars[i] = {transform.position, physics.linearVelocity(cars[i].body),
                cars[i].heading, cars[i].boost, isCarAvailable(static_cast<int>(i)), cars[i].human && !automatedPlayer,
                up.y < 0.45F && transform.position.y < 1.65F, cars[i].touchRecovery};
        }
        for (int team = 0; team < 2; ++team) {
            teamPlans[team] = planTeam(teamCars, team, predictedFlight, rotationStriker[team], difficulty == Difficulty::Pro,
                receivePending && team == receivingTeam ? receivingCar : -1);
            if (serveInProgress) {
                const int assigned = team == servingTeam ? servingCar : receivingCar;
                if (assigned >= 0 && isCarAvailable(assigned)) teamPlans[team].striker = assigned;
                if (teamPlans[team].support == assigned) {
                    teamPlans[team].support = -1;
                    for (int rank = 0; rank < activeCarsPerTeam(); ++rank) {
                        const int candidate = firstCarForTeam(team) + rank;
                        if (candidate != assigned && isCarAvailable(candidate)) { teamPlans[team].support = candidate; break; }
                    }
                }
            }
            rotationStriker[team] = teamPlans[team].striker;
        }
    }

    float ballLandingTime() const { return predictedFlight.time; }

    int strikerForTeam(int team, Vec3) const { return teamPlans[team].striker; }

    int supportForTeam(int team, int, Vec3) const { return teamPlans[team].support; }

    int nearestAvailableBoostPad(Vec3 position, int team) const {
        const float teamDirection = team == 0 ? 1.0F : -1.0F;
        int bestIndex = -1;
        float bestCost = 1000.0F;
        for (std::size_t index = 0; index < BoostPadPositions.size(); ++index) {
            if (boostPadRespawnTimers[index] > 0.0F) {
                continue;
            }
            const Vec3 pad = BoostPadPositions[index];
            const bool ownHalf = pad.z * teamDirection > 0.0F;
            if (!ownHalf) continue;
            const float largePadBonus = largeBoostPad(index) ? 1.4F : 0.0F;
            const float cost = length2D(subtract(pad, position)) - largePadBonus;
            if (cost < bestCost) {
                bestCost = cost;
                bestIndex = static_cast<int>(index);
            }
        }
        return bestIndex;
    }

    Controls aiControls(Car &car, float deltaSeconds) {
        const Transform carTransform = physics.transform(car.body);
        const Transform ballTransform = physics.transform(ball);
        const Vec3 ballVelocity = physics.linearVelocity(ball);
        const Vec3 landingTarget = predictedFlight.landing();
        const Vec3 contactTarget = teamPlans[car.team].intercept;
        const float teamDirection = car.team == 0 ? 1.0F : -1.0F;
        const bool ballThreatensTeam = landingTarget.z * teamDirection > 0.35F;
        const int strikerSlot = strikerForTeam(car.team, landingTarget);
        const bool striker = strikerSlot == car.slot;
        if (!striker || !ballThreatensTeam) car.aiShotCommitted = false;
        const bool support = supportForTeam(car.team, strikerSlot, landingTarget) == car.slot;
        const int recoveryPad = !striker && !ballThreatensTeam && car.boost < 30.0F
            ? nearestAvailableBoostPad(carTransform.position, car.team)
            : -1;
        car.aiThinkTimer -= deltaSeconds;

        if (car.aiThinkTimer <= 0.0F) {
            const bool pro = difficulty == Difficulty::Pro;
            car.aiThinkTimer = pro ? 0.055F : 0.13F;
            Vec3 target{};

            if (recoveryPad >= 0) {
                target = BoostPadPositions[static_cast<std::size_t>(recoveryPad)];
            } else if (serveInProgress && car.slot == servingCar) {
                target = ballTransform.position;
                target.z += teamDirection * 0.45F;
            } else if (ballThreatensTeam && striker) {
                target = contactTarget;
                if (target.z * teamDirection < 0.8F) {
                    target = landingTarget;
                }
                // Get behind the predicted contact before driving through it toward the net.
                // Steering determines the shot: no aimed velocity replacement after impact.
                const bool behindBall = (carTransform.position.z - target.z) * teamDirection > 1.3F;
                const bool aligned = std::abs(carTransform.position.x - target.x) < 2.2F;
                if (behindBall && aligned) car.aiShotCommitted = true;
                if ((carTransform.position.z - target.z) * teamDirection < -1.2F) car.aiShotCommitted = false;
                target.z += teamDirection * (car.aiShotCommitted ? -1.0F : 2.0F);
            } else {
                const float laneSign = landingTarget.x >= 0.0F ? -1.0F : 1.0F;
                if (support) {
                    const Vec3 owner = strikerSlot >= 0 ? physics.transform(cars[strikerSlot].body).position : contactTarget;
                    // Cover beside and behind the receiver; don't cut across their approach lane.
                    const float lane = carTransform.position.x < owner.x ? -1.0F : 1.0F;
                    target.x = clamp(owner.x + lane * 5.0F, -8.0F, 8.0F);
                    target.z = teamDirection * (ballThreatensTeam
                            ? clamp(std::max(std::abs(landingTarget.z), owner.z * teamDirection) + 4.0F, 11.5F, 19.0F)
                            : 11.8F);
                } else {
                    target.x = laneSign * 6.5F;
                    target.z = teamDirection * 17.2F;
                }
            }

            // Give every teammate room, including P2 and the opposing team. The striker owns the lane.
            if (!striker) {
                for (const Car &teammate : cars) {
                    if (teammate.slot == car.slot || teammate.team != car.team || !isCarAvailable(teammate.slot)) continue;
                    const Vec3 separation = subtract(carTransform.position, physics.transform(teammate.body).position);
                    const float distance = length2D(separation);
                    if (distance < 3.8F) {
                        const float direction = distance > 0.1F ? separation.x / distance : (car.slot < teammate.slot ? -1.0F : 1.0F);
                        target.x += direction * (3.8F - distance) * 1.8F;
                    }
                }
            }

            target.x = clamp(target.x, -10.2F, 10.2F);
            target.z = car.team == 0
                ? clamp(target.z, 1.5F, 20.1F)
                : clamp(target.z, -20.1F, -1.5F);
            car.aiTarget = target;
        }

        std::array<TeamCar, 6> nearby{};
        for (const auto &other : cars)
            nearby[other.slot] = {physics.transform(other.body).position, physics.linearVelocity(other.body),
                other.heading, other.boost, isCarAvailable(other.slot), other.human};
        const TeamAvoidance avoidance = avoidTeammates(nearby, car.slot, car.aiTarget);
        car.aiYielding = avoidance.yielding;
        Vec3 safeTarget = avoidance.target;
        safeTarget.x = clamp(safeTarget.x, -11.0F, 11.0F);
        safeTarget.z = teamDirection * clamp(safeTarget.z * teamDirection, 1.5F, 20.1F);
        const Vec3 toTarget = subtract(safeTarget, carTransform.position);
        const float desiredHeading = std::atan2(toTarget.x, toTarget.z);
        const float difference = wrapAngle(desiredHeading - car.heading);
        const float distance = length2D(toTarget);
        Controls controls;
        controls.steer = clamp(difference * 1.9F, -1.0F, 1.0F);
        if (distance < 0.75F) {
            controls.throttle = 0.0F;
            controls.steer = clamp(wrapAngle(std::atan2(ballTransform.position.x - carTransform.position.x,
                ballTransform.position.z - carTransform.position.z) - car.heading) * 1.5F, -1.0F, 1.0F);
        } else if (std::abs(difference) > 1.75F) {
            // While reversing, aim the rear axle at the target. Turning the nose toward
            // it instead sends a retreating defender away from the interception lane.
            const float reverseError = wrapAngle(desiredHeading - car.heading - Pi);
            controls.throttle = -clamp(distance / 4.5F, 0.15F, 0.9F);
            controls.steer = clamp(-reverseError * 1.9F, -1.0F, 1.0F);
        } else {
            const float approachSpeed = length2D(physics.linearVelocity(car.body));
            controls.throttle = clamp(distance / (3.0F + approachSpeed * 0.22F), 0.15F, 1.0F);
        }
        controls.boostHeld = difficulty == Difficulty::Pro
            && striker
            && distance > 3.8F
            && std::abs(difference) < 0.62F;

        const Vec3 toBall = subtract(ballTransform.position, carTransform.position);
        const float horizontalBallDistance = length2D(toBall);
        const float jumpRange = difficulty == Difficulty::Pro ? 3.25F : 2.75F;
        controls.jumpPressed = striker
            && (ballThreatensTeam || (serveInProgress && car.slot == servingCar))
            && horizontalBallDistance < jumpRange
            && ballTransform.position.y > 1.15F
            && ballTransform.position.y < (difficulty == Difficulty::Pro ? 4.9F : 3.9F)
            && ballVelocity.y < 5.0F
            && toBall.z * teamDirection < 0.5F
            && car.jumpCooldown <= 0.0F;
        controls.dodgePressed = difficulty == Difficulty::Pro
            && !serveInProgress
            && striker
            && car.jumpsUsed == 1
            && std::abs(difference) < 0.6F
            && car.airborneTime > 0.2F
            && car.airborneTime < 1.1F
            && horizontalBallDistance < 2.2F
            && ballTransform.position.y > 1.75F
            && ballTransform.position.y < carTransform.position.y + 1.8F;
        if (carTransform.position.y > 0.85F) {
            const Vector3 nose = Vector3RotateByQuaternion({0.0F, 0.0F, 1.0F}, toRay(carTransform.rotation));
            const float desiredPitch = striker && ballTransform.position.y > carTransform.position.y + 1.0F ? 0.45F : 0.0F;
            controls.throttle = clamp((nose.y - desiredPitch) * 2.2F, -1.0F, 1.0F);
            controls.boostHeld = difficulty == Difficulty::Pro && striker && nose.y > 0.15F
                && horizontalBallDistance < 6.0F && ballTransform.position.y > carTransform.position.y;
            // Choose dodge direction using the target, not the pitch stabilization input.
            if (controls.dodgePressed) controls.throttle = 1.0F;
        }
        if (powerVolleyActive() && car.heldPowerup != Powerup::None) {
            const float ballSpeed = length(ballVelocity);
            if (car.heldPowerup == Powerup::Haymaker) {
                controls.powerupPressed = striker && horizontalBallDistance < 13.5F;
            } else if (car.heldPowerup == Powerup::Freezer) {
                controls.powerupPressed = ballThreatensTeam && horizontalBallDistance < 19.0F
                    && ballSpeed > 7.0F;
            } else if (car.heldPowerup == Powerup::Magnetizer) {
                controls.powerupPressed = striker && horizontalBallDistance < 9.5F;
            }
        }
        if (avoidance.yielding) {
            if (carTransform.position.y < 0.8F) {
                const Vec3 velocity = physics.linearVelocity(car.body);
                const Vec3 forward = forwardFromHeading(car.heading);
                const float signedSpeed = velocity.x * forward.x + velocity.z * forward.z;
                controls.throttle = avoidance.throttleLimit < 0.35F && std::abs(signedSpeed) > 2.0F
                    ? (signedSpeed > 0.0F ? -0.35F : 0.35F)
                    : clamp(controls.throttle, -avoidance.throttleLimit, avoidance.throttleLimit);
            }
            controls.boostHeld = false;
            controls.jumpPressed = false;
            controls.dodgePressed = false;
        }
        return controls;
    }

    void advanceTeamRotation(int team, int touchingSlot) {
        const int first = firstCarForTeam(team);
        const int rank = touchingSlot - first;
        if (rank >= 0 && rank < activeCarsPerTeam()) {
            rotationStriker[team] = first + (rank + 1) % activeCarsPerTeam();
        }
    }

    void checkBallImpact(Vec3 ballVelocity) {
        hitSoundCooldown = std::max(0.0F, hitSoundCooldown - FixedStep);
        const float velocityChange = length(subtract(ballVelocity, previousBallVelocity));
        int touchingCar = -1;
        float closest = 1000.0F;
        for (Car &car : cars) {
            if (!isCarAvailable(car.slot)) continue;
            if (!physics.touched(ball, car.body)) {
                car.contactSeparation += FixedStep;
                continue;
            }
            // A continuous dribble is one touch. Rearm only after genuine separation.
            if (car.contactSeparation >= 0.075F) {
                const float distance = length(subtract(physics.transform(ball).position, physics.transform(car.body).position));
                if (distance < closest) { closest = distance; touchingCar = car.slot; }
            }
            car.contactSeparation = 0.0F;
        }
        if (touchingCar < 0 && velocityChange > 4.1F && hitSoundCooldown <= 0.0F) {
            audio.playHit(velocityChange);
            emitBurst(physics.transform(ball).position, GOLD, 10, clamp(velocityChange * 0.22F, 2.0F, 5.0F), 0.11F);
            shake = std::max(shake, clamp(velocityChange * 0.018F, 0.08F, 0.35F));
            hitSoundCooldown = 0.11F;
        }
        if (touchingCar >= 0) {
            Car &car = cars[touchingCar];
            car.aiShotCommitted = false;
            car.touchRecovery = 0.75F;
            ++rallyTouches;
            serveInProgress = false;
            bestRallyTouches = std::max(bestRallyTouches, rallyTouches);
            if (physics.transform(car.body).position.y > 1.05F) {
                ++matchAerialTouches[car.team];
                if (academyActive && touchingCar == 0) academyAerialTouch = true;
            }
            if (registerTeamTouch(car.team) == TouchResult::Fault && competitiveMode()) {
                scorePoint(1 - car.team);
                return;
            }
            const ContactImpact impact = physics.contactImpact(car.body, ball);
            const Vec3 change = volleyVelocityChange(impact.normal, impact.closingSpeed,
                physics.linearVelocity(car.body), physics.linearVelocity(ball));
            physics.addImpulse(ball, {change.x * BallMass, change.y * BallMass, change.z * BallMass});
            if (impact.closingSpeed > 2.0F && ballFreezeTimer <= 0.0F)
                physics.setLinearVelocity(ball, controlledVolleyVelocity(
                    {physics.transform(ball).position, physics.linearVelocity(ball)},
                    difficulty == Difficulty::Rookie ? 10.44F : 18.0F));
            advanceTeamRotation(car.team, touchingCar);
            tacticsTimer = 0.0F;
            const float strength = std::max(velocityChange, impact.closingSpeed);
            audio.playHit(std::max(3.0F, strength));
            hitSoundCooldown = 0.11F;
            shake = std::max(shake, clamp(strength * 0.015F, 0.04F, 0.28F));
            emitBurst(physics.transform(ball).position, car.paint,
                strength > 12.0F ? 18 : 8, clamp(strength * 0.25F, 1.5F, 4.5F), 0.12F);
        }
        previousBallVelocity = physics.linearVelocity(ball);
    }

    void captureReplayFrame(bool force = false) {
        if (!competitiveMode() || (!force && simulationTick % 2U != 0U)) {
            return;
        }
        ReplayFrame frame;
        frame.ball = physics.transform(ball);
        for (std::size_t index = 0; index < cars.size(); ++index) {
            frame.cars[index] = physics.transform(cars[index].body);
            if (isCarAvailable(static_cast<int>(index))) frame.visibleCars |= static_cast<std::uint8_t>(1U << index);
        }
        replayFrames.push_back(frame);
        constexpr std::size_t MaximumReplayFrames = 300;
        if (replayFrames.size() > MaximumReplayFrames) {
            replayFrames.pop_front();
        }
    }

    const ReplayFrame *currentReplayFrame() const {
        if (replayFrames.empty()) {
            return nullptr;
        }
        const float frame = std::isfinite(replayPlaybackFrame)
            ? clamp(replayPlaybackFrame, 0.0F, static_cast<float>(replayFrames.size() - 1)) : 0.0F;
        const std::size_t index = static_cast<std::size_t>(frame);
        return &replayFrames[index];
    }

    ReplayFrame sampledReplayFrame() const {
        if (replayFrames.empty()) return {};
        const float frame = std::isfinite(replayPlaybackFrame)
            ? clamp(replayPlaybackFrame, 0.0F, static_cast<float>(replayFrames.size() - 1)) : 0.0F;
        const auto index = static_cast<std::size_t>(frame);
        const ReplayFrame &from = replayFrames[index];
        const ReplayFrame &to = replayFrames[std::min(index + 1, replayFrames.size() - 1)];
        const float amount = frame - static_cast<float>(index);
        const auto interpolate = [amount](Transform a, Transform b) {
            const Vector3 position = Vector3Lerp(toRay(a.position), toRay(b.position), amount);
            const Quaternion rotation = QuaternionSlerp(toRay(a.rotation), toRay(b.rotation), amount);
            return Transform{{position.x, position.y, position.z}, {rotation.x, rotation.y, rotation.z, rotation.w}};
        };
        ReplayFrame result = from;
        result.ball = interpolate(from.ball, to.ball);
        for (std::size_t car = 0; car < cars.size(); ++car) {
            // Hide respawning cars at their recorded time; never animate a teleport
            // between the parking location and a recovered court position.
            if ((from.visibleCars & to.visibleCars & (1U << car)) != 0
                && length(subtract(from.cars[car].position, to.cars[car].position)) < 8.0F)
                result.cars[car] = interpolate(from.cars[car], to.cars[car]);
        }
        return result;
    }

    void finishPointSequence() {
        if (pendingGameOver) {
            recordCareerMatch();
            recordArcadeCupResult();
            state = MatchState::GameOver;
        } else {
            resetRound(scoringTeam);
            state = MatchState::ServeCountdown;
        }
    }

    void scorePoint(int team) {
        if (state != MatchState::Playing || team < 0 || team > 1) return;
        if (automatedPlayer) {
            const Vec3 ballPosition = physics.transform(ball).position;
            std::array<Vec3, MaximumCars> carPositions{};
            for (std::size_t index = 0; index < cars.size(); ++index) {
                carPositions[index] = physics.transform(cars[index].body).position;
            }
            TraceLog(
                LOG_WARNING,
                "RALLY: point team=%d mode=%d after_touches=%d ball=(%.1f,%.1f,%.1f) car_z=(%.1f,%.1f,%.1f|%.1f,%.1f,%.1f)",
                team,
                static_cast<int>(gameMode),
                rallyTouches,
                ballPosition.x,
                ballPosition.y,
                ballPosition.z,
                carPositions[0].z,
                carPositions[1].z,
                carPositions[2].z,
                carPositions[3].z,
                carPositions[4].z,
                carPositions[5].z);
        }
        ++score[team];
        captureReplayFrame(true);
        scoringTeam = team;
        const int result = winnerAfterPoint(score, scoreLimit, matchTime, overtime);
        winner = result >= 0 ? result : team;
        pendingGameOver = result >= 0;
        if (!pendingGameOver && matchTime <= 0.0F) overtime = true;
        if (pendingGameOver) {
            recordCareerMatch();
            recordArcadeCupResult();
        }
        pointTimer = 0.85F;
        const std::size_t replayLeadFrames = std::min<std::size_t>(replayFrames.size(), 180);
        replayPlaybackFrame = static_cast<float>(replayFrames.size() - replayLeadFrames);
        state = MatchState::PointWon;
        audio.play(audio.score);
        const Vec3 position = physics.transform(ball).position;
        emitBurst({position.x, 1.0F, position.z}, team == 0 ? SKYBLUE : ORANGE, 48, 6.2F, 0.18F);
        shake = 0.65F;
    }

    void applyRookieBallAssist() {
        if (difficulty != Difficulty::Rookie) {
            return;
        }
        Vec3 velocity = physics.linearVelocity(ball);
        const float speed = length(velocity);
        const float RookieMaximumBallSpeed = thirdTouchBoostTimer > 0.0F ? 18.0F : 15.0F;
        if (speed > RookieMaximumBallSpeed) {
            const float scale = RookieMaximumBallSpeed / speed;
            velocity.x *= scale;
            velocity.y *= scale;
            velocity.z *= scale;
            physics.setLinearVelocity(ball, velocity);
        }
    }

    const char *targetGradeName(TargetGrade grade) const {
        switch (grade) {
        case TargetGrade::Bullseye: return "BULLSEYE";
        case TargetGrade::Great: return "GREAT";
        case TargetGrade::Good: return "GOOD";
        case TargetGrade::InPlay: return "IN PLAY";
        default: return "MISS";
        }
    }

    Color targetGradeColor(TargetGrade grade) const {
        switch (grade) {
        case TargetGrade::Bullseye: return GOLD;
        case TargetGrade::Great: return SKYBLUE;
        case TargetGrade::Good: return Color{82, 225, 173, 255};
        case TargetGrade::InPlay: return RAYWHITE;
        default: return ORANGE;
        }
    }

    void completeTargetChallenge() {
        physics.setLinearVelocity(ball, {});
        physics.setAngularVelocity(ball, {});
        challengeNewRecord = challengeScore > challengeBestScore;
        challengeBestScore = std::max(challengeBestScore, challengeScore);
        challengeRecordCombo = std::max(challengeRecordCombo, challengeBestCombo);
        commitCareerProgress(100 + std::min(300, challengeScore / 4), "TARGET RUN COMPLETE");
        if (challengeNewRecord) {
            saveSettings("NEW TARGET CHALLENGE RECORD");
        } else {
            saveSettings("TARGET CHALLENGE COMPLETE");
        }
        trainingResetTimer = 0.0F;
        state = MatchState::GameOver;
        audio.play(audio.score);
    }

    void resolvePracticeLanding(Vec3 position, bool outOfBounds = false) {
        if (trainingBallHasTouchedGround) {
            return;
        }
        trainingBallHasTouchedGround = true;
        if (gameMode == GameMode::Training) {
            ++trainingCompleted;
            const bool returned = trainingReturnSuccessful && position.z < 0.0F && !outOfBounds;
            trainingStreak = returned ? trainingStreak + 1 : 0;
            trainingBestStreak = std::max(trainingBestStreak, trainingStreak);
        }
        if (gameMode == GameMode::TargetChallenge) {
            const TargetScore result = scoreTargetLanding(
                position,
                challengeTarget,
                trainingReturnSuccessful && !outOfBounds,
                challengeCombo);
            challengeScore += result.awardedPoints;
            challengeCombo = result.nextCombo;
            challengeBestCombo = std::max(challengeBestCombo, challengeCombo);
            if (result.grade == TargetGrade::Good
                || result.grade == TargetGrade::Great
                || result.grade == TargetGrade::Bullseye) {
                ++challengeTargetsHit;
            }
            if (trainingReturnSuccessful && !outOfBounds && position.z < 0.0F) {
                ++trainingReturns;
            }
            touchNotice = TextFormat("%s  +%d", targetGradeName(result.grade), result.awardedPoints);
            touchNoticeTimer = 1.55F;
            if (result.grade == TargetGrade::Bullseye) {
                emitBurst(challengeTarget, GOLD, 32, 5.8F, 0.16F);
                shake = std::max(shake, 0.35F);
                audio.play(audio.score);
            } else if (result.awardedPoints > 0) {
                emitBurst(position, SKYBLUE, 14, 3.4F, 0.11F);
            }
        } else if (trainingReturnSuccessful && position.z < 0.0F && !outOfBounds) {
            ++trainingReturns;
            touchNotice = "RETURN IN  /  NEXT FEED";
            touchNoticeTimer = 1.35F;
        } else if (rallyTouches > 0) {
            touchNotice = "RETURN OUT  /  NEXT FEED";
            touchNoticeTimer = 1.35F;
        } else {
            touchNotice = "MISSED  /  NEXT FEED";
            touchNoticeTimer = 1.35F;
        }
        trainingResetTimer = gameMode == GameMode::TargetChallenge ? 1.55F : 1.35F;
        physics.setLinearVelocity(ball, {});
        physics.setAngularVelocity(ball, {});
    }

    bool resolveCompetitiveBall() {
        const Vec3 position = physics.transform(ball).position;
        if (physics.touched(ball, courtFloor)) {
            scorePoint(position.z >= 0.0F ? 1 : 0);
            return true;
        }
        if (ballEscaped(position)) {
            touchNotice = "BALL OUT  /  LAST TOUCH FAULT";
            touchNoticeTimer = 2.0F;
            scorePoint(lastTouchTeam >= 0 ? 1 - lastTouchTeam : 1 - servingTeam);
            return true;
        }
        return false;
    }

    void academyFixedUpdate() {
        if (academyTransitionTimer > 0.0F) {
            academyTransitionTimer = std::max(0.0F, academyTransitionTimer - FixedStep);
            if (academyTransitionTimer <= 0.0F) {
                advanceAcademyLesson();
            }
            return;
        }

        academyLessonTimer = std::max(0.0F, academyLessonTimer - FixedStep);
        academyRunTimer += FixedStep;
        physics.step(FixedStep);
        checkCarOutOfBounds();
        if (!isCarAvailable(0)) {
            return;
        }
        rallyTime += FixedStep;
        captureAcademyGhostFrame();
        recordBallTrail();
        const Vec3 ballPosition = physics.transform(ball).position;
        const Vec3 ballVelocity = physics.linearVelocity(ball);
        checkBallImpact(ballVelocity);
        const Vec3 playerPosition = physics.transform(cars[0].body).position;
        academyPeakHeight = std::max(academyPeakHeight, playerPosition.y);

        if (academyLesson == AcademyLesson::BoostGates) {
            if (academyGateIndex < static_cast<int>(AcademyGatePositions.size())
                && length2D(subtract(playerPosition, AcademyGatePositions[static_cast<std::size_t>(academyGateIndex)])) < 1.85F) {
                emitBurst(AcademyGatePositions[static_cast<std::size_t>(academyGateIndex)], GOLD, 18, 4.0F, 0.12F);
                ++academyGateIndex;
                audio.play(audio.menuMove);
            }
            if (academyGateIndex >= static_cast<int>(AcademyGatePositions.size()) && academyBoostUsed) {
                completeAcademyLesson();
            }
        } else if (academyLesson == AcademyLesson::DoubleJump) {
            if (playerPosition.y >= 5.5F && cars[0].jumpsUsed >= 2 && academyBoostUsed) {
                completeAcademyLesson();
            }
        } else {
            if (!trainingReturnSuccessful && rallyTouches > 0
                && previousBallZ > 0.0F && ballPosition.z <= 0.0F
                && ballPosition.y > 2.56F + BallRadius) {
                trainingReturnSuccessful = true;
            }
            if (academyLesson == AcademyLesson::AerialReturn && trainingReturnSuccessful && academyAerialTouch) {
                completeAcademyLesson();
            } else if (academyLesson == AcademyLesson::TargetLanding
                && trainingReturnSuccessful && physics.touched(ball, courtFloor)) {
                const float targetDistance = length2D(subtract(ballPosition, challengeTarget));
                if (targetDistance <= 5.2F) {
                    completeAcademyLesson();
                } else {
                    retryAcademyLesson("TARGET MISSED");
                    return;
                }
            }
            if (physics.touched(ball, courtFloor) && (!trainingReturnSuccessful
                || (academyLesson == AcademyLesson::AerialReturn && !academyAerialTouch))) {
                retryAcademyLesson("RETURN MISSED");
                return;
            }
            if (ballPosition.y < -5.0F || std::abs(ballPosition.x) > 22.0F || std::abs(ballPosition.z) > 30.0F) {
                retryAcademyLesson("BALL LOST");
                return;
            }
            previousBallZ = ballPosition.z;
        }

        if (academyLessonTimer <= 0.0F) {
            retryAcademyLesson("TIME EXPIRED");
        }
    }

    void fixedUpdate(Controls controls, Controls playerTwoControls = {}) {
        ++simulationTick;
        thirdTouchBoostTimer = std::max(0.0F, thirdTouchBoostTimer - FixedStep);
        updateTactics(FixedStep);
        tickCarRespawns(FixedStep);
        tickBoostPads(FixedStep);
        tickPowerupSystem(FixedStep);
        driveCar(cars[0], automatedPlayer && competitiveMode() ? aiControls(cars[0], FixedStep) : controls, FixedStep);
        if (academyActive) {
            academyFixedUpdate();
            return;
        }
        if (practiceMode()) {
            if (trainingResetTimer > 0.0F) {
                trainingResetTimer -= FixedStep;
                if (trainingResetTimer <= 0.0F) {
                    if (gameMode == GameMode::TargetChallenge && trainingAttempts >= TargetChallengeShots) {
                        completeTargetChallenge();
                    } else {
                        resetTrainingServe(gameMode == GameMode::Training && trainingRepeatShot);
                    }
                }
                return;
            }
            physics.step(FixedStep);
            checkCarOutOfBounds();
            rallyTime += FixedStep;
            applyRookieBallAssist();
            recordBallTrail();
            checkBallImpact(physics.linearVelocity(ball));
            applyThirdTouchBoostIfCrossed();
            const Vec3 position = physics.transform(ball).position;
            if (!trainingReturnSuccessful && rallyTouches > 0
                && previousBallZ > 0.0F && position.z <= 0.0F
                && position.y > 2.56F + BallRadius) {
                trainingReturnSuccessful = true;
            }
            if (!trainingBallHasTouchedGround && physics.touched(ball, courtFloor)) {
                resolvePracticeLanding(position);
            }
            previousBallZ = position.z;
            if (position.y < -5.0F || std::abs(position.x) > 22.0F || std::abs(position.z) > 30.0F) {
                resolvePracticeLanding(position, true);
            }
            return;
        }
        for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
            if (!isCarAvailable(index)) {
                continue;
            }
            if (gameMode == GameMode::LocalCoop && index == 1) {
                driveCar(cars[index], playerTwoControls, FixedStep);
            } else {
                driveCar(cars[index], aiControls(cars[index], FixedStep), FixedStep);
            }
        }

        checkCarDemolitions();
        physics.step(FixedStep);
        checkCarOutOfBounds();
        applyRookieBallAssist();
        recordBallTrail();
        rallyTime += FixedStep;
        if (!overtime) {
            matchTime = std::max(0.0F, matchTime - FixedStep);
        }

        const Vec3 ballVelocity = physics.linearVelocity(ball);
        if (resolveCompetitiveBall()) return;
        checkBallImpact(ballVelocity);
        if (state != MatchState::Playing) {
            return;
        }
        applyThirdTouchBoostIfCrossed();
        fastestBallSpeed = std::max(fastestBallSpeed, length(physics.linearVelocity(ball)));
        captureReplayFrame();
        previousBallZ = physics.transform(ball).position.z;

        // At 0:00 the live rally continues. scorePoint resolves a winner or sudden death.
    }

    void countdownFixedUpdate(Controls controls, Controls playerTwoControls = {}) {
        tickCarRespawns(FixedStep);
        tickBoostPads(FixedStep);
        driveCar(cars[0], controls, FixedStep);
        if (competitiveMode()) {
            for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
                if (!isCarAvailable(index)) {
                    continue;
                }
                driveCar(cars[index], gameMode == GameMode::LocalCoop && index == 1 ? playerTwoControls : Controls{}, FixedStep);
            }
        }

        physics.step(FixedStep);
        checkCarOutOfBounds();
        physics.setTransform(ball, servePosition, {});
        physics.setLinearVelocity(ball, {});
        physics.setAngularVelocity(ball, {});

        serveCountdown = std::max(0.0F, serveCountdown - FixedStep);
        const int shownNumber = static_cast<int>(std::ceil(serveCountdown));
        if (shownNumber > 0 && shownNumber < countdownCue) {
            countdownCue = shownNumber;
            audio.play(audio.countdown);
        }
        if (serveCountdown <= 0.0F) {
            audio.play(audio.countdown);
            launchServe();
        }
    }

    void updateParticles(float deltaSeconds) {
        for (Particle &particle : particles) {
            particle.life -= deltaSeconds;
            particle.velocity.y -= 6.0F * deltaSeconds;
            particle.position.x += particle.velocity.x * deltaSeconds;
            particle.position.y += particle.velocity.y * deltaSeconds;
            particle.position.z += particle.velocity.z * deltaSeconds;
        }
        std::erase_if(particles, [](const Particle &particle) { return particle.life <= 0.0F; });
        for (TrailPoint &point : ballTrail) {
            point.life -= deltaSeconds;
        }
        std::erase_if(ballTrail, [](const TrailPoint &point) { return point.life <= 0.0F; });
    }

    void updateCamera(float deltaSeconds) {
        Vector3 desiredPosition{};
        Vector3 desiredTarget{};
        float desiredFov = 58.0F;
        bool lockBallFraming = false;
        if (state == MatchState::Title || state == MatchState::Loading) {
            const float angle = totalTime * 0.22F;
            desiredPosition = {std::sin(angle) * 31.0F, 12.5F, std::cos(angle) * 31.0F};
            desiredTarget = {0.0F, 1.8F, 0.0F};
        } else if (state == MatchState::GoalReplay && currentReplayFrame() != nullptr) {
            const Vec3 ballPosition = sampledReplayFrame().ball.position;
            const float orbit = replayPlaybackFrame * 0.018F + (scoringTeam == 0 ? 0.0F : Pi);
            desiredPosition = {
                ballPosition.x + std::sin(orbit) * 10.5F,
                clamp(ballPosition.y + 4.2F, 5.0F, 12.5F),
                ballPosition.z + std::cos(orbit) * 10.5F};
            desiredTarget = {ballPosition.x, ballPosition.y + 0.35F, ballPosition.z};
            desiredFov = 54.0F;
        } else if (scriptedAerialTest) {
            const Vec3 playerPosition = physics.transform(cars[0].body).position;
            desiredPosition = {13.5F, 7.2F, 16.0F};
            desiredTarget = {playerPosition.x, playerPosition.y + 0.4F, playerPosition.z};
            desiredFov = 61.0F;
        } else if (cameraMode == CameraMode::Ball) {
            const Transform player = physics.transform(cars[0].body);
            const bool playerRespawning = cars[0].respawnTimer > 0.0F;
            const Vec3 playerPosition = playerRespawning ? carRespawnPosition(cars[0]) : player.position;
            const Vec3 ballPosition = physics.transform(ball).position;
            const GameplayCameraPose pose = gameplayCameraPose(
                playerPosition,
                playerRespawning ? Vec3{} : physics.linearVelocity(cars[0].body),
                playerRespawning ? carRespawnHeading(cars[0]) : cars[0].heading,
                ballPosition,
                cameraMode,
                gameMode == GameMode::ThreeVsThree
                    ? GameplayCameraLayout::WideTeam
                    : GameplayCameraLayout::Standard);
            desiredPosition = pose.position;
            desiredTarget = pose.target;
            desiredFov = pose.fov;
            lockBallFraming = true;
        } else {
            const Transform player = physics.transform(cars[0].body);
            const bool playerRespawning = cars[0].respawnTimer > 0.0F;
            const Vec3 playerPosition = playerRespawning ? carRespawnPosition(cars[0]) : player.position;
            const GameplayCameraPose pose = gameplayCameraPose(
                playerPosition,
                playerRespawning ? Vec3{} : physics.linearVelocity(cars[0].body),
                playerRespawning ? carRespawnHeading(cars[0]) : cars[0].heading,
                physics.transform(ball).position,
                cameraMode,
                gameMode == GameMode::ThreeVsThree
                    ? GameplayCameraLayout::WideTeam
                    : GameplayCameraLayout::Standard);
            desiredPosition = pose.position;
            desiredTarget = pose.target;
            desiredFov = pose.fov;
        }

        const float response = 1.0F - std::exp(-6.5F * deltaSeconds);
        camera.position = Vector3Lerp(camera.position, desiredPosition, response);
        if (state != MatchState::Title && state != MatchState::Loading)
            camera.position = arenaCameraPosition(camera.position);
        camera.target = Vector3Lerp(camera.target, desiredTarget, response);
        if (lockBallFraming) {
            const Vec3 carPosition = cars[0].respawnTimer > 0.0F ? carRespawnPosition(cars[0]) : physics.transform(cars[0].body).position;
            const auto framing = ballCameraFraming(camera.position, carPosition, physics.transform(ball).position,
                static_cast<float>(ScreenWidth) / ScreenHeight, desiredFov);
            camera.target = framing.target;
            desiredFov = framing.fov;
        }
        camera.fovy += (desiredFov - camera.fovy) * response;
        if (shake > 0.0F) {
            camera.position.x += static_cast<float>(cosmeticRandom(-100, 100)) * 0.01F * shake * preferences.shakeGain();
            camera.position.y += static_cast<float>(cosmeticRandom(-100, 100)) * 0.006F * shake * preferences.shakeGain();
            shake = std::max(0.0F, shake - deltaSeconds * 2.8F);
        }
    }

    void updateLocalCoopCameras(float deltaSeconds) {
        if (gameMode != GameMode::LocalCoop || state == MatchState::Title || state == MatchState::Loading) {
            return;
        }
        const Vec3 ballPosition = physics.transform(ball).position;
        for (int slot = 0; slot < 2; ++slot) {
            const Transform player = physics.transform(cars[slot].body);
            const bool playerRespawning = cars[slot].respawnTimer > 0.0F;
            const Vec3 playerPosition = playerRespawning ? carRespawnPosition(cars[slot]) : player.position;
            const Vec3 playerVelocity = playerRespawning ? Vec3{} : physics.linearVelocity(cars[slot].body);
            const GameplayCameraPose pose = gameplayCameraPose(
                playerPosition,
                playerVelocity,
                playerRespawning ? carRespawnHeading(cars[slot]) : cars[slot].heading,
                ballPosition,
                slot == 0 ? cameraMode : playerTwoCameraMode,
                GameplayCameraLayout::SplitScreen);
            const float response = 1.0F - std::exp(-7.5F * deltaSeconds);
            Camera3D &localCamera = localCoopCameras[static_cast<std::size_t>(slot)];
            localCamera.position = Vector3Lerp(localCamera.position, pose.position, response);
            localCamera.position = arenaCameraPosition(localCamera.position);
            localCamera.target = Vector3Lerp(localCamera.target, pose.target, response);
            if ((slot == 0 ? cameraMode : playerTwoCameraMode) == CameraMode::Ball) {
                const auto framing = ballCameraFraming(localCamera.position, playerPosition, ballPosition,
                    static_cast<float>(ScreenWidth / 2) / ScreenHeight, pose.fov);
                localCamera.target = framing.target;
                localCamera.fovy += (framing.fov - localCamera.fovy) * response;
            } else {
                localCamera.fovy += (pose.fov - localCamera.fovy) * response;
            }
            localCamera.up = {0.0F, 1.0F, 0.0F};
            localCamera.projection = CAMERA_PERSPECTIVE;
        }
    }

    bool menuSelectPressed() const {
        return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)
            || (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
    }

    void applyPlayerCustomization() {
        cars[0].bodyStyle = bodyStyleIndex;
        cars[0].paint = BodyColors[bodyColorIndex].color;
        cars[0].wheelColor = WheelColors[wheelColorIndex].color;
        cars[0].spoilerColor = spoilerColorIndex == 1
            ? cars[0].paint
            : SpoilerColors[spoilerColorIndex].color;
        cars[0].decalColor = decalColorIndex == static_cast<int>(DecalColors.size()) - 1
            ? cars[0].paint
            : DecalColors[decalColorIndex].color;
        cars[0].boostColor = BoostColors[boostColorIndex].color;
    }

    void cycleCustomization(int direction) {
        if (customizeMenuIndex == 0) {
            bodyColorIndex = (bodyColorIndex + direction + static_cast<int>(BodyColors.size()))
                % static_cast<int>(BodyColors.size());
        } else if (customizeMenuIndex == 1) {
            bodyStyleIndex = (bodyStyleIndex + direction + static_cast<int>(BodyStyles.size()))
                % static_cast<int>(BodyStyles.size());
        } else if (customizeMenuIndex == 2) {
            wheelColorIndex = (wheelColorIndex + direction + static_cast<int>(WheelColors.size()))
                % static_cast<int>(WheelColors.size());
        } else if (customizeMenuIndex == 3) {
            spoilerColorIndex = (spoilerColorIndex + direction + static_cast<int>(SpoilerColors.size()))
                % static_cast<int>(SpoilerColors.size());
        } else if (customizeMenuIndex == 4) {
            decalColorIndex = (decalColorIndex + direction + static_cast<int>(DecalColors.size()))
                % static_cast<int>(DecalColors.size());
        } else if (customizeMenuIndex == 5) {
            boostColorIndex = (boostColorIndex + direction + static_cast<int>(BoostColors.size()))
                % static_cast<int>(BoostColors.size());
        } else {
            return;
        }
        applyPlayerCustomization();
        audio.play(audio.menuMove);
        saveSettings("CUSTOMIZATION SAVED");
    }

    std::string matchDurationLabel() const {
        return TextFormat("%d:%02d", matchDurationSeconds / 60, matchDurationSeconds % 60);
    }

    void cycleMatchSetup(int direction) {
        if (matchSetupMenuIndex == 0) {
            difficulty = difficulty == Difficulty::Pro ? Difficulty::Rookie : Difficulty::Pro;
            applyDifficultyPhysics();
        } else if (matchSetupMenuIndex == 1) {
            constexpr std::array<int, 3> durations{120, 180, 300};
            const auto current = std::find(durations.begin(), durations.end(), matchDurationSeconds);
            const int index = current == durations.end() ? 1 : static_cast<int>(current - durations.begin());
            matchDurationSeconds = durations[static_cast<std::size_t>((index + direction + static_cast<int>(durations.size()))
                % static_cast<int>(durations.size()))];
        } else if (matchSetupMenuIndex == 2) {
            constexpr std::array<int, 4> limits{3, 5, 7, 9};
            const auto current = std::find(limits.begin(), limits.end(), scoreLimit);
            const int index = current == limits.end() ? 2 : static_cast<int>(current - limits.begin());
            scoreLimit = limits[static_cast<std::size_t>((index + direction + static_cast<int>(limits.size()))
                % static_cast<int>(limits.size()))];
        } else if (matchSetupMenuIndex == 3) {
            ballElasticity = clamp(ballElasticity + static_cast<float>(direction) * 0.05F, 0.55F, 0.95F);
            physics.setRestitution(ball, ballElasticity);
        } else if (matchSetupMenuIndex == 4) {
            powerupsEnabled = !powerupsEnabled;
        } else {
            return;
        }
        audio.play(audio.menuMove);
        saveSettings("MATCH SETUP SAVED");
    }

    void openAboutLink(int index) const {
        const char *url = index == 0 ? RepositoryUrl : (index == 1 ? IssuesUrl : RoadmapUrl);
        if (smokeTestMode) {
            TraceLog(LOG_INFO, "SMOKE: verified about link %s", url);
            return;
        }
        OpenURL(url);
    }

    void openPreferences(bool fromPause) {
        preferencesFromPause = fromPause;
        preferencesMenuIndex = 0;
        menuPage = MenuPage::Preferences;
        settingsNotice.clear();
        settingsNoticeTimer = 0.0F;
    }

    void closePreferences() {
        preferencesFromPause = false;
        menuPage = MenuPage::Main;
    }

    void cyclePreferences(int direction) {
        int *value = preferencesMenuIndex == 0 ? &preferences.musicVolume
            : preferencesMenuIndex == 1 ? &preferences.effectsVolume
            : preferencesMenuIndex == 2 ? &preferences.cameraShake : nullptr;
        if (value) *value = std::clamp(*value + direction * 10, 0, 100);
        else if (preferencesMenuIndex == 3) preferences.pointReplays = !preferences.pointReplays;
        else if (preferencesMenuIndex == 4) preferences = {};
        else return;
        audio.applyMix(preferences.musicGain(), preferences.effectsGain());
        saveSettings("AUDIO / COMFORT SAVED");
        audio.play(audio.menuMove);
    }

    void handleMenuInput() {
        if (menuPage == MenuPage::Controls && bindingCaptureIndex >= 0) {
            const int pressedKey = GetKeyPressed();
            if (pressedKey == KEY_NULL) {
                return;
            }
            if (pressedKey == KEY_ESCAPE) {
                bindingCaptureIndex = -1;
                settingsNotice = "CHANGE CANCELLED";
                settingsNoticeTimer = 1.5F;
                audio.play(audio.menuMove);
                return;
            }
            if (assignBinding(static_cast<std::size_t>(bindingCaptureIndex), pressedKey)) {
                bindingCaptureIndex = -1;
                audio.play(audio.menuMove);
            }
            return;
        }

        const bool gamepadAvailable = IsGamepadAvailable(0);
        const bool up = IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)
            || (gamepadAvailable && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP));
        const bool down = IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN)
            || (gamepadAvailable && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN));
        const bool left = IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)
            || (gamepadAvailable && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT));
        const bool right = IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)
            || (gamepadAvailable && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT));

        int *selectionPointer = &mainMenuIndex;
        int itemCount = 14;
        if (menuPage == MenuPage::MatchSetup) {
            selectionPointer = &matchSetupMenuIndex;
            itemCount = 6;
        } else if (menuPage == MenuPage::Customize) {
            selectionPointer = &customizeMenuIndex;
            itemCount = 7;
        } else if (menuPage == MenuPage::Controls) {
            selectionPointer = &controlsMenuIndex;
            itemCount = static_cast<int>(BindingCount) + 2;
        } else if (menuPage == MenuPage::About) {
            selectionPointer = &aboutMenuIndex;
            itemCount = 4;
        } else if (menuPage == MenuPage::Preferences) {
            selectionPointer = &preferencesMenuIndex;
            itemCount = 6;
        }
        int &selection = *selectionPointer;
        if (up || down) {
            selection = (selection + (down ? 1 : -1) + itemCount) % itemCount;
            audio.play(audio.menuMove);
        }

        if (menuPage == MenuPage::Customize && (left || right)) {
            cycleCustomization(right ? 1 : -1);
        }
        if (menuPage == MenuPage::MatchSetup && matchSetupMenuIndex < 5 && (left || right)) {
            cycleMatchSetup(right ? 1 : -1);
        }
        if (menuPage == MenuPage::Main && mainMenuIndex == 7 && (left || right)) {
            cycleArena(right ? 1 : -1);
        }
        if (menuPage == MenuPage::Preferences && preferencesMenuIndex < 4 && (left || right)) {
            cyclePreferences(right ? 1 : -1);
        }

        if ((IsKeyPressed(KEY_ESCAPE) || (gamepadAvailable && (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)
            || (preferencesFromPause && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT))))) && menuPage != MenuPage::Main) {
            closePreferences();
            audio.play(audio.menuMove);
            return;
        }

        if (!menuSelectPressed()) {
            return;
        }
        if (menuPage == MenuPage::Main) {
            audio.play(audio.menuMove);
            if (mainMenuIndex == 0) {
                startArcadeCupRun();
            } else if (mainMenuIndex == 1) {
                beginLoadingMatch();
            } else if (mainMenuIndex == 2) {
                beginLoadingThreeVsThree();
            } else if (mainMenuIndex == 3) {
                beginLoadingLocalCoop();
            } else if (mainMenuIndex == 4) {
                beginLoadingAcademy();
            } else if (mainMenuIndex == 5) {
                beginLoadingTraining();
            } else if (mainMenuIndex == 6) {
                beginLoadingTargetChallenge();
            } else if (mainMenuIndex == 7) {
                cycleArena(1);
            } else if (mainMenuIndex == 8) {
                menuPage = MenuPage::Customize;
                customizeMenuIndex = 0;
            } else if (mainMenuIndex == 9) {
                menuPage = MenuPage::Controls;
                controlsMenuIndex = 0;
            } else if (mainMenuIndex == 10) {
                menuPage = MenuPage::MatchSetup;
                matchSetupMenuIndex = 0;
            } else if (mainMenuIndex == 11) {
                menuPage = MenuPage::About;
                aboutMenuIndex = 0;
            } else if (mainMenuIndex == 12) {
                openPreferences(false);
            } else {
                shouldExit = true;
            }
        } else if (menuPage == MenuPage::MatchSetup) {
            if (matchSetupMenuIndex == 5) {
                audio.play(audio.menuMove);
                menuPage = MenuPage::Main;
            } else {
                cycleMatchSetup(1);
            }
        } else if (menuPage == MenuPage::Customize) {
            if (customizeMenuIndex == 6) {
                audio.play(audio.menuMove);
                menuPage = MenuPage::Main;
            } else {
                cycleCustomization(1);
            }
        } else if (menuPage == MenuPage::Controls) {
            audio.play(audio.menuMove);
            if (controlsMenuIndex < static_cast<int>(BindingCount)) {
                bindingCaptureIndex = controlsMenuIndex;
                settingsNotice.clear();
                while (GetKeyPressed() != KEY_NULL) {}
            } else if (controlsMenuIndex == static_cast<int>(BindingCount)) {
                resetBindings();
            } else {
                menuPage = MenuPage::Main;
            }
        } else if (menuPage == MenuPage::Preferences) {
            if (preferencesMenuIndex == 5) closePreferences();
            else cyclePreferences(1);
        } else if (menuPage == MenuPage::About) {
            audio.play(audio.menuMove);
            if (aboutMenuIndex == 3) {
                menuPage = MenuPage::Main;
            } else {
                openAboutLink(aboutMenuIndex);
            }
        }
    }

    void handlePauseInput(bool helpPressed, bool pausePressed, bool focused, bool controllerConnected) {
        const bool active = state == MatchState::Playing || state == MatchState::ServeCountdown;
        const bool disconnected = controllerWasConnected && !controllerConnected;
        const bool missingCoopController = gameMode == GameMode::LocalCoop && !controllerConnected;
        controllerWasConnected = controllerConnected;
        const bool canResume = focused && (gameMode != GameMode::LocalCoop || controllerConnected);
        const auto pause = [&](const char *reason) {
            pausedFrom = state;
            state = MatchState::Paused;
            accumulator = 0.0F;
            inputEdges = {};
            pauseNotice = reason;
        };
        const auto resume = [&]() {
            showHelp = false;
            helpPausedGame = false;
            accumulator = 0.0F;
            inputEdges = {};
            pauseNotice.clear();
            state = pausedFrom;
        };
        // Interruptions win over a simultaneous resume press. Returning focus or
        // plugging a controller back in always requires an explicit resume.
        if (active && (!focused || disconnected || missingCoopController)) {
            pause((disconnected || missingCoopController) ? (gameMode == GameMode::LocalCoop
                ? "P2 DISCONNECTED / RECONNECT TO RESUME"
                : "CONTROLLER DISCONNECTED / KEYBOARD OR RECONNECT")
                : "WINDOW INACTIVE / RESUME WHEN READY");
            helpPausedGame = false;
            return;
        }
        if (helpPressed) {
            showHelp = !showHelp;
            if (showHelp) {
                helpPausedGame = active;
                if (active) pause("");
            } else {
                if (helpPausedGame && state == MatchState::Paused && canResume) resume();
                helpPausedGame = false;
            }
            return;
        }
        // Help opened during loading/replay must also stop the next kickoff.
        if (showHelp && active) {
            pause("");
            helpPausedGame = true;
        }
        if (pausePressed) {
            if (active && !showHelp) {
                pause("");
            } else if (state == MatchState::Paused) {
                if (canResume) resume();
                else if (!controllerConnected && gameMode == GameMode::LocalCoop)
                    pauseNotice = "P2 DISCONNECTED / RECONNECT TO RESUME";
            }
        }
    }

    bool gameplayShortcutPressed(int key, bool pressed) const {
        return pressed && std::find(bindings.begin(), bindings.end(), key) == bindings.end();
    }

    void handleGlobalInput() {
        const bool controllerConnected = IsGamepadAvailable(0);
        const bool focused = smokeTestMode || IsWindowFocused();
        if (focused && bindingCaptureIndex < 0 && (IsKeyPressed(KEY_F11)
            || (IsKeyPressed(KEY_ENTER) && (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT))))) {
            ToggleBorderlessWindowed();
            return; // Alt+Enter must not also select a menu item or skip a replay.
        }
        if (focused && state == MatchState::Paused && preferencesFromPause) {
            handleMenuInput();
            return;
        }
        if (focused && !showHelp && state == MatchState::Title
            && menuPage == MenuPage::Controls && bindingCaptureIndex >= 0) {
            handleMenuInput(); // Capture/reject F1 instead of opening help over the binding dialog.
            return;
        }
        handlePauseInput(IsKeyPressed(KEY_F1), IsKeyPressed(boundKey(BindAction::Pause))
            || (controllerConnected && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)),
            focused, smokeTestMode || controllerConnected);
        if (showHelp || !focused) return; // Help is modal; background windows ignore shortcuts.
        if (state == MatchState::Paused && (gameplayShortcutPressed(KEY_F2, IsKeyPressed(KEY_F2))
            || (controllerConnected && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP)))) {
            openPreferences(true);
            return;
        }
        if (state == MatchState::Title) {
            handleMenuInput();
            return;
        }
        if (state == MatchState::GoalReplay
            && menuSelectPressed()) {
            finishPointSequence();
            return;
        }
        if (!arcadeCupActive && gameplayShortcutPressed(KEY_ONE, IsKeyPressed(KEY_ONE))) {
            difficulty = Difficulty::Rookie;
            applyDifficultyPhysics();
            saveSettings("DIFFICULTY SAVED");
        }
        if (!arcadeCupActive && gameplayShortcutPressed(KEY_TWO, IsKeyPressed(KEY_TWO))) {
            difficulty = Difficulty::Pro;
            applyDifficultyPhysics();
            saveSettings("DIFFICULTY SAVED");
        }
        if (academyActive
            && (state == MatchState::Playing || state == MatchState::ServeCountdown)
            && gameplayShortcutPressed(KEY_TAB, IsKeyPressed(KEY_TAB))) {
            touchNotice = std::string("SKIPPED  /  ") + academyLessonName();
            touchNoticeTimer = 1.2F;
            advanceAcademyLesson(true);
            audio.play(audio.menuMove);
        }
        handleTrainingCommands(
            gameplayShortcutPressed(KEY_TAB, IsKeyPressed(KEY_TAB))
                || (controllerConnected && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP)),
            gameplayShortcutPressed(KEY_L, IsKeyPressed(KEY_L))
                || (controllerConnected && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)),
            controllerConnected && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN));
        if (academyActive && (state == MatchState::Playing || state == MatchState::ServeCountdown)
            && gameplayShortcutPressed(KEY_G, IsKeyPressed(KEY_G))) {
            academyGhostEnabled = !academyGhostEnabled;
            touchNotice = std::string("PB GHOST  /  ") + (academyGhostEnabled ? "ON" : "OFF");
            touchNoticeTimer = 1.6F;
            saveSettings(academyGhostEnabled ? "ACADEMY GHOST ON" : "ACADEMY GHOST OFF");
            audio.play(audio.menuMove);
        }
        if (!arcadeCupActive && gameplayShortcutPressed(KEY_LEFT_BRACKET, IsKeyPressed(KEY_LEFT_BRACKET))) {
            ballElasticity = std::max(0.55F, ballElasticity - 0.05F);
            physics.setRestitution(ball, ballElasticity);
            saveSettings("BALL BOUNCE SAVED");
        }
        if (!arcadeCupActive && gameplayShortcutPressed(KEY_RIGHT_BRACKET, IsKeyPressed(KEY_RIGHT_BRACKET))) {
            ballElasticity = std::min(0.95F, ballElasticity + 0.05F);
            physics.setRestitution(ball, ballElasticity);
            saveSettings("BALL BOUNCE SAVED");
        }
        const bool gamepadCameraToggle = IsGamepadAvailable(0)
            && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP);
        if (gameMode == GameMode::LocalCoop && gamepadCameraToggle) {
            playerTwoCameraMode = playerTwoCameraMode == CameraMode::Car ? CameraMode::Ball : CameraMode::Car;
        }
        if ((IsKeyPressed(boundKey(BindAction::Camera)) || (gamepadCameraToggle && gameMode != GameMode::LocalCoop))
            && state != MatchState::Title
            && state != MatchState::Loading) {
            cameraMode = cameraMode == CameraMode::Car ? CameraMode::Ball : CameraMode::Car;
            cameraModeNotice = 1.5F;
            audio.play(audio.menuMove);
        }
        if (IsKeyPressed(boundKey(BindAction::Restart)) && state != MatchState::Title) {
            if (academyActive) {
                startAcademy();
            } else if (gameMode == GameMode::Training) {
                resetTrainingServe(true);
            } else if (gameMode == GameMode::TargetChallenge) {
                startTargetChallenge();
            } else {
                startMatch(gameMode);
            }
        }
        if (IsKeyPressed(boundKey(BindAction::MainMenu)) && state != MatchState::Title) {
            leaveArcadeCup();
            academyActive = false;
            state = MatchState::Title;
            menuPage = MenuPage::Main;
            showHelp = false;
            helpPausedGame = false;
            accumulator = 0.0F;
        }

        if (state == MatchState::GameOver
            && menuSelectPressed()) {
            if (arcadeCupActive) {
                advanceOrRestartArcadeCup();
            } else if (academyActive) {
                beginLoadingAcademy();
            } else {
                beginLoading(gameMode);
            }
        }
    }

    void advanceSimulation(float deltaSeconds, Controls controls, Controls playerTwoControls = {}) {
        if (state != MatchState::Playing && state != MatchState::ServeCountdown) {
            accumulator = 0.0F;
            inputEdges = {};
            return;
        }
        accumulator = std::min(accumulator + deltaSeconds, 0.2F);
        const auto queueEdges = [](InputEdges &queue, const Controls &input) {
            queue.push(static_cast<std::uint8_t>((input.jumpPressed ? net::JumpPressed : 0)
                | (input.dodgePressed ? net::DodgePressed : 0) | (input.powerupPressed ? net::PowerupPressed : 0)));
        };
        queueEdges(inputEdges[0], controls);
        queueEdges(inputEdges[1], playerTwoControls);
        const MatchState activeState = state;
        while (accumulator >= FixedStep && state == activeState) {
            Controls stepControls = controls;
            Controls stepPlayerTwoControls = playerTwoControls;
            const auto consumeEdges = [](InputEdges &queue, Controls &input) {
                const std::uint8_t edges = queue.consume();
                input.jumpPressed = (edges & net::JumpPressed) != 0;
                input.dodgePressed = (edges & net::DodgePressed) != 0;
                input.powerupPressed = (edges & net::PowerupPressed) != 0;
            };
            consumeEdges(inputEdges[0], stepControls);
            consumeEdges(inputEdges[1], stepPlayerTwoControls);
            if (scriptedAerialTest) {
                aerialTestTimer += FixedStep;
                stepControls = {};
                if (!aerialFirstJumpTriggered && aerialTestTimer >= 0.1F) {
                    stepControls.jumpPressed = true;
                    aerialFirstJumpTriggered = true;
                } else if (!aerialSecondJumpTriggered && aerialTestTimer >= 0.38F) {
                    stepControls.jumpPressed = true;
                    aerialSecondJumpTriggered = true;
                }
                if (aerialTestTimer >= 0.42F && aerialTestTimer <= 0.92F) stepControls.throttle = -1.0F;
                stepControls.boostHeld = aerialTestTimer >= 0.78F && aerialTestTimer <= 1.85F;
            } else if (scriptedSprintTest) {
                sprintTestTimer += FixedStep;
                stepControls = {};
                stepControls.throttle = 1.0F;
                stepControls.boostHeld = true;
            }
            accumulator -= FixedStep;
            if (activeState == MatchState::Playing) {
                fixedUpdate(stepControls, stepPlayerTwoControls);
            } else {
                countdownFixedUpdate(stepControls, stepPlayerTwoControls);
            }
            if (scriptedAerialTest) {
                const Transform transform = physics.transform(cars[0].body);
                const Vector3 forward = Vector3RotateByQuaternion({0.0F, 0.0F, 1.0F}, toRay(transform.rotation));
                aerialPeakHeight = std::max(aerialPeakHeight, transform.position.y);
                aerialPeakForwardY = std::max(aerialPeakForwardY, forward.y);
                aerialMaxJumpsUsed = std::max(aerialMaxJumpsUsed, cars[0].jumpsUsed);
                aerialTestComplete = aerialTestTimer >= 2.55F;
            } else if (scriptedSprintTest && !sprintTestComplete) {
                if (physics.transform(cars[0].body).position.z <= 2.0F) sprintCompletedCourse = true;
                if (sprintCompletedCourse || sprintTestTimer >= 4.0F) {
                    sprintFinishTime = sprintTestTimer;
                    sprintTestComplete = true;
                }
            }
        }
    }

    void advancePointPresentation(float deltaSeconds) {
        if (state == MatchState::PointWon) {
            pointTimer -= deltaSeconds;
            if (pointTimer <= 0.0F) {
                if (preferences.pointReplays && replayFrames.size() >= 30) {
                    state = MatchState::GoalReplay;
                } else {
                    finishPointSequence();
                }
            }
        } else if (state == MatchState::GoalReplay) {
            replayPlaybackFrame += deltaSeconds * 43.0F;
            if (replayPlaybackFrame >= static_cast<float>(replayFrames.size())) {
                finishPointSequence();
            }
        }
    }

    void update(float deltaSeconds) {
        totalTime += deltaSeconds;
        boostSoundCooldown = std::max(0.0F, boostSoundCooldown - deltaSeconds);
        cameraModeNotice = std::max(0.0F, cameraModeNotice - deltaSeconds);
        settingsNoticeTimer = std::max(0.0F, settingsNoticeTimer - deltaSeconds);
        touchNoticeTimer = std::max(0.0F, touchNoticeTimer - deltaSeconds);
        careerUnlockTimer = std::max(0.0F, careerUnlockTimer - deltaSeconds);
        handleGlobalInput();
        audio.updateMusic(state == MatchState::Title || state == MatchState::Loading);

        if (state == MatchState::Loading) {
            loadingTimer += deltaSeconds;
            if (loadingTimer >= 1.6F) {
                if (pendingGameMode == GameMode::Training) {
                    if (academyActive) {
                        startAcademy();
                    } else {
                        startTraining();
                    }
                } else if (pendingGameMode == GameMode::TargetChallenge) {
                    startTargetChallenge();
                } else {
                    startMatch(pendingGameMode);
                }
            }
        } else if (state == MatchState::Playing || state == MatchState::ServeCountdown) {
            Controls controls{};
            Controls playerTwoControls{};
            if (!scriptedAerialTest && !scriptedSprintTest) {
                controls = automatedPlayer
                    ? Controls{}
                    : (gameMode == GameMode::LocalCoop ? keyboardControls() : playerControls());
                if (gameMode == GameMode::LocalCoop && !automatedPlayer) {
                    const net::PlayerInputPacket playerTwoPacket = makeInputPacket(gamepadControls(), 1);
                    playerTwoControls = controlsFromInputPacket(playerTwoPacket);
                }
            }
            advanceSimulation(deltaSeconds, controls, playerTwoControls);
        } else if (state == MatchState::PointWon || state == MatchState::GoalReplay) {
            advancePointPresentation(deltaSeconds);
        }

        if (state != MatchState::Playing && state != MatchState::ServeCountdown) inputEdges = {};
        updateParticles(deltaSeconds);
        updateCamera(deltaSeconds);
        updateLocalCoopCameras(deltaSeconds);
    }

    void drawBlockyTree(const SceneryView &view, Vector3 base, Color leaves) const {
        drawSceneryCube(view, {base.x, base.y + 2.25F, base.z}, 0.65F, 4.5F, 0.65F, Color{103, 67, 39, 255});
        drawSceneryCube(view, {base.x, base.y + 4.8F, base.z}, 2.7F, 2.0F, 2.7F, leaves);
        drawSceneryCube(view, {base.x - 0.8F, base.y + 5.7F, base.z + 0.25F}, 1.8F, 1.55F, 1.9F, scaledColor(leaves, 1.15F));
        drawSceneryCube(view, {base.x + 0.85F, base.y + 5.45F, base.z - 0.3F}, 1.7F, 1.6F, 1.8F, scaledColor(leaves, 0.86F));
    }

    void drawEnvironmentProps(const SceneryView &view) const {
        if (activeArenaIndex == 0) {
            for (int side : {-1, 1}) {
                for (int index = -3; index <= 3; ++index) {
                    drawBlockyTree(view, 
                        {static_cast<float>(index) * 4.1F, 0.0F, static_cast<float>(side) * 25.8F},
                        index % 2 == 0 ? Color{48, 139, 70, 255} : Color{31, 108, 61, 255});
                }
            }
            for (int side : {-1, 1}) {
                drawBlockyTree(view, {static_cast<float>(side) * 20.2F, 0.0F, -13.0F}, Color{42, 127, 64, 255});
                drawBlockyTree(view, {static_cast<float>(side) * 20.2F, 0.0F, 13.0F}, Color{56, 151, 72, 255});
            }
            for (int side : {-1, 1}) {
                drawBlockyTree(view, {-9.5F, 6.0F, static_cast<float>(side) * 23.4F}, Color{43, 130, 59, 255});
                drawBlockyTree(view, {9.5F, 6.0F, static_cast<float>(side) * 23.4F}, Color{52, 151, 67, 255});
            }
        } else if (activeArenaIndex == 1) {
            drawSceneryCube(view, {0.0F, -0.18F, 26.5F}, 13.5F, 0.2F, 5.6F, Color{37, 185, 224, 255});
            drawSceneryCube(view, {0.0F, -0.22F, -26.5F}, 13.5F, 0.2F, 5.6F, Color{37, 185, 224, 255});
            drawSceneryCube(view, {0.0F, -0.12F, 23.8F}, 14.3F, 0.12F, 0.28F, RAYWHITE);
            drawSceneryCube(view, {0.0F, -0.12F, -23.8F}, 14.3F, 0.12F, 0.28F, RAYWHITE);
            drawSceneryCube(view, {20.2F, -0.16F, 0.0F}, 5.0F, 0.18F, 15.0F, Color{37, 185, 224, 255});
            drawSceneryCube(view, {-20.2F, -0.16F, 0.0F}, 5.0F, 0.18F, 15.0F, Color{37, 185, 224, 255});
            for (int side : {-1, 1}) {
                for (int end : {-1, 1}) {
                    const Vector3 base{static_cast<float>(side) * 19.0F, 0.0F, static_cast<float>(end) * 9.0F};
                    drawSceneryCube(view, {base.x, 3.0F, base.z}, 0.62F, 6.0F, 0.62F, Color{139, 91, 48, 255});
                    drawSceneryCube(view, {base.x, 6.15F, base.z}, 5.0F, 0.4F, 0.72F, Color{45, 155, 87, 255});
                    drawSceneryCube(view, {base.x, 6.15F, base.z}, 0.72F, 0.4F, 5.0F, Color{38, 137, 79, 255});
                }
            }
            for (int side : {-1, 1}) {
                drawSceneryCube(view, {static_cast<float>(side) * 16.5F, 0.65F, 17.0F}, 2.6F, 0.2F, 2.6F, Color{255, 105, 90, 255});
                drawSceneryCube(view, {static_cast<float>(side) * 16.5F, 1.7F, 17.0F}, 0.18F, 2.1F, 0.18F, RAYWHITE);
            }
            for (int side : {-1, 1}) {
                for (int x : {-8, 8}) {
                    const Vector3 base{static_cast<float>(x), 6.0F, static_cast<float>(side) * 23.5F};
                    drawSceneryCube(view, {base.x, 8.1F, base.z}, 0.58F, 4.2F, 0.58F, Color{139, 91, 48, 255});
                    drawSceneryCube(view, {base.x, 10.3F, base.z}, 4.5F, 0.42F, 0.7F, Color{45, 155, 87, 255});
                    drawSceneryCube(view, {base.x, 10.3F, base.z}, 0.7F, 0.42F, 4.5F, Color{38, 137, 79, 255});
                }
            }
        } else if (activeArenaIndex == 2) {
            for (int side : {-1, 1}) {
                for (int index = -4; index <= 4; ++index) {
                    const float height = 4.5F + static_cast<float>((index * index + side + 7) % 5) * 1.6F;
                    const float x = static_cast<float>(index) * 4.0F;
                    const float z = static_cast<float>(side) * 27.0F;
                    drawSceneryCube(view, {x, height * 0.5F, z}, 3.2F, height, 3.0F, Color{20, 27, 48, 255});
                    for (int window = 1; window < static_cast<int>(height); window += 2) {
                        drawSceneryCube(view, {x, static_cast<float>(window), z - static_cast<float>(side) * 1.52F}, 1.3F, 0.35F, 0.08F,
                            (window + index) % 3 == 0 ? Color{255, 214, 93, 255} : Color{78, 156, 225, 255});
                    }
                }
            }
        } else {
            for (int side : {-1, 1}) {
                for (int end : {-1, 1}) {
                    const Vector3 base{static_cast<float>(side) * 19.5F, 0.0F, static_cast<float>(end) * 18.0F};
                    drawSceneryCube(view, {base.x, 1.1F, base.z}, 3.8F, 2.2F, 3.2F, Color{126, 65, 40, 255});
                    drawSceneryCube(view, {base.x + static_cast<float>(side), 2.55F, base.z}, 2.4F, 1.2F, 2.2F, Color{161, 80, 42, 255});
                }
            }
            for (int end : {-1, 1}) {
                for (int x : {-9, 9}) {
                    const Vector3 base{static_cast<float>(x), 0.0F, static_cast<float>(end) * 25.0F};
                    drawSceneryCube(view, {base.x, 1.9F, base.z}, 0.55F, 3.8F, 0.55F, Color{47, 129, 67, 255});
                    drawSceneryCube(view, {base.x + 0.85F, 2.35F, base.z}, 1.7F, 0.48F, 0.48F, Color{47, 129, 67, 255});
                    drawSceneryCube(view, {base.x - 0.7F, 1.45F, base.z}, 1.4F, 0.48F, 0.48F, Color{47, 129, 67, 255});
                }
            }
            for (int side : {-1, 1}) {
                drawSceneryCube(view, {-8.5F, 8.2F, static_cast<float>(side) * 23.7F}, 5.2F, 4.4F, 3.4F, Color{136, 64, 37, 255});
                drawSceneryCube(view, {8.5F, 9.0F, static_cast<float>(side) * 23.7F}, 6.0F, 5.8F, 3.6F, Color{157, 74, 39, 255});
            }
        }
    }

    void drawArena(const SceneryView &view) const {
        const ArenaTheme &theme = activeArena();
        DrawPlane({0.0F, -0.34F, 0.0F}, {82.0F, 94.0F}, scaledColor(theme.skyTop, 0.45F));

        for (int tier = 0; tier < 4; ++tier) {
            const float x = 15.4F + static_cast<float>(tier) * 1.15F;
            const float y = 0.4F + static_cast<float>(tier) * 0.85F;
            const float height = 0.8F + static_cast<float>(tier) * 0.25F;
            const Color standColor = scaledColor(theme.floor, tier % 2 == 0 ? 0.72F : 0.9F);
            drawSceneryCube(view, {-x, y, 0.0F}, 1.1F, height, 45.0F, standColor);
            drawSceneryCube(view, {x, y, 0.0F}, 1.1F, height, 45.0F, standColor);
        }

        for (int side = -1; side <= 1; side += 2) {
            for (int z = -20; z <= 20; z += 2) {
                for (int row = 0; row < 3; ++row) {
                    const bool blueFan = ((z / 2) + row + side) % 3 != 0;
                    const Color crowd = blueFan ? scaledColor(theme.blueCourt, 1.55F) : scaledColor(theme.orangeCourt, 1.55F);
                    drawSceneryCube(view, 
                        {static_cast<float>(side) * (15.05F + static_cast<float>(row) * 1.1F),
                            2.65F + static_cast<float>(row) * 0.82F,
                            static_cast<float>(z)},
                        0.28F,
                        0.28F,
                        0.72F,
                        crowd);
                }
            }
        }

        DrawPlane({0.0F, 0.005F, 0.0F}, {28.0F, 44.0F}, theme.floor);
        drawSceneryCube(view, {0.0F, 0.012F, 11.0F}, 27.6F, 0.012F, 21.6F, theme.blueCourt);
        drawSceneryCube(view, {0.0F, 0.012F, -11.0F}, 27.6F, 0.012F, 21.6F, theme.orangeCourt);

        DrawCylinder({0.0F, 0.018F, 0.0F}, 5.8F, 5.8F, 0.018F, 40, withAlpha(theme.accent, 38));
        DrawCylinderWires({0.0F, 0.035F, 0.0F}, 5.8F, 5.8F, 0.02F, 40, withAlpha(theme.accent, 185));

        for (std::size_t index = 0; index < BoostPadPositions.size(); ++index) {
            const Vec3 padPosition = BoostPadPositions[index];
            const Vector3 pad = toRay(padPosition);
            const bool blueSide = pad.z > 0.0F;
            const bool large = largeBoostPad(index);
            const Color sideGlow = blueSide ? scaledColor(theme.blueCourt, 1.7F) : scaledColor(theme.orangeCourt, 1.7F);
            const Color glow = withAlpha(large ? GOLD : sideGlow, 220);
            const float radius = large ? 0.84F : 0.60F;
            const float timer = boostPadRespawnTimers[index];
            const float recharge = 1.0F - clamp(timer / boostPadRespawnDuration(index), 0.0F, 1.0F);
            DrawCylinder(pad, radius, radius, 0.035F, 16, Color{17, 25, 34, 210});
            if (timer <= 0.0F) {
                const float pulse = radius + 0.08F * std::sin(totalTime * 4.0F + padPosition.x + padPosition.z);
                DrawCylinder(pad, pulse, pulse, 0.04F, 16, withAlpha(glow, large ? 95 : 70));
                DrawCylinderWires(pad, pulse, pulse, 0.045F, 16, glow);
                DrawSphere({pad.x, large ? 0.27F : 0.18F + 0.06F * std::sin(totalTime * 5.0F + pad.z), pad.z},
                    large ? 0.21F : 0.11F, glow);
            } else {
                const float refillRadius = std::max(0.08F, radius * recharge);
                DrawCylinderWires(pad, refillRadius, refillRadius, 0.045F, 16, withAlpha(glow, 175));
                DrawLine3D(
                    {pad.x, 0.08F, pad.z},
                    {pad.x, 0.08F + (large ? 0.75F : 0.48F) * recharge, pad.z},
                    withAlpha(glow, 195));
            }
        }

        const Color lineColor{205, 230, 235, 200};
        drawSceneryCube(view, {-13.75F, 0.025F, 0.0F}, 0.08F, 0.05F, 43.4F, lineColor);
        drawSceneryCube(view, {13.75F, 0.025F, 0.0F}, 0.08F, 0.05F, 43.4F, lineColor);
        drawSceneryCube(view, {0.0F, 0.025F, -21.75F}, 27.5F, 0.05F, 0.08F, lineColor);
        drawSceneryCube(view, {0.0F, 0.025F, 21.75F}, 27.5F, 0.05F, 0.08F, lineColor);
        drawSceneryCube(view, {0.0F, 0.03F, 0.0F}, 27.5F, 0.06F, 0.1F, lineColor);

        for (int x = -12; x <= 12; x += 2) {
            DrawLine3D({static_cast<float>(x), 0.08F, -21.7F}, {static_cast<float>(x), 0.08F, 21.7F}, Color{91, 132, 144, 45});
        }
        for (int z = -20; z <= 20; z += 2) {
            DrawLine3D({-13.7F, 0.08F, static_cast<float>(z)}, {13.7F, 0.08F, static_cast<float>(z)}, Color{91, 132, 144, 45});
        }

        drawSceneryCube(view, {-13.55F, 1.45F, 0.0F}, 0.28F, 2.9F, 0.28F, theme.accent);
        drawSceneryCube(view, {13.55F, 1.45F, 0.0F}, 0.28F, 2.9F, 0.28F, theme.accent);
        drawSceneryCube(view, {0.0F, 2.56F, 0.0F}, 27.1F, 0.12F, 0.18F, theme.accent);
        for (int x = -13; x <= 13; ++x) {
            DrawLine3D({static_cast<float>(x), 0.1F, 0.0F}, {static_cast<float>(x), 2.52F, 0.0F}, Color{222, 235, 225, 160});
        }
        for (int row = 1; row <= 8; ++row) {
            const float y = 0.1F + static_cast<float>(row) * 0.29F;
            DrawLine3D({-13.5F, y, 0.0F}, {13.5F, y, 0.0F}, Color{222, 235, 225, 160});
        }

        const Color barrier = withAlpha(theme.cage, 65);
        drawSceneryCube(view, {-14.28F, 3.0F, 0.0F}, 0.35F, 6.0F, 44.0F, barrier);
        drawSceneryCube(view, {14.28F, 3.0F, 0.0F}, 0.35F, 6.0F, 44.0F, barrier);
        drawSceneryCube(view, {0.0F, 3.0F, -22.28F}, 28.0F, 6.0F, 0.35F, barrier);
        drawSceneryCube(view, {0.0F, 3.0F, 22.28F}, 28.0F, 6.0F, 0.35F, barrier);

        const Color cageLine = theme.cage;
        for (int z = -21; z <= 21; z += 3) {
            DrawLine3D({-14.08F, 0.1F, static_cast<float>(z)}, {-14.08F, 6.1F, static_cast<float>(z)}, cageLine);
            DrawLine3D({14.08F, 0.1F, static_cast<float>(z)}, {14.08F, 6.1F, static_cast<float>(z)}, cageLine);
        }
        for (int y = 1; y <= 6; ++y) {
            DrawLine3D({-14.08F, static_cast<float>(y), -22.0F}, {-14.08F, static_cast<float>(y), 22.0F}, cageLine);
            DrawLine3D({14.08F, static_cast<float>(y), -22.0F}, {14.08F, static_cast<float>(y), 22.0F}, cageLine);
        }

        drawSceneryCube(view, {-14.15F, 6.25F, 0.0F}, 0.25F, 0.18F, 44.0F, scaledColor(theme.blueCourt, 1.65F));
        drawSceneryCube(view, {14.15F, 6.25F, 0.0F}, 0.25F, 0.18F, 44.0F, scaledColor(theme.orangeCourt, 1.65F));

        if (state == MatchState::ServeCountdown && !scriptedAerialTest) {
            const Vec3 markerPosition = practiceMode() ? serveLandingTarget : servePosition;
            const Color landingColor = practiceMode()
                ? SKYBLUE
                : (servingTeam == 0 ? SKYBLUE : ORANGE);
            DrawCylinder(
                {markerPosition.x, 0.045F, markerPosition.z},
                1.45F,
                1.45F,
                0.025F,
                24,
                withAlpha(landingColor, 58));
            DrawCylinderWires(
                {markerPosition.x, 0.055F, markerPosition.z},
                1.45F,
                1.45F,
                0.04F,
                24,
                landingColor);
        }

        for (int side = -1; side <= 1; side += 2) {
            for (int index = -10; index <= 10; index += 2) {
                const Color lamp = (index / 2 + side) % 2 == 0
                    ? scaledColor(theme.blueCourt, 1.65F)
                    : scaledColor(theme.orangeCourt, 1.65F);
                drawSceneryCube(view, {static_cast<float>(index), 8.1F, static_cast<float>(side) * 22.35F}, 0.7F, 0.35F, 0.25F, lamp);
            }

            const Color endColor = side > 0 ? scaledColor(theme.blueCourt, 1.65F) : scaledColor(theme.orangeCourt, 1.65F);
            drawSceneryCube(view, {0.0F, 8.2F, static_cast<float>(side) * 22.7F}, 10.8F, 3.0F, 0.28F, Color{13, 21, 38, 255});
            drawSceneryCube(view, {0.0F, 9.62F, static_cast<float>(side) * 22.52F}, 10.8F, 0.16F, 0.18F, endColor);
            for (int bar = -4; bar <= 4; ++bar) {
                const float brightness = 0.55F + 0.45F * std::sin(totalTime * 2.2F + static_cast<float>(bar));
                drawSceneryCube(view, 
                    {static_cast<float>(bar) * 1.05F, 8.2F, static_cast<float>(side) * 22.5F},
                    0.62F,
                    0.2F + brightness * 0.32F,
                    0.14F,
                    withAlpha(endColor, static_cast<unsigned char>(130.0F + brightness * 120.0F)));
            }
        }

        drawEnvironmentProps(view);
    }

    void drawCarGeometry(
        Vector3 position,
        Quaternion rotation,
        int bodyStyle,
        Color paint,
        Color wheelColor,
        Color spoilerColor,
        Color decalColor,
        bool drawShadow,
        unsigned char opacity = 255) const {
        Vector3 axis{};
        float angle = 0.0F;
        QuaternionToAxisAngle(rotation, &axis, &angle);
        if (Vector3Length(axis) < 0.001F) {
            axis = {0.0F, 1.0F, 0.0F};
        }
        if (drawShadow) {
            DrawCylinder({position.x, 0.035F, position.z}, 1.12F, 1.12F, 0.025F, 16, Color{0, 0, 0, 90});
        }
        const BodyStyleChoice &style = BodyStyles[static_cast<std::size_t>(
            std::clamp(bodyStyle, 0, static_cast<int>(BodyStyles.size()) - 1))];
        DrawModelEx(cubeModel, position, axis, angle * RAD2DEG, style.bodyScale, withAlpha(paint, opacity));

        const Vector3 cabinOffset = Vector3RotateByQuaternion(style.cabinOffset, rotation);
        const Vector3 cabinPosition = Vector3Add(position, cabinOffset);
        DrawModelEx(cubeModel, cabinPosition, axis, angle * RAD2DEG, style.cabinScale,
            Color{30, 42, 60, opacity});

        const Vector3 hoodStripeOffset = Vector3RotateByQuaternion(
            {0.0F, style.bodyScale.y * 0.52F, 0.62F}, rotation);
        DrawModelEx(
            cubeModel,
            Vector3Add(position, hoodStripeOffset),
            axis,
            angle * RAD2DEG,
            {0.42F, 0.055F, 1.28F},
            withAlpha(decalColor, opacity));
        for (float side : {-1.0F, 1.0F}) {
            const Vector3 sideStripeOffset = Vector3RotateByQuaternion(
                {side * (style.bodyScale.x * 0.5F + 0.015F), 0.02F, 0.10F}, rotation);
            DrawModelEx(
                cubeModel,
                Vector3Add(position, sideStripeOffset),
                axis,
                angle * RAD2DEG,
                {0.055F, 0.25F, style.bodyScale.z * 0.68F},
                withAlpha(decalColor, opacity));
        }

        constexpr std::array<Vector3, 4> wheelOffsets{
            Vector3{-0.98F, -0.35F, -0.86F}, Vector3{0.98F, -0.35F, -0.86F},
            Vector3{-0.98F, -0.35F, 0.86F}, Vector3{0.98F, -0.35F, 0.86F}};
        for (Vector3 offset : wheelOffsets) {
            const Vector3 wheelPosition = Vector3Add(position, Vector3RotateByQuaternion(offset, rotation));
            DrawSphere(wheelPosition, 0.34F, withAlpha(wheelColor, opacity));
        }

        const Vector3 noseOffset = Vector3RotateByQuaternion({0.0F, 0.03F, 1.46F}, rotation);
        const Vector3 nosePosition = Vector3Add(position, noseOffset);
        DrawModelEx(cubeModel, nosePosition, axis, angle * RAD2DEG, {1.45F, 0.22F, 0.12F}, withAlpha(spoilerColor, opacity));

        for (float side : {-0.58F, 0.58F}) {
            const Vector3 strutOffset = Vector3RotateByQuaternion({side, 0.54F, -1.18F}, rotation);
            DrawModelEx(
                cubeModel,
                Vector3Add(position, strutOffset),
                axis,
                angle * RAD2DEG,
                {0.11F, 0.58F, 0.12F},
                withAlpha(spoilerColor, opacity));
        }
        const Vector3 wingOffset = Vector3RotateByQuaternion({0.0F, 0.84F, -1.36F}, rotation);
        DrawModelEx(
            cubeModel,
            Vector3Add(position, wingOffset),
            axis,
            angle * RAD2DEG,
            {1.82F, 0.15F, 0.4F},
            withAlpha(spoilerColor, opacity));
    }

    void drawCar(const Car &car) const {
        const Transform transform = physics.transform(car.body);
        drawCarGeometry(
            toRay(transform.position),
            toRay(transform.rotation),
            car.bodyStyle,
            car.paint,
            car.wheelColor,
            car.spoilerColor,
            car.decalColor,
            true);
    }

    void drawAcademyGhost() const {
        if ((state != MatchState::Playing && state != MatchState::ServeCountdown) || !academyGhostEnabled) {
            return;
        }
        Transform ghostTransform;
        if (!academyGhostTransform(ghostTransform)) {
            return;
        }
        const Vector3 position = toRay(ghostTransform.position);
        drawCarGeometry(
            position,
            toRay(ghostTransform.rotation),
            2,
            Color{61, 222, 255, 255},
            Color{170, 236, 255, 255},
            Color{221, 92, 255, 255},
            Color{245, 247, 250, 255},
            false,
            92);
        const float pulse = 0.22F + 0.05F * std::sin(totalTime * 7.0F);
        DrawSphere({position.x, position.y + 2.05F, position.z}, pulse, Color{115, 235, 255, 155});
        DrawLine3D(
            {position.x, position.y + 1.05F, position.z},
            {position.x, position.y + 1.85F, position.z},
            Color{115, 235, 255, 125});
    }

    void drawBallTransform(const Transform &transform) const {
        const Vector3 position = toRay(transform.position);
        const float shadowScale = clamp(1.0F - transform.position.y / 16.0F, 0.25F, 0.95F);
        DrawCylinder(
            {position.x, 0.045F, position.z},
            BallRadius * shadowScale,
            BallRadius * shadowScale,
            0.025F,
            16,
            Color{0, 0, 0, 105});
        Vector3 axis{};
        float angle = 0.0F;
        QuaternionToAxisAngle(toRay(transform.rotation), &axis, &angle);
        if (Vector3Length(axis) < 0.001F) axis = {0.0F, 1.0F, 0.0F};
        DrawModelEx(ballModel, position, axis, angle * RAD2DEG, {1.0F, 1.0F, 1.0F}, Color{250, 225, 85, 255});
        rlPushMatrix();
        rlTranslatef(position.x, position.y, position.z);
        rlRotatef(angle * RAD2DEG, axis.x, axis.y, axis.z);
        DrawSphereWires({}, BallRadius + 0.012F, 6, 8, Color{135, 57, 35, 220});
        rlPopMatrix();
    }

    void drawBall() const {
        drawBallTransform(physics.transform(ball));
        if (ballFreezeTimer > 0.0F) {
            const Vector3 position = toRay(physics.transform(ball).position);
            const float pulse = 0.08F + 0.08F * std::sin(totalTime * 12.0F);
            DrawSphereWires(position, BallRadius + 0.16F + pulse, 8, 16, powerupColor(Powerup::Freezer));
            DrawSphereWires(position, BallRadius + 0.34F + pulse, 6, 12,
                withAlpha(powerupColor(Powerup::Freezer), 150));
        }
    }

    void drawPowerupEffects() const {
        if (!powerVolleyActive()) {
            return;
        }
        const Vector3 ballPosition = toRay(physics.transform(ball).position);
        for (const Car &car : cars) {
            if (!isCarAvailable(car.slot)) {
                continue;
            }
            const Vector3 carPosition = toRay(physics.transform(car.body).position);
            if (car.heldPowerup != Powerup::None) {
                const float orbit = totalTime * 2.8F + static_cast<float>(car.slot);
                DrawSphere({carPosition.x + std::cos(orbit) * 0.65F, carPosition.y + 1.55F,
                    carPosition.z + std::sin(orbit) * 0.65F}, 0.16F, powerupColor(car.heldPowerup));
            }
            if (car.magnetTimer > 0.0F) {
                const Color magnetColor = powerupColor(Powerup::Magnetizer);
                DrawLine3D({carPosition.x, carPosition.y + 0.8F, carPosition.z}, ballPosition,
                    withAlpha(magnetColor, 205));
                const float ring = 1.25F + 0.18F * std::sin(totalTime * 9.0F);
                DrawCylinderWires({carPosition.x, 0.08F, carPosition.z}, ring, ring, 0.05F, 24, magnetColor);
            }
        }
    }

    void drawParticles() const {
        for (const Particle &particle : particles) {
            const float fraction = clamp(particle.life / particle.initialLife, 0.0F, 1.0F);
            DrawCubeV(
                toRay(particle.position),
                {particle.size * fraction, particle.size * fraction, particle.size * fraction},
                withAlpha(particle.color, static_cast<unsigned char>(255.0F * fraction)));
        }
    }

    void drawBallTrail() const {
        for (const TrailPoint &point : ballTrail) {
            const float fraction = clamp(point.life / point.initialLife, 0.0F, 1.0F);
            DrawSphere(
                toRay(point.position),
                point.size * (0.45F + fraction * 0.55F),
                withAlpha(point.color, static_cast<unsigned char>(fraction * 150.0F)));
        }
    }

    void drawLandingIndicator() const {
        if (state != MatchState::Playing || trainingResetTimer > 0.0F) {
            return;
        }
        if (academyActive && (academyLesson == AcademyLesson::BoostGates
                || academyLesson == AcademyLesson::DoubleJump)) {
            return;
        }
        if (!predictedFlight.landed || ballFreezeTimer > 0.0F) return;
        const float landingTime = ballLandingTime();
        const Vec3 landing = predictedFlight.landing();
        Color sideColor = landing.z >= 0.0F ? SKYBLUE : ORANGE;
        if ((gameMode == GameMode::TargetChallenge
                || (academyActive && academyLesson == AcademyLesson::TargetLanding))
            && trainingReturnSuccessful) {
            const TargetScore projection = scoreTargetLanding(landing, challengeTarget, true, challengeCombo);
            sideColor = targetGradeColor(projection.grade);
            DrawLine3D(
                {landing.x, 0.09F, landing.z},
                {challengeTarget.x, 0.09F, challengeTarget.z},
                withAlpha(sideColor, 185));
        }
        const float countdownRadius = 0.85F + clamp(landingTime, 0.0F, 2.5F) * 0.9F;
        DrawCylinderWires({landing.x, 0.075F, landing.z}, countdownRadius, countdownRadius, 0.025F, 32,
            withAlpha(landingTime < 0.55F ? RAYWHITE : sideColor, 230));
        DrawLine3D({landing.x - 1.2F, 0.08F, landing.z}, {landing.x + 1.2F, 0.08F, landing.z}, sideColor);
        DrawLine3D({landing.x, 0.08F, landing.z - 1.2F}, {landing.x, 0.08F, landing.z + 1.2F}, sideColor);
        const float pulse = 0.82F + 0.12F * std::sin(totalTime * (landingTime < 0.55F ? 16.0F : 6.0F));
        DrawCylinder({landing.x, 0.055F, landing.z}, pulse, pulse, 0.025F, 24, withAlpha(sideColor, 42));
        DrawCylinderWires({landing.x, 0.065F, landing.z}, pulse, pulse, 0.035F, 24, withAlpha(sideColor, 210));
        DrawLine3D(
            {landing.x, 0.08F, landing.z},
            {landing.x, 0.08F + clamp(landingTime, 0.2F, 2.4F) * 0.65F, landing.z},
            withAlpha(sideColor, 130));
    }

    void drawThreeVsThreeRoles() const {
        if (gameMode != GameMode::ThreeVsThree
            || (state != MatchState::Playing && state != MatchState::ServeCountdown)) {
            return;
        }
        const Vec3 landing = state == MatchState::ServeCountdown ? serveLandingTarget : predictedFlight.landing();
        const int striker = strikerForTeam(0, landing);
        const int support = supportForTeam(0, striker, landing);
        for (int index = 0; index < CarsPerTeam; ++index) {
            if (!isCarAvailable(index)) {
                continue;
            }
            const Vec3 position = physics.transform(cars[index].body).position;
            const Color roleColor = index == striker ? GOLD : (index == support ? SKYBLUE : Color{164, 184, 204, 255});
            const float radius = index == striker ? 1.16F : (index == support ? 0.92F : 0.72F);
            const float pulse = radius + 0.06F * std::sin(totalTime * 5.0F + static_cast<float>(index));
            DrawCylinderWires({position.x, 0.07F, position.z}, pulse, pulse, 0.035F, 18, withAlpha(roleColor, 205));
            DrawLine3D(
                {position.x, position.y + 1.0F, position.z},
                {position.x, position.y + (index == striker ? 2.15F : 1.65F), position.z},
                withAlpha(roleColor, 160));
        }
    }

    void drawChallengeTarget() const {
        if (gameMode != GameMode::TargetChallenge
            || (state != MatchState::Playing && state != MatchState::ServeCountdown)) {
            return;
        }
        const float pulse = 1.0F + 0.05F * std::sin(totalTime * 5.5F);
        DrawCylinder({challengeTarget.x, 0.055F, challengeTarget.z}, 5.2F, 5.2F, 0.025F, 32, Color{31, 190, 140, 28});
        DrawCylinderWires({challengeTarget.x, 0.07F, challengeTarget.z}, 5.2F, 5.2F, 0.035F, 32, Color{82, 225, 173, 165});
        DrawCylinder({challengeTarget.x, 0.065F, challengeTarget.z}, 3.2F, 3.2F, 0.025F, 32, Color{43, 199, 255, 38});
        DrawCylinderWires({challengeTarget.x, 0.08F, challengeTarget.z}, 3.2F, 3.2F, 0.04F, 32, SKYBLUE);
        DrawCylinder({challengeTarget.x, 0.075F, challengeTarget.z}, 1.6F * pulse, 1.6F * pulse, 0.03F, 32, Color{255, 202, 44, 78});
        DrawCylinderWires({challengeTarget.x, 0.095F, challengeTarget.z}, 1.6F * pulse, 1.6F * pulse, 0.045F, 32, GOLD);
        DrawLine3D(
            {challengeTarget.x, 0.1F, challengeTarget.z},
            {challengeTarget.x, 1.65F, challengeTarget.z},
            withAlpha(GOLD, 180));
    }

    void drawAcademyMarkers() const {
        if (!academyActive || (state != MatchState::Playing && state != MatchState::ServeCountdown)) {
            return;
        }
        const float pulse = 1.0F + 0.08F * std::sin(totalTime * 6.0F);
        if (academyLesson == AcademyLesson::BoostGates) {
            for (int index = 0; index < static_cast<int>(AcademyGatePositions.size()); ++index) {
                const Vec3 gate = AcademyGatePositions[static_cast<std::size_t>(index)];
                const bool complete = index < academyGateIndex;
                const bool active = index == academyGateIndex;
                const Color color = complete ? SKYBLUE : (active ? GOLD : Color{90, 112, 137, 180});
                const float radius = active ? 1.65F * pulse : 1.42F;
                DrawCylinder({gate.x, 0.06F, gate.z}, radius, radius, 0.035F, 28, withAlpha(color, active ? 65 : 28));
                DrawCylinderWires({gate.x, 0.08F, gate.z}, radius, radius, 0.05F, 28, color);
                DrawLine3D({gate.x, 0.1F, gate.z}, {gate.x, active ? 3.8F : 2.4F, gate.z}, withAlpha(color, 175));
                DrawSphere({gate.x, active ? 3.8F : 2.4F, gate.z}, active ? 0.23F : 0.15F, color);
            }
        } else if (academyLesson == AcademyLesson::DoubleJump) {
            const Vec3 launchZone{0.0F, 0.06F, 6.0F};
            DrawCylinder(toRay(launchZone), 2.4F, 2.4F, 0.035F, 28, Color{43, 199, 255, 38});
            DrawCylinderWires(toRay(launchZone), 2.4F, 2.4F, 0.05F, 28, SKYBLUE);
            DrawCylinderWires({0.0F, 5.5F, 6.0F}, 2.15F * pulse, 2.15F * pulse, 0.08F, 28, GOLD);
            DrawLine3D({-2.15F, 0.1F, 6.0F}, {-2.15F, 5.5F, 6.0F}, withAlpha(GOLD, 145));
            DrawLine3D({2.15F, 0.1F, 6.0F}, {2.15F, 5.5F, 6.0F}, withAlpha(GOLD, 145));
        } else if (academyLesson == AcademyLesson::AerialReturn) {
            DrawLine3D({-7.0F, 2.56F + BallRadius, 0.0F}, {7.0F, 2.56F + BallRadius, 0.0F}, GOLD);
            DrawSphere({0.0F, 2.56F + BallRadius, 0.0F}, 0.22F * pulse, GOLD);
        } else {
            DrawCylinder({challengeTarget.x, 0.055F, challengeTarget.z}, 5.2F, 5.2F, 0.025F, 32, Color{31, 190, 140, 30});
            DrawCylinderWires({challengeTarget.x, 0.07F, challengeTarget.z}, 5.2F, 5.2F, 0.035F, 32, Color{82, 225, 173, 180});
            DrawCylinder({challengeTarget.x, 0.065F, challengeTarget.z}, 2.2F * pulse, 2.2F * pulse, 0.03F, 32, Color{255, 202, 44, 62});
            DrawCylinderWires({challengeTarget.x, 0.09F, challengeTarget.z}, 2.2F * pulse, 2.2F * pulse, 0.045F, 32, GOLD);
            DrawLine3D({challengeTarget.x, 0.1F, challengeTarget.z}, {challengeTarget.x, 2.4F, challengeTarget.z}, withAlpha(GOLD, 185));
        }
    }

    void drawBackdrop() const {
        const ArenaTheme &theme = activeArena();
        DrawRectangleGradientV(0, 0, ScreenWidth, ScreenHeight, theme.skyTop, theme.skyBottom);
        const Color sunColor = scaledColor(theme.accent, 1.08F);
        if (activeArenaIndex == 0) {
            DrawRectangle(1035, 70, 72, 72, sunColor);
            for (int index = 0; index < 20; ++index) {
                const int x = index * 72 - 30;
                const int height = 70 + (index * 31) % 85;
                DrawRectangle(x + 20, ScreenHeight - height - 34, 16, height, Color{35, 74, 47, 230});
                DrawRectangle(x, ScreenHeight - height - 52, 58, 45, Color{29, 96, 48, 235});
            }
        } else if (activeArenaIndex == 1) {
            DrawRectangle(1060, 64, 92, 92, sunColor);
            DrawRectangle(0, 292, ScreenWidth, ScreenHeight - 292, Color{29, 132, 174, 105});
            for (int wave = 0; wave < 10; ++wave) {
                const int y = 314 + wave * 24;
                DrawRectangle((wave % 2) * 45, y, ScreenWidth - 90, 3, withAlpha(RAYWHITE, 55));
            }
        } else if (activeArenaIndex == 2) {
            DrawRectangle(1028, 68, 72, 72, Color{218, 228, 235, 255});
            for (int index = 0; index < 22; ++index) {
                const int width = 38 + (index * 17) % 42;
                const int height = 68 + (index * 29) % 145;
                const int x = index * 63 - 25;
                DrawRectangle(x, ScreenHeight - height - 35, width, height, Color{10, 17, 33, 235});
                if (index % 2 == 0) {
                    DrawRectangle(x + 9, ScreenHeight - height - 18, 6, 6,
                        index % 4 == 0 ? theme.accent : Color{79, 151, 222, 255});
                }
            }
        } else {
            DrawRectangle(1034, 65, 88, 88, sunColor);
            DrawRectangle(0, 350, 235, 210, Color{105, 50, 33, 220});
            DrawRectangle(70, 300, 110, 70, Color{105, 50, 33, 220});
            DrawRectangle(1000, 330, 280, 240, Color{118, 54, 31, 225});
            DrawRectangle(1060, 274, 150, 76, Color{118, 54, 31, 225});
        }
    }

    void drawWorld(const Camera3D &viewCamera, int playerSlot = 0) const {
        BeginMode3D(viewCamera);
        Vec3 focus = physics.transform(cars[playerSlot].body).position;
        Vec3 focusBall = physics.transform(ball).position;
        if (state == MatchState::GoalReplay && !replayFrames.empty()) {
            const auto frame = sampledReplayFrame();
            focus = frame.cars[playerSlot].position;
            focusBall = frame.ball.position;
        }
        drawArena({viewCamera.position, {focus.x, focus.y + 1.0F, focus.z}, toRay(focusBall),
            state != MatchState::Title && state != MatchState::Loading});
        const ReplayFrame sampled = state == MatchState::GoalReplay ? sampledReplayFrame() : ReplayFrame{};
        const ReplayFrame *replay = state == MatchState::GoalReplay && !replayFrames.empty() ? &sampled : nullptr;
        if (replay != nullptr) {
            for (std::size_t index = 0; index < cars.size(); ++index) {
                if ((replay->visibleCars & (1U << index)) == 0) {
                    continue;
                }
                drawCarGeometry(
                    toRay(replay->cars[index].position),
                    toRay(replay->cars[index].rotation),
                    cars[index].bodyStyle,
                    cars[index].paint,
                    cars[index].wheelColor,
                    cars[index].spoilerColor,
                    cars[index].decalColor,
                    true);
            }
            drawBallTransform(replay->ball);
        } else {
            drawLandingIndicator();
            drawBallTrail();
            drawThreeVsThreeRoles();
            drawChallengeTarget();
            drawAcademyMarkers();
            drawAcademyGhost();
            for (int index = 0; index < static_cast<int>(cars.size()); ++index) {
                if (isCarAvailable(index)) {
                    drawCar(cars[index]);
                }
            }
            drawBall();
            drawPowerupEffects();
        }
        if (replay == nullptr) drawParticles();
        EndMode3D();
    }

    void drawWorld() const {
        drawWorld(camera);
    }

    void drawLocalPlayerFocus(int slot) const {
        const Car &car = cars[static_cast<std::size_t>(slot)];
        if (car.respawnTimer > 0.0F) {
            return;
        }
        const Transform transform = physics.transform(car.body);
        const Vector3 position = toRay(transform.position);
        const Color playerColor = slot == 0 ? SKYBLUE : GOLD;
        const float pulse = 0.5F + 0.5F * std::sin(totalTime * 5.0F + static_cast<float>(slot) * Pi);

        BeginMode3D(localCoopCameras[static_cast<std::size_t>(slot)]);
        rlDisableDepthTest();
        drawCarGeometry(
            position,
            toRay(transform.rotation),
            car.bodyStyle,
            car.paint,
            car.wheelColor,
            car.spoilerColor,
            car.decalColor,
            false);
        DrawCylinderWires(
            {position.x, 0.07F, position.z},
            1.5F + pulse * 0.16F,
            1.5F + pulse * 0.16F,
            0.04F,
            24,
            withAlpha(playerColor, 205));

        Vector3 axis{};
        float angle = 0.0F;
        const Quaternion rotation = toRay(transform.rotation);
        QuaternionToAxisAngle(rotation, &axis, &angle);
        if (Vector3Length(axis) < 0.001F) {
            axis = {0.0F, 1.0F, 0.0F};
        }
        DrawModelWiresEx(
            cubeModel,
            position,
            axis,
            angle * RAD2DEG,
            {1.92F, 0.98F, 2.92F},
            withAlpha(playerColor, 225));

        const float markerY = position.y + 2.35F + pulse * 0.18F;
        const Vector3 left{position.x - 0.42F, markerY + 0.34F, position.z};
        const Vector3 right{position.x + 0.42F, markerY + 0.34F, position.z};
        const Vector3 tip{position.x, markerY - 0.18F, position.z};
        DrawTriangle3D(left, tip, right, playerColor);
        DrawTriangle3D(right, tip, left, playerColor);
        rlEnableDepthTest();
        EndMode3D();
    }

    void drawBallLocator(const Camera3D &view, int width, int height) const {
        if (state != MatchState::Playing || showHelp || trainingResetTimer > 0.0F) return;
        if (academyActive && (academyLesson == AcademyLesson::BoostGates || academyLesson == AcademyLesson::DoubleJump)) return;
        const Vector3 ballPosition = toRay(physics.transform(ball).position);
        const Vector3 forward = Vector3Normalize(Vector3Subtract(view.target, view.position));
        const Vector3 relative = Vector3Subtract(ballPosition, view.position);
        const float depth = Vector3DotProduct(relative, forward);
        const Vector2 projected = GetWorldToScreenEx(ballPosition, view, width, height);
        const float rightEdge = static_cast<float>(width - 40);
        const float bottomEdge = static_cast<float>(height - 105);
        if (depth > 0.0F && projected.x > 25.0F && projected.x < static_cast<float>(width - 25)
            && projected.y > 105.0F && projected.y < bottomEdge) return;
        const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, view.up));
        const Vector3 up = Vector3CrossProduct(right, forward);
        Vector2 direction{Vector3DotProduct(relative, right), -Vector3DotProduct(relative, up)};
        if (std::abs(direction.x) + std::abs(direction.y) < 0.01F) direction = {0.0F, 1.0F};
        direction = Vector2Normalize(direction);
        const Vector2 center{static_cast<float>(width) * 0.5F, (125.0F + bottomEdge) * 0.5F};
        const float scale = std::min((rightEdge - center.x) / std::max(0.001F, std::abs(direction.x)),
            (bottomEdge - center.y) / std::max(0.001F, std::abs(direction.y)));
        const Vector2 point = Vector2Add(center, Vector2Scale(direction, scale));
        const Vector2 perpendicular{-direction.y, direction.x};
        DrawCircleV(point, 19.0F, Color{7, 12, 22, 230});
        DrawTriangle(Vector2Add(point, Vector2Scale(direction, 13.0F)),
            Vector2Add(Vector2Subtract(point, Vector2Scale(direction, 8.0F)), Vector2Scale(perpendicular, -8.0F)),
            Vector2Add(Vector2Subtract(point, Vector2Scale(direction, 8.0F)), Vector2Scale(perpendicular, 8.0F)), GOLD);
        DrawText("BALL", static_cast<int>(point.x) - 16, static_cast<int>(point.y) + 22, 13, GOLD);
    }

    void renderLocalCoopViews() const {
        const ArenaTheme &theme = activeArena();
        for (std::size_t slot = 0; slot < localCoopTargets.size(); ++slot) {
            BeginTextureMode(localCoopTargets[slot]);
            ClearBackground(theme.skyTop);
            DrawRectangleGradientV(0, 0, ScreenWidth / 2, ScreenHeight, theme.skyTop, theme.skyBottom);
            DrawCircleGradient(
                {static_cast<float>(slot == 0 ? 98 : ScreenWidth / 2 - 98), 104.0F},
                72.0F,
                withAlpha(slot == 0 ? SKYBLUE : GOLD, 70),
                withAlpha(theme.skyTop, 0));
            drawWorld(localCoopCameras[slot], static_cast<int>(slot));
            drawLocalPlayerFocus(static_cast<int>(slot));
            drawBallLocator(localCoopCameras[slot], ScreenWidth / 2, ScreenHeight);

            const Vec3 playerPosition = physics.transform(cars[slot].body).position;
            Vector2 badgePosition = GetWorldToScreenEx(
                toRay(Vec3{playerPosition.x, playerPosition.y + 3.15F, playerPosition.z}),
                localCoopCameras[slot],
                ScreenWidth / 2,
                ScreenHeight);
            badgePosition.x = clamp(badgePosition.x, 30.0F, static_cast<float>(ScreenWidth / 2 - 30));
            badgePosition.y = clamp(badgePosition.y, 112.0F, static_cast<float>(ScreenHeight - 58));
            const Color badgeColor = slot == 0 ? SKYBLUE : GOLD;
            DrawCircleV(badgePosition, 17.0F, Color{5, 9, 18, 205});
            DrawCircleLines(static_cast<int>(badgePosition.x), static_cast<int>(badgePosition.y), 17.0F, badgeColor);
            const char *badgeText = slot == 0 ? "P1" : "P2";
            DrawText(badgeText, static_cast<int>(badgePosition.x) - MeasureText(badgeText, 14) / 2,
                static_cast<int>(badgePosition.y) - 7, 14, badgeColor);
            EndTextureMode();
        }
    }

    void drawLocalCoopViews() const {
        for (std::size_t slot = 0; slot < localCoopTargets.size(); ++slot) {
            const RenderTexture2D &target = localCoopTargets[slot];
            DrawTexturePro(
                target.texture,
                {0.0F, 0.0F, static_cast<float>(target.texture.width), -static_cast<float>(target.texture.height)},
                {static_cast<float>(slot * (ScreenWidth / 2)), 0.0F,
                    static_cast<float>(ScreenWidth / 2), static_cast<float>(ScreenHeight)},
                {},
                0.0F,
                WHITE);
        }
        DrawRectangle(ScreenWidth / 2 - 3, 82, 6, ScreenHeight - 82, Color{5, 10, 18, 235});
    }

    void drawLocalCoopLabels() const {
        if (showHelp || (state != MatchState::Playing && state != MatchState::ServeCountdown)) {
            return;
        }
        DrawRectangle(18, 188, 146, 27, Color{7, 12, 22, 210});
        DrawRectangle(18, 188, 5, 27, SKYBLUE);
        DrawText(cameraMode == CameraMode::Ball ? "P1 / BALL CAM" : "P1 / CAR CAM", 31, 195, 14, RAYWHITE);
        DrawRectangle(ScreenWidth / 2 + 18, 188, 154, 27, Color{7, 12, 22, 210});
        DrawRectangle(ScreenWidth / 2 + 18, 188, 5, 27, GOLD);
        DrawText("P2 / GAMEPAD", ScreenWidth / 2 + 31, 195, 14,
            IsGamepadAvailable(0) ? RAYWHITE : ORANGE);
    }

    void drawTacticalRadar() const {
        if (gameMode != GameMode::ThreeVsThree
            || (state != MatchState::Playing && state != MatchState::ServeCountdown)) {
            return;
        }
        constexpr int radarX = ScreenWidth - 174;
        constexpr int radarY = 166;
        constexpr int radarWidth = 142;
        constexpr int radarHeight = 218;
        DrawRectangle(radarX - 8, radarY - 30, radarWidth + 16, radarHeight + 38, Color{7, 12, 22, 214});
        DrawRectangle(radarX - 8, radarY - 30, 5, radarHeight + 38, GOLD);
        DrawText("ROTATION RADAR", radarX, radarY - 23, 14, Color{190, 208, 224, 255});
        DrawRectangle(radarX, radarY, radarWidth, radarHeight, Color{20, 39, 50, 205});
        DrawRectangleLines(radarX, radarY, radarWidth, radarHeight, Color{152, 188, 201, 190});
        DrawLine(radarX, radarY + radarHeight / 2, radarX + radarWidth, radarY + radarHeight / 2, GOLD);

        const auto radarPoint = [&](Vec3 position) {
            return Vector2{
                static_cast<float>(radarX + radarWidth / 2) + position.x / ArenaHalfWidth * static_cast<float>(radarWidth / 2 - 5),
                static_cast<float>(radarY + radarHeight / 2) + position.z / ArenaHalfLength * static_cast<float>(radarHeight / 2 - 5)};
        };
        const Vec3 landing = state == MatchState::ServeCountdown ? serveLandingTarget : predictedFlight.landing();
        const int striker = strikerForTeam(0, landing);
        const int support = supportForTeam(0, striker, landing);
        for (std::size_t index = 0; index < BoostPadPositions.size(); ++index) {
            const Vector2 point = radarPoint(BoostPadPositions[index]);
            const bool active = boostPadRespawnTimers[index] <= 0.0F;
            const Color padColor = active
                ? (largeBoostPad(index) ? GOLD : Color{111, 225, 212, 255})
                : Color{70, 82, 94, 190};
            DrawCircleV(point, largeBoostPad(index) ? 2.8F : 1.8F, padColor);
        }
        for (int index = 0; index < static_cast<int>(cars.size()); ++index) {
            if (!isCarAvailable(index)) {
                continue;
            }
            const Vector2 point = radarPoint(physics.transform(cars[index].body).position);
            const Color teamColor = cars[index].team == 0 ? SKYBLUE : ORANGE;
            const float radius = index == 0 ? 6.0F : 4.5F;
            DrawCircleV(point, radius, teamColor);
            if (index == striker) {
                DrawCircleLines(static_cast<int>(point.x), static_cast<int>(point.y), 8.0F, GOLD);
            } else if (index == support) {
                DrawCircleLines(static_cast<int>(point.x), static_cast<int>(point.y), 7.0F, RAYWHITE);
            }
        }
        const Vector2 ballPoint = radarPoint(physics.transform(ball).position);
        DrawRectangle(static_cast<int>(ballPoint.x) - 3, static_cast<int>(ballPoint.y) - 3, 7, 7, GOLD);
    }

    void drawHud() const {
        DrawRectangle(0, 0, ScreenWidth, 82, Color{9, 14, 24, 230});
        DrawRectangle(ScreenWidth / 2 - 142, 12, 284, 58, Color{24, 34, 52, 245});
        DrawRectangle(ScreenWidth / 2 - 142, 12, 7, 58, SKYBLUE);
        DrawRectangle(ScreenWidth / 2 + 135, 12, 7, 58, ORANGE);
        if (gameMode == GameMode::Training) {
            drawCentered(academyActive ? "ROCKET ACADEMY" : "TRAINING", 28, academyActive ? 23 : 27, GOLD);
        } else if (gameMode == GameMode::TargetChallenge) {
            drawCentered("TARGET RUN", 28, 27, GOLD);
        } else {
            DrawText(TextFormat("%d", score[0]), ScreenWidth / 2 - 92, 17, 42, SKYBLUE);
            DrawText("-", ScreenWidth / 2 - 7, 20, 36, LIGHTGRAY);
            DrawText(TextFormat("%d", score[1]), ScreenWidth / 2 + 66, 17, 42, ORANGE);
        }

        std::string timerText;
        if (gameMode == GameMode::Training) {
            timerText = academyActive
                ? TextFormat("LESSON %d / %d", academyLessonIndex() + 1, AcademyLessonCount)
                : (trainingRepeatShot ? "SHOT DRILL" : "FREE PLAY");
        } else if (gameMode == GameMode::TargetChallenge) {
            timerText = TextFormat("SHOT %d / %d", std::min(trainingAttempts, TargetChallengeShots), TargetChallengeShots);
        } else if (overtime) {
            timerText = "OVERTIME";
        } else if (matchTime <= 0.0F) {
            timerText = "FINAL RALLY";
        } else {
            const int seconds = static_cast<int>(std::ceil(matchTime));
            timerText = TextFormat("%d:%02d", seconds / 60, seconds % 60);
        }
        drawCentered(timerText, 88, 26, overtime ? GOLD : RAYWHITE);
        if (state == MatchState::Playing && gameMode == GameMode::TargetChallenge) {
            DrawRectangle(ScreenWidth / 2 - 92, 121, 184, 30, Color{9, 14, 24, 210});
            drawCentered(TextFormat("COMBO  x%d", challengeCombo), 126, 18, challengeCombo > 1 ? GOLD : RAYWHITE);
        } else if (state == MatchState::Playing && rallyTouches > 0) {
            DrawRectangle(ScreenWidth / 2 - 76, 121, 152, 30, Color{9, 14, 24, 210});
            drawCentered(TextFormat("RALLY  %d", rallyTouches), 126, 18, GOLD);
        }
        if (state == MatchState::Playing || (state == MatchState::ServeCountdown && gameMode == GameMode::Training && !academyActive)) {
            if (academyActive) {
                DrawRectangle(24, 116, 470, 54, Color{9, 14, 24, 220});
                DrawRectangle(24, 116, 6, 54, SKYBLUE);
                DrawText(academyLessonName(), 40, 123, 17, GOLD);
                const std::string instruction = academyLessonInstruction();
                DrawText(instruction.c_str(), 40, 145, fittedFontSize(instruction, 438, 15, 12), RAYWHITE);
                DrawRectangle(ScreenWidth - 254, 116, 230, 54, Color{9, 14, 24, 220});
                DrawRectangle(ScreenWidth - 30, 116, 6, 54, ORANGE);
                DrawText(TextFormat("TIME  %05.2f", academyRunTimer), ScreenWidth - 238, 123, 17, RAYWHITE);
                DrawText(TextFormat("RETRIES  %d", academyRetries), ScreenWidth - 238, 145, 14,
                    academyRetries == 0 ? SKYBLUE : ORANGE);
            } else if (gameMode == GameMode::Training) {
                DrawRectangle(24, 116, 348, 70, Color{9, 14, 24, 220});
                const std::string feedLabel = std::string("FEED ")
                    + (trainingRepeatShot ? currentTrainingFeedName() : trainingFeedName()) + "  [TAB / D-UP]";
                DrawText(feedLabel.c_str(), 34, 123, fittedFontSize(feedLabel, 326, 15, 12), SKYBLUE);
                DrawText(trainingRepeatShot ? "L / D-RIGHT: UNLOCK SHOT" : "L / D-RIGHT: LOCK SHOT", 34, 145, 14,
                    trainingRepeatShot ? GOLD : RAYWHITE);
                const std::string retryLabel = keyName(boundKey(BindAction::Restart)) + " / D-DOWN: RETRY SAME SHOT";
                DrawText(retryLabel.c_str(), 34, 166, fittedFontSize(retryLabel, 326, 14, 12), RAYWHITE);
                DrawRectangle(ScreenWidth - 334, 116, 310, 54, Color{9, 14, 24, 220});
                const int accuracy = trainingCompleted > 0 ? static_cast<int>(100.0 * trainingReturns / trainingCompleted) : 0;
                const std::string returnsLabel = TextFormat("RETURNS %d / %d  (%d%%)", trainingReturns, trainingCompleted, accuracy);
                DrawText(returnsLabel.c_str(), ScreenWidth - 321, 123, fittedFontSize(returnsLabel, 284, 15, 12),
                    trainingReturns > 0 ? GOLD : RAYWHITE);
                DrawText(TextFormat("STREAK %d  /  BEST %d", trainingStreak, trainingBestStreak),
                    ScreenWidth - 321, 146, 14, SKYBLUE);
            } else if (gameMode == GameMode::TargetChallenge) {
                DrawRectangle(24, 116, 250, 30, Color{9, 14, 24, 210});
                const std::string targetFeed = std::string(currentTrainingFeedName()) + " FEED  /  TARGET ACTIVE";
                DrawText(targetFeed.c_str(), 34, 123, fittedFontSize(targetFeed, 228, 15, 12), SKYBLUE);
                DrawRectangle(ScreenWidth - 210, 116, 186, 30, Color{9, 14, 24, 210});
                DrawText(TextFormat("SCORE %04d", challengeScore), ScreenWidth - 194, 123, 17, GOLD);
            } else {
                DrawRectangle(24, 116, 166, 30, Color{9, 14, 24, 210});
                DrawText(TextFormat("BLUE TOUCHES %d/3", teamTouches[0]), 34, 123, 15,
                    teamTouches[0] == 3 ? GOLD : SKYBLUE);
                DrawRectangle(ScreenWidth - 190, 116, 166, 30, Color{9, 14, 24, 210});
                DrawText(TextFormat("ORANGE %d/3", teamTouches[1]), ScreenWidth - 177, 123, 15,
                    teamTouches[1] == 3 ? GOLD : ORANGE);
            }
        }
        if (state == MatchState::Playing && competitiveMode() && gameMode != GameMode::LocalCoop) {
            std::string teamBoost = "TEAM BOOST";
            for (int index = 1; index < activeCarsPerTeam(); ++index) {
                teamBoost += TextFormat("   P%d %03d", index + 1,
                    static_cast<int>(std::ceil(cars[index].boost)));
            }
            DrawRectangle(24, 153, gameMode == GameMode::ThreeVsThree ? 286 : 210, 28, Color{9, 14, 24, 205});
            DrawRectangle(24, 153, 5, 28, SKYBLUE);
            DrawText(teamBoost.c_str(), 38, 160, 14, Color{190, 211, 226, 255});
        }
        if (state == MatchState::Playing && gameMode == GameMode::TargetChallenge
            && trainingReturnSuccessful && !trainingBallHasTouchedGround) {
            const Vec3 projectedLanding = predictedFlight.landing();
            const TargetScore projection = scoreTargetLanding(projectedLanding, challengeTarget, true, challengeCombo);
            const Color gradeColor = targetGradeColor(projection.grade);
            DrawRectangle(24, 153, 326, 30, Color{9, 14, 24, 218});
            DrawRectangle(24, 153, 5, 30, gradeColor);
            DrawText(TextFormat("PROJECTED %s  %.1fm  +%d", targetGradeName(projection.grade),
                projection.distance, projection.awardedPoints), 38, 160, 15, gradeColor);
        }

        DrawText(gameMode == GameMode::Training ? (academyActive ? "ACADEMY" : "SOLO") : (gameMode == GameMode::TargetChallenge ? "TARGET" : (gameMode == GameMode::ThreeVsThree ? "BLUE 3" : "BLUE")),
            24, 18, 22, SKYBLUE);
        const std::string rightHeader = gameMode == GameMode::Training
            ? (academyActive ? std::string("BEST ") + academyMedalName(academyBestMedal) : "NO SCORE")
            : (gameMode == GameMode::TargetChallenge ? TextFormat("BEST %04d", challengeBestScore)
                : (gameMode == GameMode::ThreeVsThree ? "ORANGE 3" : "ORANGE"));
        DrawText(rightHeader.c_str(), ScreenWidth - MeasureText(rightHeader.c_str(), 22) - 24, 18, 22,
            gameMode == GameMode::TargetChallenge || academyActive ? academyMedalColor(academyBestMedal) : ORANGE);
        if (arcadeCupActive) {
            DrawText(TextFormat("CUP %d/3  /  %s", arcadeCupRound + 1, arcadeCupRoundName()),
                24, 50, 16, GOLD);
            const std::string cupFormat = std::string(difficulty == Difficulty::Pro ? "PRO" : "ROOKIE")
                + "  /  FIRST " + std::to_string(scoreLimit);
            DrawText(cupFormat.c_str(), ScreenWidth - MeasureText(cupFormat.c_str(), 16) - 24,
                50, 16, Color{180, 196, 215, 255});
        } else {
            const std::string difficultyLabel = academyActive
                ? std::string("TAB SKIP  /  G GHOST ") + (academyGhostEnabled ? "ON" : "OFF")
                : (practiceMode()
                    ? (difficulty == Difficulty::Pro ? "FEED: PRO [2]" : "FEED: ROOKIE [1]")
                    : (difficulty == Difficulty::Pro ? "AI: PRO [2]" : "AI: ROOKIE [1]"));
            DrawText(difficultyLabel.c_str(), 24, 50, 16, Color{180, 196, 215, 255});
            DrawText(
                difficulty == Difficulty::Pro ? "BALL SPEED: REGULAR" : "BALL SPEED: LEARNING",
                ScreenWidth - 224,
                50,
                16,
                Color{180, 196, 215, 255});
        }
        DrawText(activeArena().name, 24, 91, 15, withAlpha(activeArena().accent, 225));
        if (academyActive && state == MatchState::Playing && !academyBestGhost.empty()) {
            const bool splitAvailable = academyLastSplitDelta != 0.0F || academyTransitionTimer > 0.0F;
            const std::string ghostStatus = splitAvailable
                ? TextFormat("PB SPLIT  %+.2fs", academyLastSplitDelta)
                : std::string("RACING PB GHOST  /  G ") + (academyGhostEnabled ? "ON" : "OFF");
            const Color ghostColor = splitAvailable
                ? (academyLastSplitDelta <= 0.0F ? SKYBLUE : ORANGE)
                : Color{115, 235, 255, 255};
            DrawRectangle(ScreenWidth / 2 - 118, 164, 236, 30, Color{9, 14, 24, 218});
            drawCentered(ghostStatus, 171, 15, ghostColor);
        }
        if (powerVolleyActive()) {
            const char *mutatorLabel = "POWER VOLLEY";
            DrawText(mutatorLabel, ScreenWidth - MeasureText(mutatorLabel, 15) - 24, 91, 15,
                Color{215, 95, 255, 255});
        }

        constexpr int meterWidth = 220;
        const int meterX = 26;
        const int meterY = ScreenHeight - 48;
        DrawRectangle(meterX, meterY, meterWidth, 18, Color{12, 18, 28, 220});
        DrawRectangle(meterX + 3, meterY + 3, static_cast<int>((meterWidth - 6) * cars[0].boost / 100.0F), 12, GOLD);
        DrawText(gameMode == GameMode::LocalCoop ? "P1 BOOST" : "BOOST", meterX, meterY - 21, 16, RAYWHITE);
        DrawText(TextFormat("%03d", static_cast<int>(std::ceil(cars[0].boost))), meterX + meterWidth - 38, meterY - 21, 15,
            cars[0].boost < 20.0F ? ORANGE : GOLD);
        if (competitiveMode() && difficulty == Difficulty::Pro && cars[0].boost < 20.0F) {
            DrawText("SEEK AN ACTIVE BOOST PAD", meterX, meterY + 22, 13, ORANGE);
        }
        if (gameMode == GameMode::LocalCoop) {
            const int playerTwoMeterX = ScreenWidth - meterWidth - 26;
            DrawRectangle(playerTwoMeterX, meterY, meterWidth, 18, Color{12, 18, 28, 220});
            DrawRectangle(playerTwoMeterX + 3, meterY + 3,
                static_cast<int>((meterWidth - 6) * cars[1].boost / 100.0F), 12, SKYBLUE);
            DrawText("P2 BOOST", playerTwoMeterX, meterY - 21, 16, RAYWHITE);
            DrawText(TextFormat("%03d", static_cast<int>(std::ceil(cars[1].boost))),
                playerTwoMeterX + meterWidth - 38, meterY - 21, 15,
                cars[1].boost < 20.0F ? ORANGE : SKYBLUE);
        }
        if (powerVolleyActive()) {
            const auto drawPowerupHud = [&](const Car &car, int x, const char *prompt) {
                const bool ready = car.heldPowerup != Powerup::None;
                const Color color = ready ? powerupColor(car.heldPowerup) : Color{116, 138, 160, 255};
                DrawRectangle(x, ScreenHeight - 145, meterWidth, 44, Color{8, 13, 23, 225});
                DrawRectangle(x, ScreenHeight - 145, 5, 44, color);
                DrawText(ready ? powerupName(car.heldPowerup) : "POWER CHARGING", x + 14,
                    ScreenHeight - 137, 15, ready ? color : RAYWHITE);
                const std::string status = ready
                    ? std::string("USE  [") + prompt + "]"
                    : TextFormat("READY IN  %02d", static_cast<int>(std::ceil(car.powerupGrantTimer)));
                DrawText(status.c_str(), x + 14, ScreenHeight - 118, 13, Color{180, 199, 216, 255});
            };
            if (gameMode == GameMode::LocalCoop) {
                drawPowerupHud(cars[0], meterX, keyName(boundKey(BindAction::Powerup)).c_str());
                drawPowerupHud(cars[1], ScreenWidth - meterWidth - 26, "LB");
            } else {
                drawPowerupHud(cars[0], ScreenWidth - meterWidth - 26, keyName(boundKey(BindAction::Powerup)).c_str());
            }
        }
        const Transform playerTransform = physics.transform(cars[0].body);
        const Vec3 playerVelocity = physics.linearVelocity(cars[0].body);
        const bool demolitionReady = cars[0].respawnTimer <= 0.0F
            && length2D(playerVelocity) >= DemolitionSpeedThreshold;
        DrawText(
            TextFormat("SPEED %03d", static_cast<int>(length(playerVelocity) * 11.0F)),
            meterX + meterWidth + 22,
            meterY - 1,
            17,
            demolitionReady ? ORANGE : Color{188, 210, 226, 255});
        if (demolitionReady && competitiveMode()) {
            DrawText("SUPERSONIC  /  DEMO READY", meterX + meterWidth + 22, meterY - 23, 14, GOLD);
        }
        if (playerTransform.position.y > 1.05F) {
            const int jumpsRemaining = std::max(0, 2 - cars[0].jumpsUsed);
            DrawRectangle(ScreenWidth / 2 - 196, ScreenHeight - 46, 392, 30, Color{7, 12, 22, 208});
            const std::string airPrompt = TextFormat(
                "AIR  %d JUMP%s LEFT   %s: NOSE UP   %s: BOOST",
                jumpsRemaining,
                jumpsRemaining == 1 ? "" : "S",
                keyName(boundKey(BindAction::Reverse)).c_str(),
                keyName(boundKey(BindAction::Boost)).c_str());
            drawCentered(
                airPrompt,
                ScreenHeight - 39,
                15,
                jumpsRemaining > 0 ? SKYBLUE : GOLD);
        }
        if (state == MatchState::Playing
            && (gameMode == GameMode::Match || gameMode == GameMode::ThreeVsThree)) {
            const Vec3 landingTarget = predictedFlight.landing();
            const int striker = strikerForTeam(0, landingTarget);
            if (gameMode == GameMode::ThreeVsThree) {
                const int support = supportForTeam(0, striker, landingTarget);
                const char *role = striker == 0 ? "YOU: STRIKER" : (support == 0 ? "YOU: SETTER" : "YOU: BACK COVER");
                DrawText(role, ScreenWidth - 204, ScreenHeight - 58, 16,
                    striker == 0 ? GOLD : (support == 0 ? SKYBLUE : Color{180, 196, 215, 255}));
            } else {
                const bool partnerChasing = landingTarget.z > 0.0F && striker == 1;
                DrawText(
                    receivePending && receivingTeam == 0
                        ? (striker == 0 ? "YOUR FIRST TOUCH" : "TEAMMATE FIRST TOUCH")
                        : (cars[1].aiYielding ? "TEAMMATE: GIVING ROOM" : (partnerChasing ? "TEAMMATE: TAKING BALL" : "TEAMMATE: COVERING")),
                    ScreenWidth - 232,
                    ScreenHeight - 58,
                    16,
                    partnerChasing ? SKYBLUE : Color{180, 196, 215, 255});
            }
        } else if (state == MatchState::Playing && gameMode == GameMode::LocalCoop) {
            DrawText(
                IsGamepadAvailable(0) ? "P2 GAMEPAD: CONNECTED" : "P2 GAMEPAD: CONNECT ONE",
                ScreenWidth - 252,
                ScreenHeight - 176,
                16,
                IsGamepadAvailable(0) ? SKYBLUE : GOLD);
        }
        const CameraMode shownMode = gameMode == GameMode::LocalCoop ? playerTwoCameraMode : cameraMode;
        const std::string cameraPrompt = std::string(gameMode == GameMode::LocalCoop ? "P2 " : "")
            + (shownMode == CameraMode::Ball ? "BALL CAM [" : "CAR CAM [")
            + (gameMode == GameMode::LocalCoop ? "Y]" : keyName(boundKey(BindAction::Camera)) + " / Y]");
        DrawText(
            cameraPrompt.c_str(),
            ScreenWidth - 189,
            ScreenHeight - 91,
            16,
            cameraMode == CameraMode::Ball ? GOLD : Color{180, 196, 215, 255});
        DrawText("F1 CONTROLS", ScreenWidth - 129,
            gameMode == GameMode::LocalCoop ? ScreenHeight - 156 : ScreenHeight - 31,
            16, Color{180, 196, 215, 255});
        if (academyActive) {
            DrawText(
                ("RESET RUN [" + keyName(boundKey(BindAction::Restart)) + "]  /  SKIP [TAB]").c_str(),
                ScreenWidth - 302,
                ScreenHeight - 58,
                16,
                GOLD);
        } else if (gameMode == GameMode::Training) {
            DrawText(
                ("RESET [" + keyName(boundKey(BindAction::Restart)) + "]  /  FEED [TAB]").c_str(),
                ScreenWidth - 310,
                ScreenHeight - 58,
                16,
                GOLD);
        } else if (gameMode == GameMode::TargetChallenge) {
            DrawText(
                ("RESET RUN [" + keyName(boundKey(BindAction::Restart)) + "]").c_str(),
                ScreenWidth - 190,
                ScreenHeight - 58,
                16,
                GOLD);
        }

        if (cameraModeNotice > 0.0F) {
            DrawRectangle(24, 188, 174, 34, Color{7, 12, 22, 225});
            DrawText(cameraMode == CameraMode::Ball ? "BALL CAM" : "CAR CAM", 38, 197, 18, GOLD);
        }
        if (touchNoticeTimer > 0.0F) {
            const int noticeX = gameMode == GameMode::LocalCoop ? ScreenWidth / 2 - 188 : 24;
            const int noticeY = gameMode == GameMode::LocalCoop ? 153 : 230;
            DrawRectangle(noticeX, noticeY, 376, 34, Color{7, 12, 22, 210});
            DrawText(touchNotice.c_str(), noticeX + 12, noticeY + 9, fittedFontSize(touchNotice, 352, 16, 13),
                thirdTouchBoostTimer > 0.0F ? GOLD : RAYWHITE);
        }
        if (cars[0].respawnTimer > 0.0F) {
            constexpr int respawnWidth = 390;
            const int respawnX = (ScreenWidth - respawnWidth) / 2;
            DrawRectangle(respawnX, 225, respawnWidth, 96, Color{7, 12, 22, 238});
            DrawRectangle(respawnX, 225, 7, 96,
                cars[0].respawnCause == RespawnCause::Demolition ? ORANGE : SKYBLUE);
            drawCentered(cars[0].respawnCause == RespawnCause::Demolition ? "DEMOLISHED" : "OUT OF BOUNDS",
                240, 25, cars[0].respawnCause == RespawnCause::Demolition ? ORANGE : SKYBLUE);
            drawCentered(TextFormat("RESPAWN IN %d", std::max(1, static_cast<int>(std::ceil(cars[0].respawnTimer)))),
                278, 22, RAYWHITE);
        }
        drawTacticalRadar();
    }

    void drawMenuRow(const std::string &label, int index, int selected, int x, int y, int width) const {
        const bool active = index == selected;
        const Color panel = active ? Color{38, 56, 82, 245} : Color{15, 24, 39, 225};
        const Color edge = active ? GOLD : Color{77, 96, 121, 180};
        DrawRectangle(x, y, width, 52, panel);
        DrawRectangle(x, y, 6, 52, edge);
        if (active) {
            DrawText(">", x - 34, y + 10, 31, GOLD);
        }
        const int fontSize = fittedFontSize(label, width - 42, 22, 16);
        DrawText(label.c_str(), x + 24, y + (52 - fontSize) / 2, fontSize,
            active ? RAYWHITE : Color{188, 205, 222, 255});
    }

    void drawMainMenu() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 8, 17, 165});
        drawCentered("ROCKET", 42, 58, SKYBLUE);
        drawCentered("VOLLEY", 96, 58, ORANGE);
        drawCentered(activeArena().name, 167, 19, activeArena().accent);
        drawCenteredFitted("ARCADE CUP + ROCKET ACADEMY  /  2v2 + 3v3 + SPLIT-SCREEN",
            ScreenWidth / 2, 188, 760, 18, 15, RAYWHITE);

        const int x = ScreenWidth / 2 - 285;
        constexpr int width = 570;
        constexpr int startY = 208;
        constexpr int rowStep = 30;
        drawCompactMenuRow("ARCADE CUP  /  3 ROUNDS  /  CROWNS " + std::to_string(arcadeCupTitles),
            0, mainMenuIndex, x, startY, width);
        drawCompactMenuRow("START 2V2 MATCH", 1, mainMenuIndex, x, startY + rowStep, width);
        drawCompactMenuRow("START 3V3 ROTATION MATCH", 2, mainMenuIndex, x, startY + rowStep * 2, width);
        drawCompactMenuRow("LOCAL SPLIT-SCREEN 2P  /  P2 GAMEPAD", 3, mainMenuIndex, x, startY + rowStep * 3, width);
        drawCompactMenuRow(std::string("ROCKET ACADEMY  /  4 LESSONS  /  BEST ") + academyMedalName(academyBestMedal)
                + (academyBestGhost.empty() ? "" : "  /  PB GHOST"),
            4, mainMenuIndex, x, startY + rowStep * 4, width);
        drawCompactMenuRow("TRAINING  /  SOLO SERVE PRACTICE", 5, mainMenuIndex, x, startY + rowStep * 5, width);
        drawCompactMenuRow("TARGET CHALLENGE  /  10 SHOTS  /  BEST " + std::to_string(challengeBestScore),
            6, mainMenuIndex, x, startY + rowStep * 6, width);
        drawCompactMenuRow("ARENA     < " + arenaSelectionName() + " >", 7, mainMenuIndex, x, startY + rowStep * 7, width);
        drawCompactMenuRow("CUSTOMIZE CAR", 8, mainMenuIndex, x, startY + rowStep * 8, width);
        drawCompactMenuRow("CONTROLS", 9, mainMenuIndex, x, startY + rowStep * 9, width);
        drawCompactMenuRow(
            std::string("MATCH SETUP  /  ") + (difficulty == Difficulty::Pro ? "PRO" : "ROOKIE")
                + "  /  " + matchDurationLabel() + "  /  FIRST " + std::to_string(scoreLimit)
                + (powerupsEnabled ? "  /  POWER ON" : ""),
            10,
            mainMenuIndex,
            x,
            startY + rowStep * 10,
            width);
        drawCompactMenuRow(std::string("PILOT RECORD  /  ") + careerRankName() + "  /  "
                + std::to_string(careerXp) + " XP",
            11, mainMenuIndex, x, startY + rowStep * 11, width);
        drawCompactMenuRow("AUDIO / COMFORT", 12, mainMenuIndex, x, startY + rowStep * 12, width);
        drawCompactMenuRow("QUIT", 13, mainMenuIndex, x, startY + rowStep * 13, width);

        drawCentered("W/S MOVE     A/D CHANGE     ENTER SELECT", 642, 17, Color{204, 218, 230, 255});
        drawCentered(std::string("PILOT ") + careerRankName() + "  /  "
            + std::to_string(unlockedCareerMilestoneCount()) + "/" + std::to_string(CareerMilestoneCount)
            + " MILESTONES  /  CUP BEST " + std::to_string(arcadeCupBestRound) + "/3",
            670, 15, Color{150, 174, 196, 255});
        if (settingsNoticeTimer > 0.0F) {
            drawCentered(settingsNotice, 697, 16, GOLD);
        } else {
            drawCentered("F11 / ALT+ENTER FULLSCREEN  /  DRAG WINDOW EDGES TO RESIZE", 697, 14, Color{164, 185, 205, 255});
        }
    }

    void renderCustomizerPreviewTexture() const {
        BeginTextureMode(previewTarget);
        ClearBackground(Color{10, 17, 29, 255});
        Camera3D previewCamera{};
        previewCamera.position = {5.2F, 3.2F, 6.2F};
        previewCamera.target = {0.0F, 0.45F, 0.0F};
        previewCamera.up = {0.0F, 1.0F, 0.0F};
        previewCamera.fovy = 42.0F;
        previewCamera.projection = CAMERA_PERSPECTIVE;
        BeginMode3D(previewCamera);
        DrawPlane({0.0F, 0.0F, 0.0F}, {11.0F, 11.0F}, Color{23, 34, 49, 255});
        DrawGrid(12, 1.0F);
        const Quaternion rotation = QuaternionFromAxisAngle({0.0F, 1.0F, 0.0F}, totalTime * 0.7F);
        drawCarGeometry(
            {0.0F, 0.62F, 0.0F},
            rotation,
            cars[0].bodyStyle,
            cars[0].paint,
            cars[0].wheelColor,
            cars[0].spoilerColor,
            cars[0].decalColor,
            true);
        EndMode3D();
        EndTextureMode();
    }

    void drawMatchSetupMenu() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 8, 17, 205});
        DrawText("MATCH SETUP", 70, 53, 46, SKYBLUE);
        DrawText("BUILD YOUR FORMAT", 72, 107, 27, ORANGE);
        DrawText("W/S SELECT   A/D CHANGE   ENTER CYCLE", 72, 158, 16, Color{176, 197, 217, 255});

        drawMenuRow(std::string("AI DIFFICULTY     < ")
                + (difficulty == Difficulty::Pro ? "PRO" : "ROOKIE") + " >",
            0, matchSetupMenuIndex, 72, 215, 560);
        drawMenuRow("MATCH TIME       < " + matchDurationLabel() + " >",
            1, matchSetupMenuIndex, 72, 279, 560);
        drawMenuRow("POINT LIMIT      < FIRST TO " + std::to_string(scoreLimit) + " >",
            2, matchSetupMenuIndex, 72, 343, 560);
        drawMenuRow(TextFormat("BALL BOUNCE      < %d%% >", static_cast<int>(std::round(ballElasticity * 100.0F))),
            3, matchSetupMenuIndex, 72, 407, 560);
        drawMenuRow(std::string("POWER VOLLEY     < ") + (powerupsEnabled ? "ON" : "OFF") + " >",
            4, matchSetupMenuIndex, 72, 471, 560);
        drawMenuRow("BACK", 5, matchSetupMenuIndex, 72, 535, 560);

        DrawRectangle(686, 215, 510, 410, Color{12, 21, 35, 238});
        DrawRectangle(686, 215, 6, 410, GOLD);
        DrawText("COMPETITIVE FORMAT", 720, 246, 21, GOLD);
        DrawText("Applies to 2v2, 3v3, and local co-op.", 720, 286, 17, RAYWHITE);
        DrawText("At 0:00, finish the rally before the result.", 720, 320, 17, RAYWHITE);
        DrawText("A tied clock triggers sudden-death overtime.", 720, 350, 17, RAYWHITE);
        DrawText("POWER VOLLEY", 720, 394, 16, Color{215, 95, 255, 255});
        DrawText("Timed Haymaker, Freeze, and Magnet powers.", 720, 421, 17, RAYWHITE);
        DrawText("Optional in exhibitions; Arcade Cup stays pure.", 720, 451, 16, Color{176, 197, 217, 255});
        DrawText("ROOKIE", 720, 491, 16, SKYBLUE);
        DrawText("Slower ball + assisted boost recharge.", 720, 516, 16, RAYWHITE);
        DrawText("PRO", 720, 552, 16, ORANGE);
        DrawText("Full-speed rallies and faster AI decisions.", 720, 577, 16, RAYWHITE);
        DrawText("ESC ALSO RETURNS", 72, 604, 16, Color{145, 170, 193, 255});
        if (settingsNoticeTimer > 0.0F) {
            DrawText(settingsNotice.c_str(), 686, 620, 17, GOLD);
        }
    }

    void drawCustomizerPreview() const {
        DrawRectangle(636, 172, 574, 414, Color{10, 17, 29, 245});
        DrawRectangleLinesEx({636.0F, 172.0F, 574.0F, 414.0F}, 3.0F, Color{91, 126, 155, 255});
        DrawTextureRec(
            previewTarget.texture,
            {0.0F, 0.0F, static_cast<float>(previewTarget.texture.width), -static_cast<float>(previewTarget.texture.height)},
            {643.0F, 179.0F},
            WHITE);
        DrawText("LIVE PREVIEW", 659, 192, 17, GOLD);
    }

    void drawCustomizeMenu() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 8, 17, 185});
        DrawText("CUSTOMIZE", 58, 34, 43, SKYBLUE);
        DrawText("MAKE IT YOURS", 60, 78, 27, ORANGE);
        DrawText("W/S SELECT   A/D CHANGE   ENTER CYCLE", 60, 122, 17, Color{202, 216, 230, 255});

        drawMenuRow(
            std::string("PAINT       < ") + BodyColors[bodyColorIndex].name + " >",
            0,
            customizeMenuIndex,
            58,
            165,
            540);
        drawMenuRow(
            std::string("BODY KIT    < ") + BodyStyles[bodyStyleIndex].name + " >",
            1,
            customizeMenuIndex,
            58,
            221,
            540);
        drawMenuRow(
            std::string("WHEELS      < ") + WheelColors[wheelColorIndex].name + " >",
            2,
            customizeMenuIndex,
            58,
            277,
            540);
        const char *spoilerName = spoilerColorIndex == 1 ? "BODY MATCH" : SpoilerColors[spoilerColorIndex].name;
        drawMenuRow(std::string("SPOILER     < ") + spoilerName + " >",
            3, customizeMenuIndex, 58, 333, 540);
        const char *decalName = decalColorIndex == static_cast<int>(DecalColors.size()) - 1
            ? "BODY MATCH"
            : DecalColors[decalColorIndex].name;
        drawMenuRow(std::string("DECAL       < ") + decalName + " >",
            4, customizeMenuIndex, 58, 389, 540);
        drawMenuRow(std::string("BOOST FX    < ") + BoostColors[boostColorIndex].name + " >",
            5, customizeMenuIndex, 58, 445, 540);
        drawMenuRow("BACK", 6, customizeMenuIndex, 58, 515, 540);
        DrawText("ESC ALSO RETURNS", 58, 586, 17, Color{188, 205, 222, 255});
        drawCustomizerPreview();
    }

    void drawCompactMenuRow(const std::string &label, int index, int selected, int x, int y, int width) const {
        const bool active = index == selected;
        DrawRectangle(x, y, width, 31, active ? Color{38, 56, 82, 245} : Color{15, 24, 39, 225});
        DrawRectangle(x, y, 5, 31, active ? GOLD : Color{77, 96, 121, 180});
        if (active) {
            DrawText(">", x - 25, y + 3, 23, GOLD);
        }
        const int fontSize = fittedFontSize(label, width - 29, 17, 14);
        DrawText(label.c_str(), x + 16, y + (31 - fontSize) / 2, fontSize,
            active ? RAYWHITE : Color{188, 205, 222, 255});
    }

    void drawControlsMenu() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 8, 17, 205});
        DrawText("CONTROLS", 62, 42, 44, SKYBLUE);
        DrawText("SELECT AN ACTION, PRESS ENTER, THEN PRESS A KEY", 64, 94, 17, RAYWHITE);

        constexpr int rowX = 68;
        constexpr int rowWidth = 570;
        constexpr int rowStart = 128;
        constexpr int rowStep = 32;
        for (std::size_t index = 0; index < BindingCount; ++index) {
            const std::string label = std::string(BindingLabels[index]) + "   [ " + keyName(bindings[index]) + " ]";
            drawCompactMenuRow(label, static_cast<int>(index), controlsMenuIndex, rowX, rowStart + static_cast<int>(index) * rowStep, rowWidth);
        }
        drawCompactMenuRow("RESET DEFAULTS", static_cast<int>(BindingCount), controlsMenuIndex, rowX, 526, rowWidth);
        drawCompactMenuRow("BACK", static_cast<int>(BindingCount) + 1, controlsMenuIndex, rowX, 566, rowWidth);

        DrawRectangle(684, 132, 522, 448, Color{12, 21, 35, 238});
        DrawRectangle(684, 132, 6, 448, ORANGE);
        DrawText("GAMEPAD  /  FIXED", 718, 163, 23, ORANGE);
        DrawText("LEFT STICK", 718, 211, 17, GOLD);
        DrawText("Drive, steer, nose control", 718, 238, 18, RAYWHITE);
        DrawText("A", 718, 281, 17, GOLD);
        DrawText("Jump / double jump", 760, 281, 18, RAYWHITE);
        DrawText("X", 718, 321, 17, GOLD);
        DrawText("Directional dodge", 760, 321, 18, RAYWHITE);
        DrawText("B / RT", 718, 361, 17, GOLD);
        DrawText("Boost", 800, 361, 18, RAYWHITE);
        DrawText("Y", 718, 401, 17, GOLD);
        DrawText("Toggle camera", 760, 401, 18, RAYWHITE);
        DrawText("LB", 718, 441, 17, Color{215, 95, 255, 255});
        DrawText("Use Power Volley ability", 760, 441, 18, RAYWHITE);
        DrawText("MENU ACCESS", 718, 486, 17, SKYBLUE);
        DrawText("D-pad + A select / B back / Start pause", 718, 515, 17, RAYWHITE);
        DrawText("F1 help, F11 fullscreen, Enter reserved.", 718, 551, 15, Color{164, 185, 205, 255});

        DrawText("ESC BACK", 68, 638, 16, Color{164, 185, 205, 255});
        DrawText("Custom bindings override gameplay shortcuts.", 684, 638, 15, Color{164, 185, 205, 255});
        if (settingsNoticeTimer > 0.0F) {
            DrawText(settingsNotice.c_str(), 684, 607, 17, GOLD);
        }

        if (bindingCaptureIndex >= 0) {
            DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{3, 6, 12, 190});
            DrawRectangle(ScreenWidth / 2 - 285, 250, 570, 188, Color{17, 28, 46, 252});
            DrawRectangle(ScreenWidth / 2 - 285, 250, 7, 188, GOLD);
            drawCentered("PRESS A KEY", 284, 35, GOLD);
            drawCentered(BindingLabels[static_cast<std::size_t>(bindingCaptureIndex)], 340, 20, RAYWHITE);
            drawCentered("ESC CANCELS", 393, 16, Color{167, 188, 207, 255});
            if (settingsNoticeTimer > 0.0F) drawCentered(settingsNotice, 420, 14, GOLD);
        }
    }

    void drawPreferencesMenu() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 8, 17, 235});
        drawCentered("AUDIO / COMFORT", 72, 40, SKYBLUE);
        drawCentered(preferencesFromPause ? "MATCH PAUSED / CHANGES APPLY IMMEDIATELY" : "YOUR MIX, YOUR CAMERA / SAVES AUTOMATICALLY",
            130, 18, RAYWHITE);
        constexpr int x = ScreenWidth / 2 - 285;
        drawMenuRow(TextFormat("MUSIC     < %d%% >", preferences.musicVolume), 0, preferencesMenuIndex, x, 174, 570);
        drawMenuRow(TextFormat("SOUND EFFECTS     < %d%% >", preferences.effectsVolume), 1, preferencesMenuIndex, x, 238, 570);
        drawMenuRow(TextFormat("CAMERA SHAKE     < %d%% >", preferences.cameraShake), 2, preferencesMenuIndex, x, 302, 570);
        drawMenuRow(std::string("POINT REPLAYS     < ") + (preferences.pointReplays ? "ON" : "OFF") + " >",
            3, preferencesMenuIndex, x, 366, 570);
        drawMenuRow("RESTORE OPTIONS DEFAULTS", 4, preferencesMenuIndex, x, 444, 570);
        drawMenuRow(preferencesFromPause ? "BACK TO PAUSE" : "BACK", 5, preferencesMenuIndex, x, 508, 570);
        drawCentered("0% MUTES AUDIO OR DISABLES CAMERA SHAKE", 590, 16, GOLD);
        drawCentered("ARROWS / D-PAD ADJUST   ENTER / A SELECT   ESC / B BACK", 624, 16, RAYWHITE);
        if (settingsNoticeTimer > 0.0F) drawCentered(settingsNotice, 668, 16, GOLD);
    }

    void drawAboutMenu() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 8, 17, 205});
        DrawText("PILOT RECORD", 60, 42, 44, SKYBLUE);
        DrawText("CAREER PROGRESS + PROJECT", 62, 94, 23, ORANGE);
        DrawText(TextFormat("VERSION %s", ROCKET_VOLLEY_VERSION), 64, 132, 16, GOLD);

        DrawRectangle(60, 169, 584, 475, Color{12, 21, 35, 242});
        DrawRectangle(60, 169, 7, 475, SKYBLUE);
        DrawText(careerRankName(), 88, 193, 31, GOLD);
        DrawText(TextFormat("%d XP", careerXp), 531, 201, 18, RAYWHITE);
        const int rankIndex = careerRankIndex();
        const int rankStart = CareerRankThresholds[static_cast<std::size_t>(rankIndex)];
        const int rankEnd = rankIndex + 1 < static_cast<int>(CareerRankThresholds.size())
            ? CareerRankThresholds[static_cast<std::size_t>(rankIndex + 1)]
            : rankStart;
        const float rankProgress = rankEnd > rankStart
            ? clamp(static_cast<float>(careerXp - rankStart) / static_cast<float>(rankEnd - rankStart), 0.0F, 1.0F)
            : 1.0F;
        DrawRectangle(88, 237, 526, 17, Color{29, 42, 61, 255});
        DrawRectangle(91, 240, static_cast<int>(520.0F * rankProgress), 11, GOLD);
        DrawRectangleLines(88, 237, 526, 17, Color{92, 116, 142, 255});
        DrawText(rankIndex + 1 < static_cast<int>(CareerRankNames.size())
                ? TextFormat("NEXT: %s AT %d XP", CareerRankNames[static_cast<std::size_t>(rankIndex + 1)], rankEnd)
                : "MAXIMUM PILOT RANK",
            88, 262, 13, Color{166, 189, 209, 255});

        DrawText("CAREER TOTALS", 88, 295, 16, GOLD);
        DrawText(TextFormat("MATCHES  %d     WINS  %d     TEAM POINTS  %d", careerMatches, careerWins, careerPoints),
            88, 320, 15, RAYWHITE);
        DrawText(TextFormat("TEAM TOUCHES  %d     AERIALS  %d     BEST RALLY  %d", careerTouches, careerAerials, careerBestRally),
            88, 344, 15, RAYWHITE);
        DrawText(TextFormat("BOOST PADS  %d     POWER-UPS  %d", careerBoostPads, careerPowerups),
            88, 368, 15, RAYWHITE);

        DrawText(TextFormat("MILESTONES  %d / %d", unlockedCareerMilestoneCount(), CareerMilestoneCount), 88, 405, 17, GOLD);
        for (int index = 0; index < CareerMilestoneCount; ++index) {
            const int column = index / 5;
            const int row = index % 5;
            const int x = 88 + column * 264;
            const int y = 436 + row * 38;
            const bool unlocked = (careerMilestones & (1 << index)) != 0;
            const Color color = unlocked ? (index >= 8 ? GOLD : SKYBLUE) : Color{91, 108, 128, 255};
            DrawRectangle(x, y, 24, 24, unlocked ? withAlpha(color, 85) : Color{25, 35, 50, 255});
            DrawRectangleLines(x, y, 24, 24, color);
            DrawText(unlocked ? "+" : "-", x + 7, y + 3, 17, color);
            DrawText(CareerMilestoneNames[static_cast<std::size_t>(index)], x + 33, y, 13, color);
            DrawText(CareerMilestoneDescriptions[static_cast<std::size_t>(index)], x + 33, y + 16, 10,
                unlocked ? Color{174, 196, 214, 255} : Color{102, 119, 138, 255});
        }

        DrawText("PROJECT + LINKS", 680, 169, 21, GOLD);
        DrawText("Created by Vraj Patel  /  @VrajP0518", 680, 203, 16, RAYWHITE);
        DrawText("C++20  /  raylib  /  Jolt Physics  /  protocol v4", 680, 229, 14, Color{162, 185, 205, 255});
        drawMenuRow("OPEN GITHUB REPOSITORY", 0, aboutMenuIndex, 680, 265, 510);
        drawMenuRow("REPORT AN ISSUE", 1, aboutMenuIndex, 680, 329, 510);
        drawMenuRow("VIEW RELEASE / ONLINE ROADMAP", 2, aboutMenuIndex, 680, 393, 510);
        drawMenuRow("BACK", 3, aboutMenuIndex, 680, 473, 510);
        DrawText("Milestones reward normal play across every mode.", 680, 551, 15, SKYBLUE);
        DrawText("Progress saves automatically in your local profile.", 680, 578, 15, Color{162, 185, 205, 255});
        DrawText("W / S MOVE     ENTER OPEN     ESC BACK", 680, 624, 16, RAYWHITE);
    }

    void drawHelp() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{5, 8, 15, 215});
        DrawRectangle(ScreenWidth / 2 - 330, 120, 660, 520, Color{18, 27, 43, 248});
        DrawRectangle(ScreenWidth / 2 - 330, 120, 8, 520, GOLD);
        drawCentered("CONTROLS", 154, 34, GOLD);
        drawCenteredFitted(keyName(boundKey(BindAction::Forward)) + " / " + keyName(boundKey(BindAction::Reverse)) + "     Accelerate / brake",
            ScreenWidth / 2, 222, 600, 23, 16, RAYWHITE);
        drawCenteredFitted(keyName(boundKey(BindAction::SteerLeft)) + " / " + keyName(boundKey(BindAction::SteerRight)) + "     Steer",
            ScreenWidth / 2, 264, 600, 23, 16, RAYWHITE);
        drawCenteredFitted(keyName(boundKey(BindAction::Jump)) + " / A    Jump, then jump again",
            ScreenWidth / 2, 306, 600, 23, 16, RAYWHITE);
        drawCenteredFitted("AIR: " + keyName(boundKey(BindAction::Reverse)) + " / STICK DOWN NOSE UP, " + keyName(boundKey(BindAction::Boost)) + " / RT BOOST",
            ScreenWidth / 2, 348, 600, 20, 14, RAYWHITE);
        drawCenteredFitted(keyName(boundKey(BindAction::Dodge)) + " / X        Directional dodge / flip",
            ScreenWidth / 2, 390, 600, 23, 16, RAYWHITE);
        drawCenteredFitted(keyName(boundKey(BindAction::Camera)) + " / Y        Toggle Car Cam / Ball Cam",
            ScreenWidth / 2, 424, 600, 22, 15, RAYWHITE);
        drawCenteredFitted(keyName(boundKey(BindAction::Powerup)) + " / LB      Use Power Volley ability",
            ScreenWidth / 2, 456, 600, 22, 15,
            Color{215, 95, 255, 255});
        drawCenteredFitted(keyName(boundKey(BindAction::Pause)) + " pause   1/2 AI   [/] ball bounce",
            ScreenWidth / 2, 488, 600, 20, 15, RAYWHITE);
        drawCenteredFitted("Gamepad: stick drive, A jump, X dodge, B/RT boost",
            ScreenWidth / 2, 518, 600, 18, 15, Color{196, 213, 228, 255});
        drawCenteredFitted(keyName(boundKey(BindAction::Restart)) + " reset   TAB feeds / Academy skip   G PB ghost",
            ScreenWidth / 2, 546, 600, 17, 14, Color{196, 213, 228, 255});
        drawCentered("3 TEAM TOUCHES = POWER  /  4TH TOUCH = FAULT", 570, 15, GOLD);
        drawCentered("GOLD PADS: FULL / 10S   SMALL PADS: +28 / 4S", 593, 14, SKYBLUE);
        drawCentered("F11 / ALT+ENTER FULLSCREEN    F1 TO CLOSE", 618, 16, Color{176, 199, 219, 255});
    }

    void drawLoadingScreen() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 8, 18, 242});
        DrawCircleGradient({ScreenWidth / 2.0F, 255.0F}, 190.0F, Color{48, 139, 190, 75}, Color{4, 8, 18, 0});
        drawCentered(activeArena().name, 174, 24, activeArena().accent);
        const std::string loadingTitle = arcadeCupActive
            ? std::string("ARCADE CUP  /  ROUND ") + std::to_string(arcadeCupRound + 1)
            : (academyActive
                    ? "ROCKET ACADEMY"
                    : (pendingGameMode == GameMode::Training
                    ? "PREPARING TRAINING"
                    : (pendingGameMode == GameMode::TargetChallenge
                            ? "TARGET CHALLENGE"
                    : (pendingGameMode == GameMode::LocalCoop
                            ? "PREPARING SPLIT-SCREEN"
                            : (pendingGameMode == GameMode::ThreeVsThree ? "PREPARING 3V3" : "PREPARING ARENA")))));
        drawCentered(loadingTitle, 218, 48, RAYWHITE);
        drawCentered(
            arcadeCupActive
                ? std::string(arcadeCupRoundName()) + "  /  "
                    + (pendingGameMode == GameMode::ThreeVsThree ? "3v3 ROTATIONS" : "2v2 OPENING ROUND")
                : (academyActive
                ? "Four guided lessons  /  master boost, jumps, aerials, and placement"
                : (pendingGameMode == GameMode::Training
                ? "Loading a solo court and repeatable serve feed"
                : (pendingGameMode == GameMode::TargetChallenge
                        ? "Ten varied feeds  /  land returns inside the moving target"
                : (pendingGameMode == GameMode::LocalCoop
                        ? "Independent P1 + P2 cameras  /  keyboard + gamepad"
                        : (pendingGameMode == GameMode::ThreeVsThree
                                ? "Six cars  /  rotating striker, setter, and back cover"
                                : "Synchronizing cars, cameras, and team rotations"))))),
            288,
            18,
            Color{176, 199, 219, 255});
        constexpr int barWidth = 580;
        const int barX = (ScreenWidth - barWidth) / 2;
        const float progress = clamp(loadingTimer / 1.6F, 0.0F, 1.0F);
        DrawRectangle(barX, 350, barWidth, 24, Color{17, 27, 44, 255});
        DrawRectangle(barX + 4, 354, static_cast<int>((barWidth - 8) * progress), 16, GOLD);
        DrawRectangleLines(barX, 350, barWidth, 24, Color{95, 126, 154, 255});
        drawCentered(TextFormat("%d%%", static_cast<int>(progress * 100.0F)), 391, 19, RAYWHITE);
        if (arcadeCupActive) {
            constexpr int nodeWidth = 152;
            constexpr int nodeGap = 34;
            const int bracketWidth = nodeWidth * ArcadeCupRoundCount + nodeGap * (ArcadeCupRoundCount - 1);
            const int bracketX = (ScreenWidth - bracketWidth) / 2;
            for (int round = 0; round < ArcadeCupRoundCount; ++round) {
                const int nodeX = bracketX + round * (nodeWidth + nodeGap);
                const bool completed = round < arcadeCupRound;
                const bool current = round == arcadeCupRound;
                DrawRectangle(nodeX, 444, nodeWidth, 38,
                    current ? Color{45, 58, 78, 245} : Color{14, 23, 38, 225});
                DrawRectangle(nodeX, 444, 5, 38, completed ? SKYBLUE : (current ? GOLD : Color{70, 84, 104, 210}));
                DrawText(TextFormat("R%d  %s", round + 1, completed ? "WON" : (current ? "LIVE" : "LOCKED")),
                    nodeX + 16, 455, 15, completed ? SKYBLUE : (current ? GOLD : Color{135, 153, 173, 255}));
                if (round + 1 < ArcadeCupRoundCount) {
                    DrawLine(nodeX + nodeWidth, 463, nodeX + nodeWidth + nodeGap, 463,
                        completed ? SKYBLUE : Color{70, 84, 104, 210});
                }
            }
            drawCentered(TextFormat("%s AI  /  %s  /  FIRST TO %d", difficulty == Difficulty::Pro ? "PRO" : "ROOKIE",
                pendingGameMode == GameMode::ThreeVsThree ? "3v3" : "2v2", scoreLimit), 514, 18, GOLD);
            drawCentered("WIN TO ADVANCE  /  ONE LOSS ENDS THE RUN", 548, 16, Color{176, 199, 219, 255});
        } else if (academyActive) {
            constexpr int nodeWidth = 172;
            constexpr int nodeGap = 18;
            const int courseWidth = nodeWidth * AcademyLessonCount + nodeGap * (AcademyLessonCount - 1);
            const int courseX = (ScreenWidth - courseWidth) / 2;
            for (int lesson = 0; lesson < AcademyLessonCount; ++lesson) {
                const int nodeX = courseX + lesson * (nodeWidth + nodeGap);
                const bool completed = lesson < academyLessonsCompleted;
                const bool current = lesson == academyLessonIndex();
                DrawRectangle(nodeX, 438, nodeWidth, 42,
                    current ? Color{45, 58, 78, 245} : Color{14, 23, 38, 225});
                DrawRectangle(nodeX, 438, 5, 42,
                    completed ? SKYBLUE : (current ? GOLD : Color{70, 84, 104, 210}));
                DrawText(TextFormat("%d  %s", lesson + 1, AcademyLessonNames[static_cast<std::size_t>(lesson)]),
                    nodeX + 14, 451, 13, completed ? SKYBLUE : (current ? GOLD : Color{135, 153, 173, 255}));
                if (lesson + 1 < AcademyLessonCount) {
                    DrawLine(nodeX + nodeWidth, 459, nodeX + nodeWidth + nodeGap, 459,
                        completed ? SKYBLUE : Color{70, 84, 104, 210});
                }
            }
            const float bestSeconds = static_cast<float>(academyBestTimeCentiseconds) / 100.0F;
            drawCentered(std::string("BEST ") + academyMedalName(academyBestMedal)
                    + (academyBestTimeCentiseconds > 0 ? TextFormat("  /  %.2fs", bestSeconds) : "  /  NO RUN YET"),
                510, 18, academyMedalColor(academyBestMedal));
            drawCentered(academyBestGhost.empty()
                    ? "PB GHOST: SET A COMPLETE RUN TO RECORD"
                    : std::string("PB GHOST READY  /  G TOGGLE  /  ") + (academyGhostEnabled ? "ON" : "OFF"),
                538, 15, academyBestGhost.empty() ? Color{135, 153, 173, 255} : Color{115, 235, 255, 255});
            drawCentered("GOLD: UNDER 75s WITH ZERO RETRIES  /  PROGRESS SAVES AUTOMATICALLY",
                566, 16, Color{176, 199, 219, 255});
        } else if (pendingGameMode == GameMode::TargetChallenge) {
            drawCentered("TARGET HITS BUILD COMBOS  /  BULLSEYE 100  /  GREAT 65  /  GOOD 35", 494, 17, GOLD);
            drawCentered("TEN SHOTS  /  RECORDS SAVE AUTOMATICALLY", 527, 16, Color{176, 199, 219, 255});
        } else if (pendingGameMode == GameMode::Training) {
            drawCentered("MEET THE BALL BEFORE IT LANDS  /  RESET TO RETRY THE SAME FEED", 494, 17, SKYBLUE);
            drawCentered("L LOCKS THE SHOT / TAB CHANGES FEED / D-PAD RIGHT LOCKS, UP CHANGES, DOWN RETRIES", 527, 15, Color{176, 199, 219, 255});
        } else {
            drawCentered("TIP: " + keyName(boundKey(BindAction::Jump)) + " TWICE, TILT NOSE-UP WITH " + keyName(boundKey(BindAction::Reverse)) + ", THEN BOOST", 494, 17, SKYBLUE);
            drawCentered("USE UP TO 3 TEAM TOUCHES FOR A POWERED RETURN  /  4TH IS A FAULT", 527, 16, Color{176, 199, 219, 255});
        }
    }

    void drawOverlay() const {
        if (state != MatchState::Title && state != MatchState::Loading) {
            drawHud();
        }

        if (state == MatchState::Title) {
            if (menuPage == MenuPage::Main) {
                drawMainMenu();
            } else if (menuPage == MenuPage::MatchSetup) {
                drawMatchSetupMenu();
            } else if (menuPage == MenuPage::Customize) {
                drawCustomizeMenu();
            } else if (menuPage == MenuPage::Controls) {
                drawControlsMenu();
            } else if (menuPage == MenuPage::Preferences) {
                drawPreferencesMenu();
            } else {
                drawAboutMenu();
            }
        } else if (state == MatchState::Loading) {
            drawLoadingScreen();
        } else if (state == MatchState::ServeCountdown) {
            if (scriptedAerialTest) {
            } else if (academyActive) {
                DrawRectangle(ScreenWidth / 2 - 290, 184, 580, 278, Color{7, 12, 22, 228});
                DrawRectangle(ScreenWidth / 2 - 290, 184, 8, 278, GOLD);
                drawCentered(TextFormat("LESSON %d / %d", academyLessonIndex() + 1, AcademyLessonCount), 211, 18, SKYBLUE);
                drawCentered(academyLessonName(), 244, 30, RAYWHITE);
                drawCentered(TextFormat("%d", std::max(1, static_cast<int>(std::ceil(serveCountdown)))), 294, 104, GOLD);
                drawCenteredFitted(academyLessonInstruction(), ScreenWidth / 2, 411, 530, 19, 14,
                    Color{214, 226, 237, 255});
                drawCenteredFitted(std::string("TAB SKIP  /  R RESTART  /  G PB GHOST ")
                        + (academyGhostEnabled ? "ON" : "OFF"),
                    ScreenWidth / 2, 437, 530, 15, 13,
                    academyBestGhost.empty() ? Color{178, 198, 216, 255} : Color{115, 235, 255, 255});
            } else {
                constexpr int countdownWidth = 380;
                const int countdownX = (ScreenWidth - countdownWidth) / 2;
                DrawRectangle(countdownX, 190, countdownWidth, 274, Color{7, 12, 22, 235});
                DrawRectangle(countdownX, 190, 8, 274, GOLD);
                drawCentered(gameMode == GameMode::Training
                        ? "PRACTICE FEED"
                        : (gameMode == GameMode::TargetChallenge
                                ? "TARGET SHOT"
                                : (arcadeCupActive ? arcadeCupRoundName() : "GET READY")),
                    216, 27, RAYWHITE);
                drawCentered(TextFormat("%d", std::max(1, static_cast<int>(std::ceil(serveCountdown)))), 266, 112, GOLD);
                const std::string serveMessage = gameMode == GameMode::Training
                    ? "BALL INCOMING"
                    : (gameMode == GameMode::TargetChallenge
                            ? std::string("SHOT ") + std::to_string(trainingAttempts) + "  /  " + currentTrainingFeedName()
                            : (servingTeam == 0
                                ? (servingCar == 0 ? "YOU SERVE  /  HIT THE TOSS" : "TEAMMATE SERVES  /  COVER")
                                : (receivingCar == 0 ? "YOU RECEIVE FIRST  /  TEAMMATE COVERS"
                                    : (gameMode == GameMode::LocalCoop ? "P2 RECEIVES FIRST  /  P1 COVERS" : "TEAMMATE RECEIVES FIRST  /  YOU COVER"))));
                drawCenteredFitted(serveMessage, ScreenWidth / 2, 394, countdownWidth - 42, 21, 16,
                    Color{224, 233, 242, 255});
                if (gameMode == GameMode::Training) {
                    drawCenteredFitted(keyName(boundKey(BindAction::Restart)) + " RESET SERVE",
                        ScreenWidth / 2, 426, countdownWidth - 42, 18, 15, SKYBLUE);
                }
            }
        } else if (state == MatchState::Paused) {
            if (preferencesFromPause) {
                drawPreferencesMenu();
                return;
            }
            DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 7, 14, 190});
            drawCentered("PAUSED", 270, 55, GOLD);
            const std::string pausePrompt = keyName(boundKey(BindAction::Pause)) + " RESUME  /  "
                + keyName(boundKey(BindAction::Restart))
                + (academyActive
                        ? " RESET ACADEMY  /  "
                        : (gameMode == GameMode::Training
                        ? " RESET SERVE  /  "
                        : (gameMode == GameMode::TargetChallenge ? " RESET RUN  /  " : " RESTART  /  ")))
                + keyName(boundKey(BindAction::MainMenu)) + " MAIN MENU";
            drawCenteredFitted(pausePrompt, ScreenWidth / 2, 345, ScreenWidth - 120, 22, 15, RAYWHITE);
            drawCenteredFitted(pauseNotice, ScreenWidth / 2, 390, ScreenWidth - 120, 18, 14, GOLD);
            drawCentered("F2 / GAMEPAD Y: AUDIO / COMFORT", 435, 17, SKYBLUE);
        } else if (state == MatchState::PointWon) {
            DrawRectangle(0, 210, ScreenWidth, 175, Color{7, 10, 18, 220});
            drawCentered(scoringTeam == 0 ? "BLUE SCORES!" : "ORANGE SCORES!", 242, 48, scoringTeam == 0 ? SKYBLUE : ORANGE);
            drawCentered(TextFormat("%d  -  %d", score[0], score[1]), 309, 30, RAYWHITE);
        } else if (state == MatchState::GoalReplay) {
            DrawRectangle(22, 100, 244, 68, Color{7, 10, 18, 225});
            DrawRectangle(22, 100, 7, 68, scoringTeam == 0 ? SKYBLUE : ORANGE);
            DrawText("INSTANT REPLAY", 43, 111, 23, GOLD);
            DrawText("SPACE / ENTER / A TO SKIP", 43, 140, 14, Color{186, 204, 220, 255});
            DrawRectangle(ScreenWidth / 2 - 120, ScreenHeight - 76, 240, 42, Color{7, 10, 18, 215});
            drawCentered(scoringTeam == 0 ? "BLUE FINISH" : "ORANGE FINISH", ScreenHeight - 65, 20,
                scoringTeam == 0 ? SKYBLUE : ORANGE);
        } else if (state == MatchState::GameOver) {
            DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 7, 14, 195});
            if (academyActive) {
                const float runSeconds = academyRunTimer;
                const float bestSeconds = static_cast<float>(academyBestTimeCentiseconds) / 100.0F;
                const Color medalColor = academyMedalColor(academyRunMedal);
                drawCentered(!academyRecordEligible
                        ? "ACADEMY TOUR COMPLETE"
                        : (academyNewRecord ? "NEW ACADEMY RECORD" : "ACADEMY COMPLETE"),
                    170, 48, academyNewRecord ? GOLD : SKYBLUE);
                drawCentered(academyMedalName(academyRunMedal), 236, 54, medalColor);
                drawCentered(TextFormat("TIME  %.2fs     RETRIES  %d", runSeconds, academyRetries), 310, 23, RAYWHITE);
                drawCentered(TextFormat("LESSONS  %d / %d", academyLessonsCompleted, AcademyLessonCount), 351, 19, SKYBLUE);
                const std::string personalBest = academyBestTimeCentiseconds > 0
                    ? TextFormat("PERSONAL BEST  %s  /  %.2fs", academyMedalName(academyBestMedal), bestSeconds)
                    : "PERSONAL BEST  UNRANKED  /  NO TIME";
                drawCentered(personalBest, 388, 19, academyMedalColor(academyBestMedal));
                const std::string ghostResult = !academyRecordEligible
                    ? "UNRANKED TOUR  /  SKIPPED LESSONS DO NOT SET PB"
                    : (academyNewRecord
                        ? (academyResultDelta < -0.005F
                                ? TextFormat("PB GHOST UPDATED  /  %.2fs FASTER", -academyResultDelta)
                                : "PB GHOST RECORDED")
                        : TextFormat("PB DELTA  %+.2fs  /  GHOST PRESERVED", academyResultDelta));
                drawCentered(academyGhostSaved ? ghostResult : "PB TIME SAVED  /  GHOST FILE UNAVAILABLE",
                    420, 16, academyGhostSaved ? Color{115, 235, 255, 255} : ORANGE);
                drawCentered("PRESS ENTER TO RUN THE COURSE AGAIN", 463, 22, GOLD);
                drawCentered(keyName(boundKey(BindAction::MainMenu)) + " MAIN MENU", 504, 17, Color{176, 199, 219, 255});
            } else if (gameMode == GameMode::TargetChallenge) {
                drawCentered(challengeNewRecord ? "NEW TARGET RECORD" : "TARGET RUN COMPLETE", 202, 50,
                    challengeNewRecord ? GOLD : SKYBLUE);
                drawCentered(TextFormat("SCORE  %04d", challengeScore), 278, 38, RAYWHITE);
                drawCentered(TextFormat("TARGETS  %d / %d     RETURNS  %d / %d", challengeTargetsHit,
                    TargetChallengeShots, trainingReturns, TargetChallengeShots), 345, 20, RAYWHITE);
                drawCentered(TextFormat("BEST COMBO  x%d     ALL-TIME  %04d", challengeBestCombo, challengeBestScore),
                    382, 19, Color{188, 207, 223, 255});
                drawCentered("PRESS ENTER TO RUN IT AGAIN", 447, 23, GOLD);
                drawCentered(keyName(boundKey(BindAction::MainMenu)) + " MAIN MENU", 491, 17, Color{176, 199, 219, 255});
            } else if (arcadeCupActive) {
                const bool roundWon = winner == 0;
                const bool champion = roundWon && arcadeCupRound == ArcadeCupRoundCount - 1;
                drawCentered(champion ? "ARCADE CUP CHAMPIONS" : (roundWon ? "ROUND CLEARED" : "CUP RUN ENDS"),
                    184, 48, champion ? GOLD : (roundWon ? SKYBLUE : ORANGE));
                drawCentered(arcadeCupRoundName(), 247, 24, RAYWHITE);
                drawCentered(TextFormat("FINAL  %d - %d", score[0], score[1]), 292, 30, RAYWHITE);

                constexpr int nodeWidth = 138;
                constexpr int nodeGap = 28;
                const int bracketWidth = nodeWidth * ArcadeCupRoundCount + nodeGap * (ArcadeCupRoundCount - 1);
                const int bracketX = (ScreenWidth - bracketWidth) / 2;
                for (int round = 0; round < ArcadeCupRoundCount; ++round) {
                    const int nodeX = bracketX + round * (nodeWidth + nodeGap);
                    const bool wonNode = round < arcadeCupMatchWins;
                    const bool lostNode = !roundWon && round == arcadeCupRound;
                    DrawRectangle(nodeX, 350, nodeWidth, 40, Color{12, 21, 35, 238});
                    DrawRectangle(nodeX, 350, 5, 40, wonNode ? SKYBLUE : (lostNode ? ORANGE : Color{70, 84, 104, 210}));
                    DrawText(TextFormat("R%d  %s", round + 1, wonNode ? "WIN" : (lostNode ? "LOSS" : "--")),
                        nodeX + 15, 362, 15, wonNode ? SKYBLUE : (lostNode ? ORANGE : Color{135, 153, 173, 255}));
                    if (round + 1 < ArcadeCupRoundCount) {
                        DrawLine(nodeX + nodeWidth, 370, nodeX + nodeWidth + nodeGap, 370,
                            wonNode ? SKYBLUE : Color{70, 84, 104, 210});
                    }
                }
                drawCentered(TextFormat("BEST RUN  %d/3     CROWNS  %d", arcadeCupBestRound, arcadeCupTitles),
                    424, 19, GOLD);
                drawCentered(roundWon && !champion ? "PRESS ENTER FOR NEXT ROUND" : "PRESS ENTER TO START A NEW CUP",
                    474, 22, RAYWHITE);
                drawCentered(keyName(boundKey(BindAction::MainMenu)) + " ABANDON CUP / MAIN MENU",
                    516, 16, Color{176, 199, 219, 255});
            } else {
                drawCentered(winner == 0 ? "BLUE WINS" : "ORANGE WINS", 222, 62, winner == 0 ? SKYBLUE : ORANGE);
                drawCentered(TextFormat("FINAL  %d - %d", score[0], score[1]), 310, 30, RAYWHITE);
                drawCentered(TextFormat("TOUCHES  %d - %d     AERIALS  %d - %d", matchTouches[0], matchTouches[1],
                    matchAerialTouches[0], matchAerialTouches[1]), 360, 19, RAYWHITE);
                drawCentered(TextFormat("FASTEST BALL  %03d     BEST RALLY  %d", static_cast<int>(fastestBallSpeed * 11.0F),
                    bestRallyTouches), 393, 18, Color{188, 207, 223, 255});
                drawCentered(TextFormat("BOOST USED  %03d - %03d     PADS  %d - %d", static_cast<int>(matchBoostSpent[0]),
                    static_cast<int>(matchBoostSpent[1]), matchBoostPickups[0], matchBoostPickups[1]),
                    424, 17, Color{188, 207, 223, 255});
                drawCentered(TextFormat("DEMOLITIONS  %d - %d", matchDemolitions[0], matchDemolitions[1]),
                    452, 17, ORANGE);
                if (powerVolleyActive()) {
                    drawCentered(TextFormat("POWER-UPS USED  %d - %d", matchPowerupsUsed[0], matchPowerupsUsed[1]),
                        480, 17, Color{215, 95, 255, 255});
                }
                drawCentered("PRESS ENTER TO PLAY AGAIN", powerVolleyActive() ? 520 : 492, 23, GOLD);
                drawCentered(keyName(boundKey(BindAction::MainMenu)) + " MAIN MENU",
                    powerVolleyActive() ? 558 : 533, 17, Color{176, 199, 219, 255});
            }
            if (careerLastXpAward > 0) {
                constexpr int recordWidth = 326;
                const int recordX = ScreenWidth - recordWidth - 24;
                DrawRectangle(recordX, 94, recordWidth, 72, Color{8, 14, 24, 238});
                DrawRectangle(recordX, 94, 6, 72, GOLD);
                DrawText(TextFormat("PILOT %s  /  +%d XP", careerRankName(), careerLastXpAward),
                    recordX + 17, 105, 17, GOLD);
                DrawText(careerUnlockNotice.c_str(), recordX + 17, 134, 13, Color{184, 205, 221, 255});
            }
        } else if (state == MatchState::Playing && serveInProgress && competitiveMode()) {
            DrawRectangle(ScreenWidth / 2 - 225, 168, 450, 52, Color{7, 12, 22, 220});
            drawCenteredFitted(servingCar == 0 ? "SERVE LIVE  /  HIT THE BALL" : "SERVE LIVE  /  AI APPROACHING",
                ScreenWidth / 2, 183, 414, 21, 16, GOLD);
        } else if (state == MatchState::Playing && rallyTime < 0.48F) {
            drawCentered("GO!", 210, 66, GOLD);
        }

        if (showHelp) {
            drawHelp();
        }
    }

    void draw() const {
        if (state == MatchState::Title && menuPage == MenuPage::Customize) {
            renderCustomizerPreviewTexture();
        }
        const bool localSplitView = gameMode == GameMode::LocalCoop
            && state != MatchState::Title && state != MatchState::Loading;
        if (localSplitView) {
            renderLocalCoopViews();
        }
        BeginTextureMode(sceneTarget);
        ClearBackground(Color{8, 14, 27, 255});
        if (localSplitView) {
            drawLocalCoopViews();
        } else {
            drawBackdrop();
            drawWorld();
            drawBallLocator(camera, ScreenWidth, ScreenHeight);
        }
        drawOverlay();
        if (localSplitView) {
            drawLocalCoopLabels();
        }
        EndTextureMode();
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(sceneTarget.texture, {0.0F, 0.0F, static_cast<float>(ScreenWidth), -static_cast<float>(ScreenHeight)},
            displayViewport(GetScreenWidth(), GetScreenHeight()), {0.0F, 0.0F}, 0.0F, WHITE);
        EndDrawing();
    }

    int runHeadlessTests();

    int run() {
        if (headlessTestMode) return runHeadlessTests();
        float smokeElapsed = 0.0F;
        bool menuCaptured = false;
        bool cityArenaCaptured = false;
        bool customizeCaptured = false;
        bool customizationPassed = false;
        bool controlsCaptured = false;
        bool preferencesTestPassed = false;
        bool preferencesCaptured = false;
        int preferencesCaptureFrames = 0;
        const auto smokeOriginalPreferences = preferences;
        bool matchSetupPrepared = false;
        bool matchSetupCaptured = false;
        bool matchSettingsPassed = false;
        bool aboutCaptured = false;
        bool bindingTestPassed = false;
        bool aboutLinksVerified = false;
        bool careerProgressionPassed = false;
        bool arcadeCupStarted = false;
        bool arcadeCupLoadingCaptured = false;
        bool arcadeCupResultsCaptured = false;
        bool arcadeCupProgressionPassed = false;
        bool arcadeCupLogicPassed = false;
        bool multiplayerTestRun = false;
        bool multiplayerProtocolPassed = false;
        bool localCoopPassed = false;
        bool localCoopCaptured = false;
        bool splitScreenPassed = false;
        bool outOfBoundsRecoveryPassed = false;
        bool demolitionPassed = false;
        bool boostPadEconomyPassed = false;
        bool boostAiRoutingPassed = false;
        bool powerupGrantPassed = false;
        bool haymakerPassed = false;
        bool freezerPassed = false;
        bool magnetizerPassed = false;
        bool powerupAiPassed = false;
        bool threeVsThreeStarted = false;
        bool threeVsThreeLoadingSeen = false;
        bool threeVsThreeCaptured = false;
        bool threeVsThreePassed = false;
        bool touchRulePassed = false;
        bool trainingLoadingCaptured = false;
        bool trainingCountdownCaptured = false;
        bool trainingPlayCaptured = false;
        bool trainingGroundPassed = false;
        bool trainingResetPassed = false;
        bool trainingFeedsPassed = false;
        bool uiTextFitPassed = false;
        bool academyLoadingCaptured = false;
        bool academyCountdownCaptured = false;
        bool academyPlayCaptured = false;
        bool academyObjectivesPassed = false;
        bool academyResultsCaptured = false;
        bool academyPersistencePassed = false;
        bool academyGhostCodecPassed = false;
        bool academyGhostPlaybackPassed = false;
        bool academyGhostRecordPassed = false;
        bool academySkipIntegrityPassed = false;
        int academyResultsDelayFrames = 0;
        int academyOriginalBestMedal = academyBestMedal;
        int academyOriginalBestTime = academyBestTimeCentiseconds;
        int academyOriginalCompletions = academyCompletions;
        std::vector<AcademyGhostFrame> academyOriginalGhost = academyBestGhost;
        bool academyOriginalGhostSaved = academyGhostSaved;
        bool academyOriginalGhostEnabled = academyGhostEnabled;
        bool challengeLoadingCaptured = false;
        bool challengeCountdownCaptured = false;
        bool challengePlayCaptured = false;
        bool challengeScoringPassed = false;
        bool challengeResultsCaptured = false;
        bool arenaTestPassed = false;
        bool rookieSpeedPassed = false;
        bool proSpeedPassed = false;
        bool teammateLanePassed = false;
        bool trueServeTossPassed = false;
        bool trueServeContactPassed = false;
        bool matchStarted = false;
        bool loadingCaptured = false;
        bool countdownCaptured = false;
        bool carCameraCaptured = false;
        bool ballCameraActivated = false;
        bool ballCameraCaptured = false;
        bool ballCameraFramingPassed = false;
        bool replayTriggered = false;
        bool replayCaptured = false;
        int replayCaptureFrames = 0;
        bool replayPassed = false;
        bool aerialTestStarted = false;
        bool aerialCaptured = false;
        bool sprintTestStarted = false;
        float ballCameraElapsed = 0.0F;
        while (!WindowShouldClose() && !shouldExit) {
            const float deltaSeconds = std::min(GetFrameTime(), 0.1F);
            update(deltaSeconds);
            draw();
            if (smokeTestMode) {
                smokeElapsed += deltaSeconds;
                if (!menuCaptured && smokeElapsed >= 0.5F) {
                    activeArenaIndex = 0;
                    TakeScreenshot("rocket_volley_menu_smoke.png");
                    menuCaptured = true;
                }
                if (menuCaptured && !cityArenaCaptured && smokeElapsed >= 0.65F) {
                    activeArenaIndex = 2;
                }
                if (!cityArenaCaptured && smokeElapsed >= 0.85F) {
                    TakeScreenshot("rocket_volley_city_arena_smoke.png");
                    cityArenaCaptured = true;
                }
                if (cityArenaCaptured && !customizeCaptured && smokeElapsed >= 1.0F) {
                    menuPage = MenuPage::Customize;
                    bodyColorIndex = 4;
                    bodyStyleIndex = 3;
                    wheelColorIndex = 2;
                    spoilerColorIndex = 5;
                    decalColorIndex = 0;
                    boostColorIndex = 4;
                    applyPlayerCustomization();
                    customizationPassed = cars[0].bodyStyle == bodyStyleIndex
                        && cars[0].paint.r == BodyColors[bodyColorIndex].color.r
                        && cars[0].decalColor.b == DecalColors[decalColorIndex].color.b
                        && cars[0].boostColor.g == BoostColors[boostColorIndex].color.g
                        && BodyStyles.size() >= 5 && DecalColors.size() >= 8 && BoostColors.size() >= 7;
                }
                if (!customizeCaptured && smokeElapsed >= 1.35F) {
                    TakeScreenshot("rocket_volley_customize_smoke.png");
                    customizeCaptured = true;
                }
                if (customizeCaptured && !controlsCaptured && smokeElapsed >= 1.5F) {
                    menuPage = MenuPage::Controls;
                }
                if (!controlsCaptured && smokeElapsed >= 1.85F) {
                    const bool firstAssignment = assignBinding(bindingIndex(BindAction::Jump), KEY_Q);
                    const bool swapAssignment = assignBinding(bindingIndex(BindAction::Dodge), KEY_Q);
                    bindingTestPassed = firstAssignment && swapAssignment
                        && boundKey(BindAction::Jump) == KEY_E
                        && boundKey(BindAction::Dodge) == KEY_Q
                        && bindingsAreUnique(bindings);
                    resetBindings();
                    TakeScreenshot("rocket_volley_controls_smoke.png");
                    controlsCaptured = true;
                }
                if (controlsCaptured && !matchSetupPrepared && smokeElapsed >= 2.0F) {
                    menuPage = MenuPage::MatchSetup;
                    matchSetupMenuIndex = 0;
                    cycleMatchSetup(1);
                    matchSetupMenuIndex = 1;
                    cycleMatchSetup(1);
                    matchSetupMenuIndex = 2;
                    cycleMatchSetup(1);
                    matchSetupMenuIndex = 3;
                    cycleMatchSetup(1);
                    matchSetupMenuIndex = 4;
                    cycleMatchSetup(1);
                    matchSettingsPassed = difficulty == Difficulty::Rookie
                        && matchDurationSeconds == 300
                        && scoreLimit == 9
                        && std::abs(ballElasticity - 0.87F) < 0.01F
                        && powerupsEnabled;
                    matchSetupMenuIndex = 0;
                    matchSetupPrepared = true;
                }
                if (matchSetupPrepared && !matchSetupCaptured && smokeElapsed >= 2.35F) {
                    TakeScreenshot("rocket_volley_match_setup_smoke.png");
                    matchSetupCaptured = true;
                    startMatch();
                    matchSettingsPassed = matchSettingsPassed && std::abs(matchTime - 300.0F) < 0.01F;
                    score = {scoreLimit - 1, 0};
                    state = MatchState::Playing;
                    scorePoint(0);
                    matchSettingsPassed = matchSettingsPassed
                        && score[0] == scoreLimit
                        && pendingGameOver;
                    difficulty = Difficulty::Pro;
                    matchDurationSeconds = 180;
                    scoreLimit = 7;
                    ballElasticity = 0.82F;
                    applyDifficultyPhysics();
                    state = MatchState::Title;
                    menuPage = MenuPage::About;
                }
                if (matchSetupCaptured && !aboutCaptured && smokeElapsed >= 2.7F) {
                    openAboutLink(0);
                    openAboutLink(1);
                    openAboutLink(2);
                    aboutLinksVerified = true;
                    TakeScreenshot("rocket_volley_about_smoke.png");
                    aboutCaptured = true;
                    preferences = {0, 0, 0};
                    audio.applyMix(preferences.musicGain(), preferences.effectsGain());
                    audio.updateMusic(true);
                    const bool muted = !audio.ready || (!IsSoundPlaying(audio.menuMusic) && !IsSoundPlaying(audio.gameMusic));
                    openPreferences(false);
                    preferences.musicVolume = 50;
                    preferences.effectsVolume = 30;
                    audio.applyMix(preferences.musicGain(), preferences.effectsGain());
                    audio.updateMusic(true);
                    preferencesTestPassed = muted && audio.musicGain == 0.5F && audio.effectsGain == 0.3F
                        && (!audio.ready || IsSoundPlaying(audio.menuMusic));
                }
                if (aboutCaptured && !preferencesCaptured && ++preferencesCaptureFrames >= 3) {
                    // Allow both back buffers to render the new menu before reading pixels.
                    TakeScreenshot("rocket_volley_preferences_smoke.png");
                    preferencesCaptured = true;
                    preferences = smokeOriginalPreferences;
                    audio.applyMix(preferences.musicGain(), preferences.effectsGain());
                    closePreferences();
                    menuPage = MenuPage::About;
                }
                if (preferencesCaptured && !arcadeCupStarted && smokeElapsed >= 2.85F) {
                    arcadeCupStarted = true;
                    menuPage = MenuPage::Main;
                    startArcadeCupRun();
                }
                if (arcadeCupStarted && !arcadeCupLoadingCaptured && smokeElapsed >= 3.15F) {
                    const int initialTitles = arcadeCupTitles;
                    TakeScreenshot("rocket_volley_arcade_cup_smoke.png");
                    arcadeCupLoadingCaptured = true;

                    const bool qualifierPassed = arcadeCupActive
                        && arcadeCupRound == 0
                        && pendingGameMode == GameMode::Match
                        && difficulty == Difficulty::Rookie
                        && matchDurationSeconds == 120
                        && scoreLimit == 3
                        && activeArenaIndex == ArcadeCupArenas[0];
                    winner = 0;
                    recordArcadeCupResult();
                    const bool qualifierWinPassed = arcadeCupMatchWins == 1 && arcadeCupBestRound >= 1;
                    advanceOrRestartArcadeCup();
                    const bool semifinalPassed = arcadeCupRound == 1
                        && pendingGameMode == GameMode::ThreeVsThree
                        && difficulty == Difficulty::Rookie
                        && activeArenaIndex == ArcadeCupArenas[1];
                    winner = 0;
                    recordArcadeCupResult();
                    advanceOrRestartArcadeCup();
                    const bool finalPassed = arcadeCupRound == 2
                        && pendingGameMode == GameMode::ThreeVsThree
                        && difficulty == Difficulty::Pro
                        && matchDurationSeconds == 180
                        && scoreLimit == 5
                        && activeArenaIndex == ArcadeCupArenas[2];
                    winner = 0;
                    recordArcadeCupResult();
                    const bool crownPassed = arcadeCupMatchWins == 3
                        && arcadeCupBestRound == 3
                        && arcadeCupTitles == initialTitles + 1;
                    arcadeCupProgressionPassed = qualifierPassed && qualifierWinPassed && semifinalPassed
                        && finalPassed && crownPassed;
                    score = {5, 2};
                    state = MatchState::GameOver;
                }
                if (arcadeCupLoadingCaptured && !arcadeCupResultsCaptured && smokeElapsed >= 3.45F) {
                    TakeScreenshot("rocket_volley_arcade_cup_results_smoke.png");
                    arcadeCupResultsCaptured = true;
                    const Difficulty expectedDifficulty = arcadeCupSavedDifficulty;
                    const int expectedDuration = arcadeCupSavedDuration;
                    const int expectedLimit = arcadeCupSavedScoreLimit;
                    const int expectedArenaSelection = arcadeCupSavedArenaSelection;
                    leaveArcadeCup();
                    arcadeCupLogicPassed = arcadeCupProgressionPassed
                        && difficulty == expectedDifficulty
                        && matchDurationSeconds == expectedDuration
                        && scoreLimit == expectedLimit
                        && arenaSelection == expectedArenaSelection;
                    state = MatchState::Title;
                    menuPage = MenuPage::Main;
                }
                if (arcadeCupResultsCaptured && !multiplayerTestRun && smokeElapsed >= 3.55F) {
                    multiplayerTestRun = true;
                    menuPage = MenuPage::Main;
                    activeArenaIndex = 2;
                    startMatch(GameMode::LocalCoop);
                    state = MatchState::Playing;
                    serveInProgress = false;
                    const Vec3 playerTwoStart = physics.transform(cars[1].body).position;
                    Controls playerTwoTestControls;
                    playerTwoTestControls.throttle = 1.0F;
                    for (int step = 0; step < 24; ++step) {
                        fixedUpdate({}, playerTwoTestControls);
                    }
                    const Vec3 playerTwoEnd = physics.transform(cars[1].body).position;
                    localCoopPassed = cars[1].human && length2D(subtract(playerTwoEnd, playerTwoStart)) > 0.08F;
                    updateLocalCoopCameras(0.5F);
                    const Vector2 playerOneScreen = GetWorldToScreenEx(
                        toRay(physics.transform(cars[0].body).position), localCoopCameras[0], ScreenWidth / 2, ScreenHeight);
                    const Vector2 playerTwoScreen = GetWorldToScreenEx(
                        toRay(physics.transform(cars[1].body).position), localCoopCameras[1], ScreenWidth / 2, ScreenHeight);
                    TraceLog(LOG_INFO, "SMOKE: split projections p1=(%.1f,%.1f) p2=(%.1f,%.1f)",
                        playerOneScreen.x, playerOneScreen.y, playerTwoScreen.x, playerTwoScreen.y);
                    splitScreenPassed = IsRenderTextureValid(localCoopTargets[0])
                        && IsRenderTextureValid(localCoopTargets[1])
                        && localCoopTargets[0].texture.width == ScreenWidth / 2
                        && localCoopTargets[1].texture.height == ScreenHeight
                        && Vector3Distance(localCoopCameras[0].position, localCoopCameras[1].position) > 2.0F
                        && playerOneScreen.y > 80.0F && playerOneScreen.y < ScreenHeight - 25.0F
                        && playerTwoScreen.y > 80.0F && playerTwoScreen.y < ScreenHeight - 25.0F;
                    multiplayerProtocolPassed = multiplayerProtocolSelfTest();

                    resetBoostPads();
                    matchBoostPickups = {0, 0};
                    difficulty = Difficulty::Pro;
                    applyDifficultyPhysics();
                    const Vec3 smallPad = BoostPadPositions[4];
                    physics.setTransform(cars[0].body, {smallPad.x, 0.62F, smallPad.z}, yawRotation(Pi));
                    physics.setLinearVelocity(cars[0].body, {});
                    cars[0].boost = 0.0F;
                    driveCar(cars[0], {}, FixedStep);
                    const bool smallCollected = std::abs(cars[0].boost - SmallBoostPickup) < 0.01F
                        && boostPadRespawnTimers[4] > SmallBoostPadRespawn - 0.1F
                        && matchBoostPickups[0] == 1;
                    physics.setTransform(cars[1].body, {smallPad.x, 0.62F, smallPad.z}, yawRotation(Pi));
                    physics.setLinearVelocity(cars[1].body, {});
                    cars[1].boost = 0.0F;
                    driveCar(cars[1], {}, FixedStep);
                    const bool sharedDepletion = cars[1].boost < 0.01F && matchBoostPickups[0] == 1;
                    tickBoostPads(SmallBoostPadRespawn + 0.1F);
                    const bool smallRespawned = boostPadRespawnTimers[4] <= 0.0F;

                    const Vec3 largePad = BoostPadPositions[2];
                    physics.setTransform(cars[0].body, {largePad.x, 0.62F, largePad.z}, yawRotation(Pi));
                    physics.setLinearVelocity(cars[0].body, {});
                    cars[0].boost = 1.0F;
                    driveCar(cars[0], {}, FixedStep);
                    const bool largeCollected = cars[0].boost >= 99.9F
                        && boostPadRespawnTimers[2] > LargeBoostPadRespawn - 0.1F
                        && matchBoostPickups[0] == 2;
                    boostPadEconomyPassed = smallCollected && sharedDepletion && smallRespawned && largeCollected;

                    boostPadRespawnTimers.fill(2.0F);
                    boostPadRespawnTimers[6] = 0.0F;
                    resetCar(cars[0], {0.0F, 0.62F, 10.0F}, Pi);
                    resetCar(cars[1], {-10.0F, 0.62F, 18.0F}, Pi);
                    cars[1].boost = 5.0F;
                    cars[1].aiThinkTimer = 0.0F;
                    rotationStriker[0] = 0;
                    serveInProgress = false;
                    physics.setTransform(ball, {0.0F, 5.0F, -10.0F}, {});
                    physics.setLinearVelocity(ball, {});
                    updateTactics(0.0F, true);
                    aiControls(cars[1], FixedStep);
                    boostAiRoutingPassed = length2D(subtract(cars[1].aiTarget, BoostPadPositions[6])) < 0.1F;

                    for (Car &car : cars) {
                        car.heldPowerup = Powerup::None;
                        car.powerupGrantTimer = 0.0F;
                        car.magnetTimer = 0.0F;
                    }
                    tickPowerupSystem(FixedStep);
                    powerupGrantPassed = cars[0].heldPowerup != Powerup::None
                        && cars[1].heldPowerup != Powerup::None
                        && cars[3].heldPowerup != Powerup::None;

                    resetCar(cars[0], {0.0F, 0.62F, 10.0F}, Pi);
                    physics.setTransform(ball, {0.0F, 2.2F, 5.0F}, {});
                    physics.setLinearVelocity(ball, {});
                    cars[0].heldPowerup = Powerup::Haymaker;
                    haymakerPassed = usePowerup(cars[0])
                        && length(physics.linearVelocity(ball)) > 24.0F
                        && matchPowerupsUsed[0] >= 1;

                    resetCar(cars[1], {-2.0F, 0.62F, 8.0F}, Pi);
                    physics.setTransform(ball, {0.0F, 3.0F, 0.0F}, {});
                    physics.setLinearVelocity(ball, {2.0F, 3.0F, 12.0F});
                    cars[1].heldPowerup = Powerup::Freezer;
                    const bool freezeActivated = usePowerup(cars[1])
                        && ballFreezeTimer > 0.9F
                        && length(physics.linearVelocity(ball)) < 0.01F;
                    tickPowerupSystem(1.1F);
                    freezerPassed = freezeActivated && ballFreezeTimer <= 0.0F
                        && length(physics.linearVelocity(ball)) > 5.0F;

                    resetCar(cars[0], {0.0F, 0.62F, 10.0F}, Pi);
                    physics.setTransform(ball, {0.0F, 2.2F, 5.5F}, {});
                    physics.setLinearVelocity(ball, {});
                    cars[0].heldPowerup = Powerup::Magnetizer;
                    const bool magnetActivated = usePowerup(cars[0]) && cars[0].magnetTimer > 2.9F;
                    tickPowerupSystem(0.25F);
                    magnetizerPassed = magnetActivated && cars[0].magnetTimer > 2.5F
                        && length(physics.linearVelocity(ball)) > 0.1F;

                    resetCar(cars[3], {-10.0F, 0.62F, -18.0F}, 0.0F);
                    resetCar(cars[4], {0.0F, 0.62F, -8.0F}, 0.0F);
                    physics.setTransform(ball, {0.0F, 2.2F, -3.0F}, {});
                    physics.setLinearVelocity(ball, {0.0F, 0.0F, 4.0F});
                    cars[4].heldPowerup = Powerup::Haymaker;
                    cars[4].aiThinkTimer = 0.0F;
                    rotationStriker[1] = 4;
                    updateTactics(0.0F, true);
                    powerupAiPassed = aiControls(cars[4], FixedStep).powerupPressed;

                    resetCar(cars[0], {0.0F, -6.0F, 0.0F}, Pi);
                    checkCarOutOfBounds();
                    const bool recoveryQueued = cars[0].respawnCause == RespawnCause::OutOfBounds
                        && cars[0].respawnTimer > OutOfBoundsRespawnDelay - 0.1F;
                    cars[0].respawnTimer = 0.001F;
                    tickCarRespawns(FixedStep);
                    const Vec3 recoveredPosition = physics.transform(cars[0].body).position;
                    outOfBoundsRecoveryPassed = recoveryQueued
                        && cars[0].respawnTimer <= 0.0F
                        && recoveredPosition.y > 0.5F
                        && std::abs(recoveredPosition.z) < ArenaHalfLength;

                    resetCar(cars[0], {0.0F, 0.62F, 3.0F}, Pi);
                    resetCar(cars[3], {0.0F, 0.62F, 0.0F}, 0.0F);
                    physics.setLinearVelocity(cars[0].body, {0.0F, 0.0F, -25.0F});
                    physics.setLinearVelocity(cars[3].body, {});
                    matchDemolitions = {0, 0};
                    checkCarDemolitions();
                    const bool demolitionQueued = cars[3].respawnCause == RespawnCause::Demolition
                        && cars[3].respawnTimer > DemolitionRespawnDelay - 0.1F
                        && matchDemolitions[0] == 1;
                    cars[3].respawnTimer = 0.001F;
                    tickCarRespawns(FixedStep);
                    demolitionPassed = demolitionQueued
                        && cars[3].respawnTimer <= 0.0F
                        && std::abs(physics.transform(cars[3].body).position.z) < ArenaHalfLength;

                    startMatch(GameMode::LocalCoop);
                    state = MatchState::Playing;
                    serveInProgress = false;
                    boostPadRespawnTimers[6] = SmallBoostPadRespawn * 0.5F;
                    cars[0].heldPowerup = Powerup::Magnetizer;
                    cars[0].magnetTimer = 2.8F;
                    cars[1].heldPowerup = Powerup::Freezer;

                    resetTouchSequence();
                    const bool firstTouch = registerTeamTouch(0) == TouchResult::Normal;
                    const bool secondTouch = registerTeamTouch(0) == TouchResult::Normal;
                    const bool thirdTouch = registerTeamTouch(0) == TouchResult::PowerReady;
                    previousBallZ = 0.25F;
                    physics.setTransform(ball, {0.0F, 3.2F, -0.25F}, {});
                    physics.setLinearVelocity(ball, {0.0F, 0.0F, -10.0F});
                    applyThirdTouchBoostIfCrossed();
                    const bool boostApplied = length(physics.linearVelocity(ball)) > 11.9F
                        && pendingThirdTouchBoostTeam == -1;
                    const bool fourthFault = registerTeamTouch(0) == TouchResult::Fault;
                    touchRulePassed = firstTouch && secondTouch && thirdTouch && boostApplied && fourthFault;
                    teamTouches = {3, 0};
                    touchNotice = "THREE TOUCH POWER!";
                    touchNoticeTimer = 5.0F;
                    thirdTouchBoostTimer = 5.0F;
                }
                if (multiplayerTestRun && !localCoopCaptured && smokeElapsed >= 3.85F) {
                    const Vec3 p2Position = physics.transform(cars[1].body).position;
                    const Vector2 p2Projection = GetWorldToScreenEx(
                        toRay(p2Position), localCoopCameras[1], ScreenWidth / 2, ScreenHeight);
                    const Vector3 p2View = Vector3Normalize(Vector3Subtract(
                        localCoopCameras[1].target, localCoopCameras[1].position));
                    const float p2Depth = Vector3DotProduct(
                        Vector3Subtract(toRay(p2Position), localCoopCameras[1].position), p2View);
                    TraceLog(LOG_INFO,
                        "SMOKE: split capture p2_world=(%.1f,%.1f,%.1f) screen=(%.1f,%.1f) depth=%.1f active=%d",
                        p2Position.x, p2Position.y, p2Position.z, p2Projection.x, p2Projection.y, p2Depth,
                        isCarActive(1));
                    TakeScreenshot("rocket_volley_local_coop_smoke.png");
                    localCoopCaptured = true;
                }
                if (localCoopCaptured && !threeVsThreeStarted && smokeElapsed >= 4.0F) {
                    menuPage = MenuPage::Main;
                    arenaSelection = 0;
                    activeArenaIndex = 0;
                    chooseArenaForSession();
                    arenaTestPassed = ArenaThemes.size() >= 4 && activeArenaIndex != 0;
                    arenaSelection = 2;
                    activeArenaIndex = 1;
                    difficulty = Difficulty::Pro;
                    beginLoadingThreeVsThree();
                    threeVsThreeStarted = true;
                }
                if (threeVsThreeStarted && !threeVsThreeLoadingSeen && state == MatchState::Loading
                    && pendingGameMode == GameMode::ThreeVsThree && loadingTimer >= 0.35F) {
                    threeVsThreeLoadingSeen = true;
                    loadingTimer = 1.48F;
                }
                if (threeVsThreeStarted && !threeVsThreePassed && state == MatchState::ServeCountdown
                    && gameMode == GameMode::ThreeVsThree && serveCountdown <= 2.55F) {
                    const Vec3 landing = serveLandingTarget;
                    const int blueStriker = strikerForTeam(0, landing);
                    const int blueSupport = supportForTeam(0, blueStriker, landing);
                    aiControls(cars[1], deltaSeconds);
                    aiControls(cars[2], deltaSeconds);
                    const auto snapshot = net::decodeWorldSnapshot(net::encodeWorldSnapshot(makeWorldSnapshot()));
                    int activeCount = 0;
                    for (int index = 0; index < static_cast<int>(cars.size()); ++index) {
                        activeCount += isCarActive(index) ? 1 : 0;
                    }
                    threeVsThreePassed = activeCount == 6
                        && blueStriker >= 0 && blueStriker < 3
                        && blueSupport >= 0 && blueSupport < 3 && blueSupport != blueStriker
                        && servingCar >= 3 && servingCar < 6
                        && length2D(subtract(cars[1].aiTarget, cars[2].aiTarget)) > 2.5F
                        && snapshot && snapshot->gameMode == static_cast<std::uint8_t>(GameMode::ThreeVsThree)
                        && snapshot->cars.size() == MaximumCars;
                    serveCountdown = 0.18F;
                }
                if (threeVsThreePassed && !threeVsThreeCaptured && state == MatchState::Playing
                    && gameMode == GameMode::ThreeVsThree && rallyTime >= 0.65F) {
                    TakeScreenshot("rocket_volley_3v3_smoke.png");
                    threeVsThreeCaptured = true;
                    automatedPlayer = true;
                    difficulty = Difficulty::Rookie;
                    trainingFeed = TrainingFeed::Lob;
                    applyDifficultyPhysics();
                    beginLoadingTraining();
                }
                if (!trainingLoadingCaptured && state == MatchState::Loading
                    && pendingGameMode == GameMode::Training && loadingTimer >= 0.35F) {
                    TakeScreenshot("rocket_volley_training_loading_smoke.png");
                    trainingLoadingCaptured = true;
                    loadingTimer = 1.48F;
                }
                if (!trainingCountdownCaptured && state == MatchState::ServeCountdown
                    && gameMode == GameMode::Training && serveCountdown <= 2.55F) {
                    const std::string resetPrompt = keyName(boundKey(BindAction::Restart)) + " RESET SERVE";
                    uiTextFitPassed = MeasureText("PRACTICE FEED", 27) <= 338
                        && MeasureText("BALL INCOMING", 21) <= 338
                        && MeasureText(resetPrompt.c_str(), fittedFontSize(resetPrompt, 338, 18, 15)) <= 338;
                    TakeScreenshot("rocket_volley_training_countdown_smoke.png");
                    trainingCountdownCaptured = true;
                    serveCountdown = 0.18F;
                }
                if (!trainingPlayCaptured && state == MatchState::Playing
                    && gameMode == GameMode::Training && rallyTime >= 1.85F) {
                    TakeScreenshot("rocket_volley_training_smoke.png");
                    trainingPlayCaptured = true;
                }
                if (!trainingGroundPassed && state == MatchState::Playing
                    && gameMode == GameMode::Training && trainingBallHasTouchedGround) {
                    trainingGroundPassed = score[0] == 0 && score[1] == 0;
                    physics.setLinearVelocity(ball, {30.0F, 0.0F, 0.0F});
                    difficulty = Difficulty::Rookie;
                    applyDifficultyPhysics();
                    applyRookieBallAssist();
                    rookieSpeedPassed = length(physics.linearVelocity(ball)) <= 15.01F;
                    difficulty = Difficulty::Pro;
                    applyDifficultyPhysics();
                    physics.setLinearVelocity(ball, {30.0F, 0.0F, 0.0F});
                    applyRookieBallAssist();
                    proSpeedPassed = length(physics.linearVelocity(ball)) >= 29.99F;
                    trainingFeed = TrainingFeed::Lob;
                    resetTrainingServe();
                    const float lobFeedForward = std::abs(serveVelocity.z);
                    trainingFeed = TrainingFeed::Fast;
                    resetTrainingServe();
                    const float fastFeedForward = std::abs(serveVelocity.z);
                    trainingFeed = TrainingFeed::CrossCourt;
                    resetTrainingServe();
                    const float crossFeedX = std::abs(serveVelocity.x);
                    trainingFeedsPassed = fastFeedForward > lobFeedForward + 2.0F && crossFeedX > 4.0F;
                    trainingFeed = TrainingFeed::Mixed;
                    resetTrainingServe();
                    trainingResetPassed = state == MatchState::ServeCountdown
                        && gameMode == GameMode::Training
                        && rallyTouches == 0;
                    academyOriginalBestMedal = academyBestMedal;
                    academyOriginalBestTime = academyBestTimeCentiseconds;
                    academyOriginalCompletions = academyCompletions;
                    academyOriginalGhost = academyBestGhost;
                    academyOriginalGhostSaved = academyGhostSaved;
                    academyOriginalGhostEnabled = academyGhostEnabled;
                    const std::vector<AcademyGhostFrame> seededGhost{
                        {0.00F, AcademyLesson::BoostGates, {{0.0F, 0.62F, 19.0F}, yawRotation(Pi)}},
                        {1.00F, AcademyLesson::BoostGates, {{0.0F, 0.62F, 13.0F}, yawRotation(Pi)}},
                        {10.0F, AcademyLesson::DoubleJump, {{0.0F, 0.62F, 10.8F}, yawRotation(Pi)}},
                        {16.0F, AcademyLesson::DoubleJump, {{0.0F, 5.6F, 6.0F}, yawRotation(Pi)}},
                        {25.0F, AcademyLesson::AerialReturn, {{-2.0F, 0.62F, 8.0F}, yawRotation(Pi)}},
                        {34.0F, AcademyLesson::AerialReturn, {{0.0F, 4.0F, -1.0F}, yawRotation(Pi)}},
                        {44.0F, AcademyLesson::TargetLanding, {{3.0F, 0.62F, 8.0F}, yawRotation(Pi)}},
                        {60.0F, AcademyLesson::TargetLanding, {{-4.0F, 0.62F, -10.5F}, yawRotation(Pi)}}};
                    const std::string encodedGhost = encodeAcademyGhost(seededGhost, 6000);
                    std::vector<AcademyGhostFrame> decodedGhost;
                    int decodedGhostTime = 0;
                    std::vector<AcademyGhostFrame> corruptGhost;
                    int corruptGhostTime = 0;
                    academyGhostCodecPassed = !encodedGhost.empty()
                        && decodeAcademyGhost(encodedGhost, decodedGhost, decodedGhostTime)
                        && decodedGhostTime == 6000
                        && decodedGhost.size() == seededGhost.size()
                        && !decodeAcademyGhost(encodedGhost + "CORRUPT", corruptGhost, corruptGhostTime);
                    if (academyGhostCodecPassed) {
                        academyBestGhost = std::move(decodedGhost);
                        academyBestTimeCentiseconds = decodedGhostTime;
                        academyBestMedal = 3;
                        academyGhostEnabled = true;
                        academyGhostSaved = true;
                        academyActive = true;
                        academyLesson = AcademyLesson::TargetLanding;
                        academyRecordEligible = false;
                        academyRetries = 1;
                        academyRunTimer = 1.0F;
                        academyCurrentGhost.clear();
                        state = MatchState::Playing;
                        const int completionsBeforeSkip = academyCompletions;
                        completeAcademyRun();
                        academySkipIntegrityPassed = !academyNewRecord
                            && academyRunMedal == 0
                            && academyBestTimeCentiseconds == 6000
                            && academyBestGhost.size() == seededGhost.size()
                            && academyCompletions == completionsBeforeSkip;
                    }
                    arenaSelection = 1;
                    activeArenaIndex = 0;
                    beginLoadingAcademy();
                }
                if (!academyLoadingCaptured && academyActive && state == MatchState::Loading
                    && pendingGameMode == GameMode::Training && loadingTimer >= 0.35F) {
                    TakeScreenshot("rocket_volley_academy_loading_smoke.png");
                    academyLoadingCaptured = true;
                    loadingTimer = 1.48F;
                }
                if (!academyCountdownCaptured && academyActive && state == MatchState::ServeCountdown
                    && academyLesson == AcademyLesson::BoostGates && serveCountdown <= 2.25F) {
                    TakeScreenshot("rocket_volley_academy_countdown_smoke.png");
                    academyCountdownCaptured = true;
                    serveCountdown = 0.18F;
                }
                if (!academyPlayCaptured && academyActive && state == MatchState::Playing
                    && academyLesson == AcademyLesson::BoostGates && rallyTime >= 0.18F) {
                    TakeScreenshot("rocket_volley_academy_smoke.png");
                    academyPlayCaptured = true;
                    Transform playbackTransform;
                    academyGhostPlaybackPassed = academyGhostTransform(playbackTransform)
                        && std::isfinite(playbackTransform.position.x)
                        && playbackTransform.position.z < 19.0F
                        && playbackTransform.position.z > 12.5F;

                    academyBoostUsed = true;
                    for (const Vec3 &gate : AcademyGatePositions) {
                        physics.setTransform(cars[0].body, {gate.x, 0.62F, gate.z}, yawRotation(Pi));
                        academyFixedUpdate();
                    }
                    const bool gatesPassed = academyGateIndex == static_cast<int>(AcademyGatePositions.size())
                        && academyTransitionTimer > 0.0F;
                    academyTransitionTimer = FixedStep;
                    academyFixedUpdate();

                    state = MatchState::Playing;
                    academyBoostUsed = true;
                    cars[0].jumpsUsed = 2;
                    physics.setTransform(cars[0].body, {0.0F, 5.6F, 6.0F}, yawRotation(Pi));
                    academyFixedUpdate();
                    const bool jumpPassed = academyLesson == AcademyLesson::DoubleJump
                        && academyPeakHeight >= 5.5F && academyTransitionTimer > 0.0F;
                    academyTransitionTimer = FixedStep;
                    academyFixedUpdate();

                    state = MatchState::Playing;
                    rallyTouches = 1;
                    academyAerialTouch = true;
                    trainingReturnSuccessful = false;
                    previousBallZ = 1.0F;
                    physics.setTransform(ball, {0.0F, 4.2F, -1.0F}, {});
                    physics.setLinearVelocity(ball, {});
                    academyFixedUpdate();
                    const bool returnPassed = academyLesson == AcademyLesson::AerialReturn
                        && trainingReturnSuccessful && academyTransitionTimer > 0.0F;
                    academyTransitionTimer = FixedStep;
                    academyFixedUpdate();

                    state = MatchState::Playing;
                    trainingReturnSuccessful = true;
                    physics.setTransform(ball, {challengeTarget.x + 0.35F, BallRadius - 0.01F, challengeTarget.z + 0.25F}, {});
                    physics.setLinearVelocity(ball, {});
                    academyFixedUpdate();
                    const bool targetPassed = academyLesson == AcademyLesson::TargetLanding
                        && academyTransitionTimer > 0.0F;
                    academyTransitionTimer = FixedStep;
                    academyFixedUpdate();
                    academyObjectivesPassed = gatesPassed && jumpPassed && returnPassed && targetPassed
                        && state == MatchState::GameOver && academyLessonsCompleted == AcademyLessonCount
                        && academyRunMedal == 3;
                    academyPersistencePassed = academyBestMedal >= 3
                        && academyBestTimeCentiseconds > 0
                        && academyCompletions == academyOriginalCompletions + 1;
                    int recordedLessonMask = 0;
                    for (const AcademyGhostFrame &frame : academyBestGhost) {
                        recordedLessonMask |= 1 << static_cast<int>(frame.lesson);
                    }
                    academyGhostRecordPassed = academyGhostSaved
                        && academyBestTimeCentiseconds < 6000
                        && !academyBestGhost.empty()
                        && academyBestGhost.size() == academyCurrentGhost.size()
                        && recordedLessonMask == (1 << AcademyLessonCount) - 1;
                    academyResultsDelayFrames = 2;
                }
                if (academyObjectivesPassed && !academyResultsCaptured && academyActive
                    && state == MatchState::GameOver) {
                    if (academyResultsDelayFrames > 0) {
                        --academyResultsDelayFrames;
                    } else {
                        TakeScreenshot("rocket_volley_academy_results_smoke.png");
                        academyResultsCaptured = true;
                        academyBestMedal = academyOriginalBestMedal;
                        academyBestTimeCentiseconds = academyOriginalBestTime;
                        academyCompletions = academyOriginalCompletions;
                        academyBestGhost = academyOriginalGhost;
                        academyGhostSaved = academyOriginalGhostSaved;
                        academyGhostEnabled = academyOriginalGhostEnabled;
                        saveSettings("SMOKE SETTINGS RESTORED");
                        arenaSelection = 4;
                        activeArenaIndex = 3;
                        beginLoadingTargetChallenge();
                    }
                }
                if (!challengeLoadingCaptured && state == MatchState::Loading
                    && pendingGameMode == GameMode::TargetChallenge && loadingTimer >= 0.35F) {
                    TakeScreenshot("rocket_volley_target_loading_smoke.png");
                    challengeLoadingCaptured = true;
                    loadingTimer = 1.48F;
                }
                if (!challengeCountdownCaptured && state == MatchState::ServeCountdown
                    && gameMode == GameMode::TargetChallenge && serveCountdown <= 2.55F) {
                    challengeCountdownCaptured = challengeTarget.z < 0.0F
                        && trainingAttempts == 1
                        && currentTrainingFeed == TrainingFeed::Lob;
                    serveCountdown = 0.18F;
                }
                if (!challengePlayCaptured && state == MatchState::Playing
                    && gameMode == GameMode::TargetChallenge && rallyTime >= 0.65F) {
                    TakeScreenshot("rocket_volley_target_challenge_smoke.png");
                    challengePlayCaptured = true;
                    trainingReturnSuccessful = true;
                    rallyTouches = 1;
                    resolvePracticeLanding({challengeTarget.x + 0.4F, BallRadius, challengeTarget.z + 0.3F});
                    trainingAttempts = TargetChallengeShots;
                    trainingResetTimer = 0.001F;
                    fixedUpdate({});
                    challengeScoringPassed = challengeScore == 100
                        && challengeTargetsHit == 1
                        && challengeBestCombo == 1
                        && challengeBestScore == 100
                        && challengeNewRecord
                        && state == MatchState::GameOver;
                }
                else if (challengeScoringPassed && !challengeResultsCaptured
                    && state == MatchState::GameOver && gameMode == GameMode::TargetChallenge) {
                    TakeScreenshot("rocket_volley_target_results_smoke.png");
                    challengeResultsCaptured = true;
                    beginLoadingMatch();
                    matchStarted = true;
                }
                if (matchStarted && !loadingCaptured && state == MatchState::Loading
                    && pendingGameMode == GameMode::Match && loadingTimer >= 0.35F) {
                    TakeScreenshot("rocket_volley_loading_smoke.png");
                    loadingCaptured = true;
                    loadingTimer = 1.48F;
                }
                if (matchStarted && !countdownCaptured && state == MatchState::ServeCountdown
                    && gameMode == GameMode::Match && serveCountdown <= 2.55F) {
                    automatedPlayer = false;
                    teammateLanePassed = strikerForTeam(0, serveLandingTarget) == 0;
                    aiControls(cars[1], deltaSeconds);
                    teammateLanePassed = teammateLanePassed
                        && supportForTeam(0, 0, serveLandingTarget) == 1
                        && cars[1].aiTarget.z > 1.5F;
                    automatedPlayer = true;
                    TakeScreenshot("rocket_volley_countdown_smoke.png");
                    countdownCaptured = true;
                    serveCountdown = 0.18F;
                }
                if (!trueServeTossPassed && state == MatchState::Playing
                    && gameMode == GameMode::Match && rallyTime < 0.45F) {
                    const Vec3 initialServeVelocity = physics.linearVelocity(ball);
                    trueServeTossPassed = serveInProgress
                        && std::abs(initialServeVelocity.z) < 1.0F
                        && initialServeVelocity.y > 0.0F;
                }
                if (state == MatchState::Playing && gameMode == GameMode::Match
                    && rallyTouches >= 1 && !serveInProgress) {
                    trueServeContactPassed = true;
                }
                if (matchStarted && gameMode == GameMode::Match
                    && !carCameraCaptured && state == MatchState::Playing && rallyTouches >= 1) {
                    cameraMode = CameraMode::Car;
                    TakeScreenshot("rocket_volley_gameplay_car_smoke.png");
                    carCameraCaptured = true;
                }
                if (carCameraCaptured && gameMode == GameMode::Match
                    && !ballCameraActivated && state == MatchState::Playing) {
                    cameraMode = CameraMode::Ball;
                    cameraModeNotice = 1.5F;
                    ballCameraActivated = true;
                    ballCameraElapsed = 0.0F;
                }
                if (ballCameraActivated && !ballCameraCaptured) {
                    ballCameraElapsed += deltaSeconds;
                    if (state == MatchState::Playing && gameMode == GameMode::Match && ballCameraElapsed >= 1.0F) {
                        const Vec3 playerPosition = physics.transform(cars[0].body).position;
                        const Vec3 ballPosition = physics.transform(ball).position;
                        const Vector2 playerScreen = GetWorldToScreen(toRay(playerPosition), camera);
                        const Vector2 ballScreen = GetWorldToScreen(toRay(ballPosition), camera);
                        const float horizontalAnchorDistance = length2D(subtract(
                            {camera.position.x, camera.position.y, camera.position.z}, playerPosition));
                        ballCameraFramingPassed = horizontalAnchorDistance >= 6.5F
                            && horizontalAnchorDistance <= 12.5F
                            && std::abs(playerScreen.x - static_cast<float>(ScreenWidth) * 0.5F) < 125.0F
                            && playerScreen.y > 100.0F && playerScreen.y < static_cast<float>(ScreenHeight - 35)
                            && ballScreen.x >= 0.0F && ballScreen.x <= static_cast<float>(ScreenWidth)
                            && ballScreen.y >= 0.0F && ballScreen.y <= static_cast<float>(ScreenHeight)
                            && camera.fovy >= 60.0F && camera.fovy <= 83.0F;
                        TraceLog(LOG_INFO,
                            "SMOKE: ball_camera_rig=%d anchor=%.2f player=(%.1f,%.1f) ball=(%.1f,%.1f)",
                            ballCameraFramingPassed,
                            horizontalAnchorDistance,
                            playerScreen.x,
                            playerScreen.y,
                            ballScreen.x,
                            ballScreen.y);
                        TakeScreenshot("rocket_volley_gameplay_ball_smoke.png");
                        ballCameraCaptured = true;
                    }
                }
                if (ballCameraCaptured && !replayTriggered && state == MatchState::Playing
                    && replayFrames.size() >= 30) {
                    scorePoint(0);
                    replayTriggered = true;
                }
                if (replayTriggered && !replayCaptured && state == MatchState::GoalReplay && ++replayCaptureFrames >= 3) {
                    replayPassed = currentReplayFrame() != nullptr && replayFrames.size() >= 30;
                    TakeScreenshot("rocket_volley_replay_smoke.png");
                    replayCaptured = true;
                    finishPointSequence();
                }
                // Rally quality is measured by seeded headless matches. Do not make graphics coverage
                // depend on random AI reaching a third touch before this real-time capture deadline.
                if (!aerialTestStarted && ballCameraCaptured && replayCaptured) {
                    scriptedAerialTest = true;
                    serveInProgress = false;
                    aerialTestStarted = true;
                    aerialTestTimer = 0.0F;
                    aerialPeakHeight = 0.0F;
                    aerialPeakForwardY = 0.0F;
                    aerialMaxJumpsUsed = 0;
                    aerialFirstJumpTriggered = false;
                    aerialSecondJumpTriggered = false;
                    aerialTestComplete = false;
                    cameraMode = CameraMode::Car;
                    // This flight fixture tests controls, independently of the current
                    // competitive serve (which can now occupy the same launch lane).
                    servePosition = {0.0F, 12.0F, -16.0F};
                    serveCountdown = 99.0F;
                    countdownCue = 0;
                    state = MatchState::ServeCountdown;
                    accumulator = 0.0F;
                    resetCar(cars[0], {0.0F, 0.62F, 10.0F}, Pi);
                    resetCar(cars[1], {-10.0F, 0.62F, 18.0F}, Pi);
                    resetCar(cars[3], {-10.0F, 0.62F, -18.0F}, 0.0F);
                    resetCar(cars[4], {10.0F, 0.62F, -18.0F}, 0.0F);
                    parkInactiveCars();
                }
                if (aerialTestStarted && !aerialCaptured && aerialTestTimer >= 1.85F) {
                    TakeScreenshot("rocket_volley_aerial_smoke.png");
                    aerialCaptured = true;
                }
                if (!sprintTestStarted && aerialTestComplete && aerialCaptured) {
                    scriptedAerialTest = false;
                    scriptedSprintTest = true;
                    serveInProgress = false;
                    sprintTestStarted = true;
                    sprintTestTimer = 0.0F;
                    sprintFinishTime = 0.0F;
                    sprintTestComplete = false;
                    sprintCompletedCourse = false;
                    cameraMode = CameraMode::Car;
                    serveCountdown = 99.0F;
                    countdownCue = 0;
                    state = MatchState::ServeCountdown;
                    accumulator = 0.0F;
                    resetCar(cars[0], {0.0F, 0.62F, 19.0F}, Pi);
                    resetCar(cars[1], {-10.0F, 0.62F, 19.0F}, Pi);
                    resetCar(cars[3], {-10.0F, 0.62F, -19.0F}, 0.0F);
                    resetCar(cars[4], {10.0F, 0.62F, -19.0F}, 0.0F);
                    parkInactiveCars();
                }
                if (sprintTestComplete && !careerProgressionPassed) {
                    const int xpBeforeMilestones = careerXp;
                    careerBestRally = std::max(careerBestRally, 6);
                    careerAerials = std::max(careerAerials, 5);
                    careerBoostPads = std::max(careerBoostPads, 25);
                    careerPowerups = std::max(careerPowerups, 5);
                    challengeBestScore = std::max(challengeBestScore, 500);
                    academyCompletions = std::max(academyCompletions, 1);
                    academyBestMedal = std::max(academyBestMedal, 3);
                    arcadeCupTitles = std::max(arcadeCupTitles, 1);
                    const int milestoneXp = unlockCareerMilestones();
                    careerProgressionPassed = careerMatches >= 1
                        && careerWins >= 1
                        && careerXp > xpBeforeMilestones
                        && milestoneXp > 0
                        && careerMilestones == (1 << CareerMilestoneCount) - 1
                        && unlockedCareerMilestoneCount() == CareerMilestoneCount
                        && careerRankIndex() >= 1;
                }
                if ((sprintTestComplete && careerProgressionPassed) || smokeElapsed >= 40.0F) {
                    break;
                }
            }
        }
        if (smokeTestMode) {
            bestRallyTouches = std::max(bestRallyTouches, rallyTouches);
            // Render the same fixed-layout canvas into a non-16:9 window, then
            // inspect scenery from outside the cage in both player viewports.
            const bool resizeEnabled = IsWindowState(FLAG_WINDOW_RESIZABLE);
            SetWindowSize(960, 800);
            for (int frame = 0; frame < 3; ++frame) draw();
            const bool resized = GetScreenWidth() == 960 && GetScreenHeight() == 800;
            TakeScreenshot("rocket_volley_resized_smoke.png");
            SetWindowSize(ScreenWidth, ScreenHeight);
            scriptedAerialTest = scriptedSprintTest = false;
            state = MatchState::Playing;
            gameMode = GameMode::LocalCoop;
            academyActive = false;
            activeArenaIndex = 0;
            resetCar(cars[0], {10.0F, 0.5F, 12.0F}, Pi);
            resetCar(cars[1], {5.0F, 0.5F, 10.0F}, Pi);
            physics.setTransform(ball, {0.0F, 5.0F, 0.0F}, {});
            localCoopCameras[0] = {{21.0F, 5.0F, 13.0F}, {7.0F, 1.5F, 8.0F}, {0.0F, 1.0F, 0.0F}, 68.0F, CAMERA_PERSPECTIVE};
            localCoopCameras[1] = {{-6.0F, 8.0F, 18.0F}, {5.0F, 1.5F, 10.0F}, {0.0F, 1.0F, 0.0F}, 68.0F, CAMERA_PERSPECTIVE};
            for (int frame = 0; frame < 3; ++frame) draw();
            TakeScreenshot("rocket_volley_obstruction_smoke.png");
            TraceLog(LOG_INFO, "SMOKE: resizable=%d resized_canvas=%d", resizeEnabled, resized);
            if (!resizeEnabled || !resized) return 2;
            TraceLog(
                LOG_INFO,
                "SMOKE: best rally touches=%d current rally=%d score=%d-%d",
                bestRallyTouches,
                rallyTouches,
                score[0],
                score[1]);
            TraceLog(
                LOG_INFO,
                "SMOKE: aerial peak=%.2f forward_y=%.2f max_jumps_used=%d",
                aerialPeakHeight,
                aerialPeakForwardY,
                aerialMaxJumpsUsed);
            TraceLog(
                LOG_INFO,
                "SMOKE: boosted backline-to-net sprint completed=%s time=%.2f",
                sprintCompletedCourse ? "true" : "false",
                sprintFinishTime);
            TraceLog(
                LOG_INFO,
                "SMOKE: training=%d%d%d reset=%d speeds=%d%d arenas=%d teammate=%d true_serve=%d%d ball_radius=%.2f",
                trainingLoadingCaptured,
                trainingCountdownCaptured,
                trainingGroundPassed,
                trainingResetPassed,
                rookieSpeedPassed,
                proSpeedPassed,
                arenaTestPassed,
                teammateLanePassed,
                trueServeTossPassed,
                trueServeContactPassed,
                BallRadius);
            TraceLog(
                LOG_INFO,
                "SMOKE: environments=forest+beach+city+canyon city_capture=%d multiplayer_protocol=%d local_coop=%d local_capture=%d three_touch_rule=%d",
                cityArenaCaptured,
                multiplayerProtocolPassed,
                localCoopPassed,
                localCoopCaptured,
                touchRulePassed);
            TraceLog(LOG_INFO, "SMOKE: three_vs_three=%d%d%d training_feeds=%d instant_replay=%d",
                threeVsThreeLoadingSeen, threeVsThreeCaptured, threeVsThreePassed, trainingFeedsPassed, replayPassed);
            TraceLog(LOG_INFO, "SMOKE: rocket_academy=%d%d%d%d%d persistence=%d medal=%s",
                academyLoadingCaptured, academyCountdownCaptured, academyPlayCaptured,
                academyObjectivesPassed, academyResultsCaptured, academyPersistencePassed,
                academyMedalName(academyRunMedal));
            TraceLog(LOG_INFO, "SMOKE: academy_pb_ghost codec=%d playback=%d record=%d skip_guard=%d frames=%d",
                academyGhostCodecPassed, academyGhostPlaybackPassed, academyGhostRecordPassed, academySkipIntegrityPassed,
                static_cast<int>(academyCurrentGhost.size()));
            TraceLog(
                LOG_INFO,
                "SMOKE: target_challenge=%d%d%d%d%d score=%d best_combo=%d",
                challengeLoadingCaptured,
                challengeCountdownCaptured,
                challengePlayCaptured,
                challengeScoringPassed,
                challengeResultsCaptured,
                challengeScore,
                challengeBestCombo);
            TraceLog(LOG_INFO, "SMOKE: match_setup=%d%d format=%dsec/first%d",
                matchSetupCaptured, matchSettingsPassed, matchDurationSeconds, scoreLimit);
            TraceLog(LOG_INFO, "SMOKE: boost_economy=%d ai_pad_route=%d protocol_v=%d",
                boostPadEconomyPassed, boostAiRoutingPassed, net::ProtocolVersion);
            TraceLog(LOG_INFO, "SMOKE: power_volley=%d%d%d%d%d enabled=%d",
                powerupGrantPassed, haymakerPassed, freezerPassed, magnetizerPassed, powerupAiPassed,
                powerupsEnabled);
            TraceLog(LOG_INFO, "SMOKE: local_split_screen=%d targets=%dx%d",
                splitScreenPassed, localCoopTargets[0].texture.width, localCoopTargets[0].texture.height);
            TraceLog(LOG_INFO, "SMOKE: recovery=%d demolition=%d customization=%d ui_text_fit=%d",
                outOfBoundsRecoveryPassed, demolitionPassed, customizationPassed, uiTextFitPassed);
            TraceLog(LOG_INFO, "SMOKE: arcade_cup=%d%d%d best=%d/3 crowns=%d",
                arcadeCupLoadingCaptured, arcadeCupResultsCaptured, arcadeCupLogicPassed,
                arcadeCupBestRound, arcadeCupTitles);
            TraceLog(LOG_INFO, "SMOKE: pilot_record=%d rank=%s xp=%d matches=%d wins=%d milestones=%d/%d",
                careerProgressionPassed, careerRankName(), careerXp, careerMatches, careerWins,
                unlockedCareerMilestoneCount(), CareerMilestoneCount);
            if (!menuCaptured || !cityArenaCaptured || !customizeCaptured || !customizationPassed
                || !controlsCaptured || !aboutCaptured || !preferencesTestPassed || !preferencesCaptured
                || !matchSetupCaptured || !matchSettingsPassed
                || !bindingTestPassed || !aboutLinksVerified || !loadingCaptured || !countdownCaptured
                || !careerProgressionPassed
                || !arcadeCupLoadingCaptured || !arcadeCupResultsCaptured || !arcadeCupLogicPassed
                || !multiplayerProtocolPassed || !localCoopPassed || !localCoopCaptured
                || !splitScreenPassed || !outOfBoundsRecoveryPassed || !demolitionPassed
                || !boostPadEconomyPassed || !boostAiRoutingPassed
                || !powerupGrantPassed || !haymakerPassed || !freezerPassed || !magnetizerPassed || !powerupAiPassed
                || !threeVsThreeLoadingSeen || !threeVsThreeCaptured || !threeVsThreePassed || !touchRulePassed
                || !trainingLoadingCaptured || !trainingCountdownCaptured || !trainingPlayCaptured
                || !trainingGroundPassed || !trainingResetPassed || !trainingFeedsPassed || !uiTextFitPassed
                || !rookieSpeedPassed || !proSpeedPassed
                || !academyLoadingCaptured || !academyCountdownCaptured || !academyPlayCaptured
                || !academyObjectivesPassed || !academyResultsCaptured || !academyPersistencePassed
                || !academyGhostCodecPassed || !academyGhostPlaybackPassed || !academyGhostRecordPassed || !academySkipIntegrityPassed
                || !challengeLoadingCaptured || !challengeCountdownCaptured || !challengePlayCaptured
                || !challengeScoringPassed || !challengeResultsCaptured
                || !arenaTestPassed || !teammateLanePassed || !trueServeTossPassed || !trueServeContactPassed
                || BallRadius < 1.07F
                || !carCameraCaptured || !ballCameraCaptured || !ballCameraFramingPassed
                || !replayPassed || !aerialCaptured || !aerialTestComplete
                || aerialPeakHeight < 6.0F || aerialPeakForwardY < 0.3F
                || aerialMaxJumpsUsed != 2 || !sprintCompletedCourse || sprintFinishTime > 2.2F) {
                TraceLog(
                    LOG_ERROR,
                    "SMOKE: gate failed captures=%d%d%d%d%d%d%d%d%d camera=%d training=%d%d%d%d speeds=%d%d arena=%d teammate=%d serve=%d%d binding=%d links=%d rally=%d aerial=(%.2f,%.2f,%d) sprint=(%d,%.2f)",
                    menuCaptured,
                    customizeCaptured,
                    controlsCaptured,
                    aboutCaptured,
                    loadingCaptured,
                    countdownCaptured,
                    carCameraCaptured,
                    ballCameraCaptured,
                    aerialCaptured,
                    ballCameraFramingPassed,
                    trainingLoadingCaptured,
                    trainingCountdownCaptured,
                    trainingPlayCaptured,
                    trainingGroundPassed && trainingResetPassed,
                    rookieSpeedPassed,
                    proSpeedPassed,
                    arenaTestPassed,
                    teammateLanePassed,
                    trueServeTossPassed,
                    trueServeContactPassed,
                    bindingTestPassed,
                    aboutLinksVerified,
                    bestRallyTouches,
                    aerialPeakHeight,
                    aerialPeakForwardY,
                    aerialMaxJumpsUsed,
                    sprintCompletedCourse,
                    sprintFinishTime);
                return 2;
            }
        }
        return 0;
    }
};

#include "../tests/GameHeadlessTests.inl"

Game::Game(bool smokeTest, bool headlessTest) : impl_(std::make_unique<Impl>(smokeTest, headlessTest)) {}

Game::~Game() = default;

int Game::run() {
    return impl_->run();
}

} // namespace rv
