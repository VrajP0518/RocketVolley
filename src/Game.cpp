#include "rocket_volley/Game.hpp"

#include "rocket_volley/GameplayLogic.hpp"
#include "rocket_volley/MathTypes.hpp"
#include "rocket_volley/MultiplayerProtocol.hpp"
#include "rocket_volley/PhysicsWorld.hpp"

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
#include <filesystem>
#include <fstream>
#include <functional>
#include <numbers>
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
constexpr float FixedStep = 1.0F / 120.0F;
constexpr float PowerupInitialDelay = 8.0F;
constexpr float PowerupRecharge = 15.0F;
constexpr float PowerupMaximumTimer = 20.0F;
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
    Sound hit{};
    Sound jump{};
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
        SetSoundVolume(hit, 0.72F);
        SetSoundVolume(jump, 0.55F);
        SetSoundVolume(score, 0.7F);
        SetSoundVolume(boost, 0.32F);
        SetSoundVolume(menuMove, 0.38F);
        SetSoundVolume(countdown, 0.5F);
        SetSoundVolume(menuMusic, 0.28F);
        SetSoundVolume(gameMusic, 0.22F);
    }

    void updateMusic(bool menuActive) const {
        if (!ready) {
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
        if (ready) {
            PlaySound(sound);
        }
    }

    void playHit(float strength) const {
        if (!ready) {
            return;
        }
        const float normalized = clamp(strength / 18.0F, 0.0F, 1.0F);
        SetSoundVolume(hit, 0.24F + normalized * 0.66F);
        SetSoundPitch(hit, 0.82F + normalized * 0.34F);
        PlaySound(hit);
    }

    void shutdown() {
        if (!ready) {
            return;
        }
        UnloadSound(hit);
        UnloadSound(jump);
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

enum class MenuPage {
    Main,
    MatchSetup,
    Customize,
    Controls,
    About,
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
    Vec3{0.0F, 0.08F, 13.0F}, Vec3{-5.0F, 0.08F, 5.0F}, Vec3{4.5F, 0.08F, -4.0F}};

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
    "FORWARD DODGE",
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
    float aiThinkTimer = 0.0F;
    float touchCooldown = 0.0F;
    float groundedGrace = 0.0F;
    float jumpBuffer = 0.0F;
    Vec3 aiTarget{};
    int jumpsUsed = 0;
    bool dodgeAvailable = true;
    Powerup heldPowerup = Powerup::None;
    float powerupGrantTimer = PowerupInitialDelay;
    float magnetTimer = 0.0F;
    Color paint = WHITE;
    Color wheelColor{18, 20, 25, 255};
    Color spoilerColor = GOLD;
};

struct ColorChoice {
    const char *name;
    Color color;
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
    Model cubeModel{};
    Model ballModel{};
    RenderTexture2D previewTarget{};
    std::array<RenderTexture2D, 2> localCoopTargets{};
    BodyHandle ball = InvalidBody;
    std::array<Car, MaximumCars> cars{};
    std::vector<Particle> particles;
    std::vector<TrailPoint> ballTrail;
    std::vector<ReplayFrame> replayFrames;
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
    float aiBallTouchCooldown = 0.0F;
    float rallyTouchCooldown = 0.0F;
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
    int bodyColorIndex = 0;
    int wheelColorIndex = 0;
    int spoilerColorIndex = 0;
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
    int rallyTouches = 0;
    int bestRallyTouches = 0;
    int trainingAttempts = 0;
    int trainingReturns = 0;
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

    explicit Impl(bool smokeTest) : smokeTestMode(smokeTest) {
        unsigned int windowFlags = FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT;
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
        SetTargetFPS(120);
        audio.initialize();

        cubeModel = LoadModelFromMesh(GenMeshCube(1.0F, 1.0F, 1.0F));
        ballModel = LoadModelFromMesh(GenMeshSphere(BallRadius, 12, 8));
        previewTarget = LoadRenderTexture(560, 380);
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
                || lesson < previousLesson || frame.time < previousTime || frame.time > 600.0F
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
        const std::filesystem::path path = academyGhostPath();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::filesystem::path temporaryPath = path;
        temporaryPath += ".tmp";
        {
            std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
            output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
            if (!output) {
                academyGhostSaved = false;
                return false;
            }
        }
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporaryPath, path, error);
        if (error) {
            std::filesystem::remove(temporaryPath, error);
            academyGhostSaved = false;
            return false;
        }
        academyGhostSaved = true;
        return true;
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
            if (candidate[first] < KEY_SPACE || candidate[first] > KEY_KB_MENU) {
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
            if (!(valueStream >> value)) {
                continue;
            }
            for (std::size_t index = 0; index < BindingCount; ++index) {
                if (name == BindingSettingNames[index]) {
                    loadedBindings[index] = value;
                }
            }
            if (name == "body") bodyColorIndex = value;
            if (name == "wheels") wheelColorIndex = value;
            if (name == "spoiler") spoilerColorIndex = value;
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

        bodyColorIndex = std::clamp(bodyColorIndex, 0, static_cast<int>(BodyColors.size()) - 1);
        wheelColorIndex = std::clamp(wheelColorIndex, 0, static_cast<int>(WheelColors.size()) - 1);
        spoilerColorIndex = std::clamp(spoilerColorIndex, 0, static_cast<int>(SpoilerColors.size()) - 1);
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

        const std::filesystem::path path = settingsPath();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream output(path, std::ios::trunc);
        if (!output) {
            settingsNotice = "COULD NOT SAVE SETTINGS";
            return;
        }
        output << "version=2\n";
        for (std::size_t index = 0; index < BindingCount; ++index) {
            output << BindingSettingNames[index] << '=' << bindings[index] << '\n';
        }
        output << "body=" << bodyColorIndex << '\n';
        output << "wheels=" << wheelColorIndex << '\n';
        output << "spoiler=" << spoilerColorIndex << '\n';
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
    }

    bool assignBinding(std::size_t selectedIndex, int newKey) {
        if (selectedIndex >= BindingCount || newKey < KEY_SPACE || newKey > KEY_KB_MENU) {
            settingsNotice = "THAT KEY CANNOT BE ASSIGNED";
            settingsNoticeTimer = 2.2F;
            return false;
        }
        if (newKey == KEY_F1 || newKey == KEY_ENTER) {
            settingsNotice = "F1 AND ENTER ARE RESERVED FOR MENUS";
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
        physics.createStaticBox({0.0F, -0.5F, 0.0F}, {ArenaHalfWidth, 0.5F, ArenaHalfLength});
        physics.createStaticBox({-ArenaHalfWidth - 0.45F, 3.1F, 0.0F}, {0.45F, 3.1F, ArenaHalfLength + 0.45F});
        physics.createStaticBox({ArenaHalfWidth + 0.45F, 3.1F, 0.0F}, {0.45F, 3.1F, ArenaHalfLength + 0.45F});
        physics.createStaticBox({0.0F, 3.1F, -ArenaHalfLength - 0.45F}, {ArenaHalfWidth + 0.45F, 3.1F, 0.45F});
        physics.createStaticBox({0.0F, 3.1F, ArenaHalfLength + 0.45F}, {ArenaHalfWidth + 0.45F, 3.1F, 0.45F});
        physics.createStaticBox({0.0F, 1.28F, 0.0F}, {ArenaHalfWidth - 0.25F, 1.28F, 0.16F});
    }

    void createActors() {
        ball = physics.createDynamicSphere({0.0F, 5.0F, 0.0F}, BallRadius, 4.2F, ballElasticity);
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
        }
        cars[0].paint = BodyColors[bodyColorIndex].color;
        cars[0].wheelColor = WheelColors[wheelColorIndex].color;
        cars[0].spoilerColor = SpoilerColors[spoilerColorIndex].color;
    }

    void resetCar(Car &car, Vec3 position, float heading) {
        car.heading = heading;
        car.boost = practiceMode() ? 100.0F : 65.0F;
        car.jumpCooldown = 0.0F;
        car.airborneTime = 0.0F;
        car.aiThinkTimer = 0.0F;
        car.touchCooldown = 0.0F;
        car.groundedGrace = 0.0F;
        car.jumpBuffer = 0.0F;
        car.aiTarget = position;
        car.jumpsUsed = 0;
        car.dodgeAvailable = true;
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
            }
        }
    }

    void resetRound(int nextServingTeam) {
        resetBoostPads();
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
        receivingCar = firstCarForTeam(receivingTeam);
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
                    resetCar(cars[index], {serveX, 0.62F, side * 19.1F}, heading);
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
            servingSide * 15.8F};
        serveVelocity = {};
        physics.setTransform(ball, servePosition, {});
        physics.setLinearVelocity(ball, {});
        physics.setAngularVelocity(ball, {});
        previousBallVelocity = {};
        aiBallTouchCooldown = 0.0F;
        rallyTouchCooldown = 0.0F;
        bestRallyTouches = std::max(bestRallyTouches, rallyTouches);
        rallyTouches = 0;
        rallyTime = 0.0F;
        replayFrames.clear();
        replayPlaybackFrame = 0.0F;
        ballTrail.clear();
        ballTrailTimer = 0.0F;
        resetTouchSequence();
        previousBallZ = servePosition.z;
        serveInProgress = true;
        serveCountdown = 3.0F;
        countdownCue = 4;
        accumulator = 0.0F;
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
        scoringTeam = 0;
        winner = 0;
        rallyTouches = 0;
        bestRallyTouches = 0;
        matchTouches = {0, 0};
        matchAerialTouches = {0, 0};
        matchBoostPickups = {0, 0};
        matchBoostSpent = {0.0F, 0.0F};
        matchPowerupsUsed = {0, 0};
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

    void resetTrainingServe() {
        if (gameMode != GameMode::TargetChallenge) {
            gameMode = GameMode::Training;
        }
        applyDifficultyPhysics();
        resetBoostPads();
        servingTeam = 1;
        receivingTeam = 0;
        servingCar = -1;
        receivingCar = 0;
        resetCar(cars[0], {0.0F, 0.62F, 10.8F}, Pi);
        for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
            resetCar(cars[index], {32.0F + static_cast<float>(index) * 3.0F, -8.0F, 28.0F}, 0.0F);
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
        serveCountdown = 3.0F;
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
        matchTime = 180.0F;
        overtime = false;
        pendingGameOver = false;
        resetTrainingServe();
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
            const Quaternion rotation = QuaternionNlerp(toRay(from.car.rotation), toRay(to.car.rotation), amount);
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
        academyGhostCaptureTimer = 0.0F;
        academyBoostUsed = false;
        academyLessonTimer = AcademyLessonDurations[static_cast<std::size_t>(academyLessonIndex())];
        serveInProgress = false;
        ballFreezeTimer = 0.0F;
        physics.setGravityFactor(ball, difficulty == Difficulty::Rookie ? 0.58F : 1.0F);

        resetCar(cars[0], {0.0F, 0.62F, academyLesson == AcademyLesson::BoostGates ? 19.0F : 10.8F}, Pi);
        for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
            resetCar(cars[index], {32.0F + static_cast<float>(index) * 3.0F, -8.0F, 28.0F}, 0.0F);
        }

        if (academyLesson == AcademyLesson::BoostGates) {
            academyGateIndex = 0;
            servePosition = {30.0F, -8.0F, 30.0F};
            serveVelocity = {};
        } else if (academyLesson == AcademyLesson::DoubleJump) {
            servePosition = {30.0F, -8.0F, 30.0F};
            serveVelocity = {};
        } else if (academyLesson == AcademyLesson::AerialReturn) {
            servePosition = {-3.8F, BallRadius + 2.2F, -15.8F};
            serveVelocity = {2.35F, 7.2F, 15.8F};
        } else {
            servePosition = {5.2F, BallRadius + 2.6F, -15.8F};
            serveVelocity = {-3.1F, 7.8F, 16.4F};
            challengeTarget = {-4.0F, 0.08F, -10.5F};
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
        controls.steer = std::abs(stickX) > 0.16F ? -stickX : 0.0F;
        controls.throttle = std::abs(stickY) > 0.16F ? -stickY : 0.0F;
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

    void emitBurst(Vec3 position, Color color, int count, float speed, float size = 0.12F) {
        for (int index = 0; index < count; ++index) {
            const float angle = static_cast<float>(GetRandomValue(0, 628)) * 0.01F;
            const float magnitude = speed * static_cast<float>(GetRandomValue(45, 100)) * 0.01F;
            Particle particle;
            particle.position = position;
            particle.velocity = {
                std::cos(angle) * magnitude,
                static_cast<float>(GetRandomValue(20, 100)) * 0.01F * magnitude,
                std::sin(angle) * magnitude};
            particle.life = static_cast<float>(GetRandomValue(35, 85)) * 0.01F;
            particle.initialLife = particle.life;
            particle.color = color;
            particle.size = size * static_cast<float>(GetRandomValue(70, 140)) * 0.01F;
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
            if (!isCarActive(car.slot)) {
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
        if (controls.powerupPressed) {
            usePowerup(car);
        }
        Transform transform = physics.transform(car.body);
        Vec3 velocity = physics.linearVelocity(car.body);
        const bool grounded = transform.position.y < 0.72F && std::abs(velocity.y) < 2.2F;
        car.jumpCooldown = std::max(0.0F, car.jumpCooldown - deltaSeconds);
        car.touchCooldown = std::max(0.0F, car.touchCooldown - deltaSeconds);
        car.jumpBuffer = controls.jumpPressed ? 0.12F : std::max(0.0F, car.jumpBuffer - deltaSeconds);

        if (grounded) {
            car.groundedGrace = 0.09F;
            car.airborneTime = 0.0F;
            car.jumpsUsed = 0;
            car.dodgeAvailable = true;
        } else {
            car.groundedGrace = std::max(0.0F, car.groundedGrace - deltaSeconds);
            car.airborneTime += deltaSeconds;
        }

        const float planarSpeed = length2D(velocity);
        const float turnFactor = clamp(0.36F + planarSpeed / 14.0F, 0.36F, 1.0F);
        const float turnRate = (!car.human || automatedPlayer) ? 3.05F : 2.35F;
        car.heading = wrapAngle(car.heading + controls.steer * turnRate * turnFactor * deltaSeconds);
        const Vec3 forward = forwardFromHeading(car.heading);

        const Quaternion bodyRotation = toRay(transform.rotation);
        const Vector3 rotatedForward = Vector3RotateByQuaternion({0.0F, 0.0F, 1.0F}, bodyRotation);
        const Vector3 rotatedRight = Vector3RotateByQuaternion({1.0F, 0.0F, 0.0F}, bodyRotation);
        const Vector3 rotatedUp = Vector3RotateByQuaternion({0.0F, 1.0F, 0.0F}, bodyRotation);
        const Vec3 aerialForward{rotatedForward.x, rotatedForward.y, rotatedForward.z};

        if (!grounded) {
            Vec3 angularVelocity = physics.angularVelocity(car.body);
            const Vec3 desiredAngular{
                rotatedRight.x * controls.throttle * 4.7F - rotatedUp.x * controls.steer * 2.7F,
                rotatedRight.y * controls.throttle * 4.7F - rotatedUp.y * controls.steer * 2.7F,
                rotatedRight.z * controls.throttle * 4.7F - rotatedUp.z * controls.steer * 2.7F};
            const float aerialResponse = clamp(7.5F * deltaSeconds, 0.0F, 1.0F);
            angularVelocity.x += (desiredAngular.x - angularVelocity.x) * aerialResponse;
            angularVelocity.y += (desiredAngular.y - angularVelocity.y) * aerialResponse;
            angularVelocity.z += (desiredAngular.z - angularVelocity.z) * aerialResponse;
            physics.setAngularVelocity(car.body, angularVelocity);
        }

        const bool boosting = controls.boostHeld && car.boost > 0.0F && (!grounded || controls.throttle > -0.1F);
        const float targetSpeed = boosting ? 21.5F : 13.5F;
        const float desiredX = grounded ? forward.x * controls.throttle * targetSpeed : velocity.x;
        const float desiredZ = grounded ? forward.z * controls.throttle * targetSpeed : velocity.z;
        const float traction = grounded ? 6.5F : 0.18F;
        velocity.x += (desiredX - velocity.x) * clamp(traction * deltaSeconds, 0.0F, 1.0F);
        velocity.z += (desiredZ - velocity.z) * clamp(traction * deltaSeconds, 0.0F, 1.0F);

        if (std::abs(controls.throttle) < 0.05F && grounded) {
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
            if (GetRandomValue(0, 7) == 0) {
                emitBurst(
                    {transform.position.x - boostDirection.x * 1.5F,
                        transform.position.y - boostDirection.y * 1.5F,
                        transform.position.z - boostDirection.z * 1.5F},
                    GOLD,
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

    Vec3 predictBall(float time) const {
        const Transform ballTransform = physics.transform(ball);
        const float gravity = difficulty == Difficulty::Rookie ? 10.44F : 18.0F;
        const BallKinematics predicted = predictBallMotion(
            {ballTransform.position, physics.linearVelocity(ball)},
            time,
            gravity,
            ballElasticity,
            {ArenaHalfWidth, ArenaHalfLength, 0.0F, 2.56F, 0.16F, BallRadius});
        return predicted.position;
    }

    float ballTimeToHeight(float height) const {
        const Transform ballTransform = physics.transform(ball);
        const Vec3 velocity = physics.linearVelocity(ball);
        const float c = ballTransform.position.y - height;
        const float halfGravity = difficulty == Difficulty::Rookie ? 5.22F : 9.0F;
        const float discriminant = velocity.y * velocity.y + 4.0F * halfGravity * c;
        if (discriminant <= 0.0F) {
            return 0.18F;
        }
        const float root = (velocity.y + std::sqrt(discriminant)) / (2.0F * halfGravity);
        return clamp(root, 0.08F, 2.4F);
    }

    float ballLandingTime() const {
        return std::max(0.18F, ballTimeToHeight(BallRadius));
    }

    int strikerForTeam(int team, Vec3 landingTarget) const {
        if (serveInProgress) {
            return team == servingTeam ? servingCar : receivingCar;
        }
        const auto interceptEta = [&](int index) {
            const Vec3 position = physics.transform(cars[index].body).position;
            const Vec3 offset = subtract(landingTarget, position);
            const float distance = length2D(offset);
            const float desiredHeading = std::atan2(offset.x, offset.z);
            const float turnPenalty = std::abs(wrapAngle(desiredHeading - cars[index].heading)) * 0.24F;
            const float speed = length2D(physics.linearVelocity(cars[index].body));
            return distance / clamp(7.0F + speed * 0.55F, 7.0F, 16.0F) + turnPenalty;
        };
        const int first = firstCarForTeam(team);
        int best = first;
        float bestEta = interceptEta(first);
        const int current = rotationStriker[team];
        if (current == first) {
            bestEta -= 0.18F;
        }
        for (int rank = 1; rank < activeCarsPerTeam(); ++rank) {
            const int index = first + rank;
            float eta = interceptEta(index);
            if (index == current) {
                eta -= 0.18F;
            }
            if (eta < bestEta) {
                best = index;
                bestEta = eta;
            }
        }

        if (team == 0 && !automatedPlayer && gameMode != GameMode::Training) {
            // AI teammates only call the ball away from the player with a decisive ETA advantage.
            const float playerEta = interceptEta(0) - (current == 0 ? 0.18F : 0.0F);
            return best != 0 && bestEta + 0.32F < playerEta ? best : 0;
        }
        return best;
    }

    int supportForTeam(int team, int striker, Vec3 landingTarget) const {
        const int first = firstCarForTeam(team);
        const float teamDirection = team == 0 ? 1.0F : -1.0F;
        const Vec3 supportTarget{
            clamp(-landingTarget.x * 0.5F, -6.5F, 6.5F),
            0.0F,
            teamDirection * 12.6F};
        int best = -1;
        float bestDistance = 1000.0F;
        for (int rank = 0; rank < activeCarsPerTeam(); ++rank) {
            const int index = first + rank;
            if (index == striker) {
                continue;
            }
            const float distance = length2D(subtract(supportTarget, physics.transform(cars[index].body).position));
            if (distance < bestDistance) {
                best = index;
                bestDistance = distance;
            }
        }
        return best;
    }

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
            const float territoryPenalty = ownHalf ? 0.0F : 7.5F;
            const float largePadBonus = largeBoostPad(index) ? 1.4F : 0.0F;
            const float cost = length2D(subtract(pad, position)) + territoryPenalty - largePadBonus;
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
        const float landingTime = ballLandingTime();
        const Vec3 landingTarget = predictBall(landingTime);
        const float contactHeight = difficulty == Difficulty::Pro ? 2.2F : 1.95F;
        const Vec3 contactTarget = predictBall(ballTimeToHeight(contactHeight));
        const float teamDirection = car.team == 0 ? 1.0F : -1.0F;
        const bool ballThreatensTeam = landingTarget.z * teamDirection > 0.35F;
        const int strikerSlot = strikerForTeam(car.team, landingTarget);
        const bool striker = strikerSlot == car.slot;
        const bool support = supportForTeam(car.team, strikerSlot, landingTarget) == car.slot;
        const int recoveryPad = !striker && car.boost < 30.0F
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
                target.z += teamDirection * (pro ? 0.2F : 0.1F);
            } else {
                const float laneSign = landingTarget.x >= 0.0F ? -1.0F : 1.0F;
                if (support) {
                    target.x = clamp(landingTarget.x * -0.65F, -7.2F, 7.2F);
                    target.z = teamDirection * (ballThreatensTeam
                            ? clamp(std::abs(landingTarget.z) + 3.8F, 11.5F, 15.3F)
                            : 11.8F);
                } else {
                    target.x = laneSign * 6.5F;
                    target.z = teamDirection * 17.2F;
                }
            }

            if (car.team == 0 && car.slot != 0 && !striker) {
                const Vec3 playerPosition = physics.transform(cars[0].body).position;
                const Vec3 separation = subtract(carTransform.position, playerPosition);
                const float separationDistance = length2D(separation);
                if (separationDistance < 4.2F) {
                    const float inverseDistance = 1.0F / std::max(0.25F, separationDistance);
                    target.x = carTransform.position.x + separation.x * inverseDistance * 5.5F;
                    target.z = carTransform.position.z + separation.z * inverseDistance * 5.5F;
                }
            }

            target.x = clamp(target.x, -10.2F, 10.2F);
            target.z = car.team == 0
                ? clamp(target.z, 1.5F, 20.1F)
                : clamp(target.z, -20.1F, -1.5F);
            car.aiTarget = target;
        }

        const Vec3 toTarget = subtract(car.aiTarget, carTransform.position);
        const float desiredHeading = std::atan2(toTarget.x, toTarget.z);
        const float difference = wrapAngle(desiredHeading - car.heading);
        const float distance = length2D(toTarget);
        Controls controls;
        controls.steer = clamp(difference * 1.9F, -1.0F, 1.0F);
        if (distance < 0.75F) {
            controls.throttle = 0.0F;
            controls.steer = 0.0F;
        } else if (std::abs(difference) > 2.7F) {
            controls.throttle = -0.72F;
            controls.steer = 0.0F;
        } else if (std::abs(difference) > 1.75F) {
            controls.throttle = -0.52F;
            controls.steer *= -1.0F;
        } else {
            controls.throttle = clamp(distance / 4.5F, 0.32F, 1.0F);
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
            && car.jumpCooldown <= 0.0F;
        controls.dodgePressed = difficulty == Difficulty::Pro
            && striker
            && car.jumpsUsed == 1
            && car.airborneTime > 0.2F
            && car.airborneTime < 1.1F
            && horizontalBallDistance < 3.1F
            && ballTransform.position.y > 1.75F;
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
        rallyTouchCooldown = std::max(0.0F, rallyTouchCooldown - FixedStep);
        const float velocityChange = length(subtract(ballVelocity, previousBallVelocity));
        if (velocityChange > 4.1F && hitSoundCooldown <= 0.0F) {
            const Vec3 ballPosition = physics.transform(ball).position;
            audio.playHit(velocityChange);
            emitBurst(ballPosition, Color{255, 224, 92, 255}, 12, clamp(velocityChange * 0.22F, 2.0F, 5.0F), 0.11F);
            shake = std::max(shake, clamp(velocityChange * 0.025F, 0.12F, 0.5F));
            hitSoundCooldown = 0.11F;
            if (rallyTouchCooldown <= 0.0F) {
                int touchingCar = -1;
                float closestDistance = 3.55F;
                for (int index = 0; index < static_cast<int>(cars.size()); ++index) {
                    if (!isCarActive(index)) {
                        continue;
                    }
                    const float distance = length(subtract(ballPosition, physics.transform(cars[index].body).position));
                    if (distance < closestDistance) {
                        closestDistance = distance;
                        touchingCar = index;
                    }
                }
                if (touchingCar >= 0) {
                    ++rallyTouches;
                    serveInProgress = false;
                    bestRallyTouches = std::max(bestRallyTouches, rallyTouches);
                    rallyTouchCooldown = 0.14F;
                    const int touchingTeam = cars[touchingCar].team;
                    advanceTeamRotation(touchingTeam, touchingCar);
                    if (physics.transform(cars[touchingCar].body).position.y > 1.05F) {
                        ++matchAerialTouches[touchingTeam];
                    }
                    if (registerTeamTouch(touchingTeam) == TouchResult::Fault && competitiveMode()) {
                        previousBallVelocity = ballVelocity;
                        scorePoint(1 - touchingTeam);
                        return;
                    }
                }
            }
        }
        previousBallVelocity = ballVelocity;
    }

    bool tryAiBallTouch(Car &car) {
        if ((car.human && !automatedPlayer) || car.touchCooldown > 0.0F || aiBallTouchCooldown > 0.0F) {
            return false;
        }

        const Transform carTransform = physics.transform(car.body);
        const Transform ballTransform = physics.transform(ball);
        const Vec3 offset = subtract(ballTransform.position, carTransform.position);
        const Vec3 landingTarget = predictBall(ballLandingTime());
        if (strikerForTeam(car.team, landingTarget) != car.slot
            && !(serveInProgress && car.slot == servingCar)) {
            return false;
        }
        const float horizontalReach = difficulty == Difficulty::Pro ? 3.05F : 2.8F;
        const float verticalReach = difficulty == Difficulty::Pro ? 3.72F : 3.35F;
        if (length2D(offset) > horizontalReach || offset.y < -0.45F || offset.y > verticalReach) {
            return false;
        }

        const float teamDirection = car.team == 0 ? 1.0F : -1.0F;
        if (ballTransform.position.z * teamDirection < -0.8F) {
            return false;
        }

        const float flightTime = difficulty == Difficulty::Pro ? 1.5F : 1.82F;
        const Vec3 target{
            clamp(-ballTransform.position.x * 0.35F
                    + std::sin(totalTime * 1.9F + static_cast<float>(car.slot)) * 2.0F,
                -8.2F,
                8.2F),
            BallRadius + 0.08F,
            -teamDirection * (difficulty == Difficulty::Pro ? 11.4F : 9.8F)};
        Vec3 returnVelocity{
            (target.x - ballTransform.position.x) / flightTime,
            (target.y - ballTransform.position.y
                + (difficulty == Difficulty::Rookie ? 5.22F : 9.0F) * flightTime * flightTime) / flightTime,
            (target.z - ballTransform.position.z) / flightTime};
        returnVelocity.y = clamp(returnVelocity.y, 7.2F, 13.5F);
        const float impactStrength = length(subtract(returnVelocity, physics.linearVelocity(ball)));
        physics.setLinearVelocity(ball, returnVelocity);
        physics.setAngularVelocity(ball, {2.0F, teamDirection * 5.0F, -returnVelocity.x * 0.25F});
        physics.addImpulse(car.body, {0.0F, 85.0F, -teamDirection * 55.0F});

        car.touchCooldown = 0.48F;
        aiBallTouchCooldown = 0.2F;
        rallyTouchCooldown = 0.18F;
        ++rallyTouches;
        serveInProgress = false;
        bestRallyTouches = std::max(bestRallyTouches, rallyTouches);
        if (carTransform.position.y > 1.05F) {
            ++matchAerialTouches[car.team];
        }
        if (registerTeamTouch(car.team) == TouchResult::Fault && competitiveMode()) {
            scorePoint(1 - car.team);
            return true;
        }
        advanceTeamRotation(car.team, car.slot);
        if (automatedPlayer) {
            TraceLog(
                LOG_INFO,
                "RALLY: touch=%d car=%d team=%d ball=(%.1f,%.1f,%.1f) next_striker=%d",
                rallyTouches,
                car.slot,
                car.team,
                ballTransform.position.x,
                ballTransform.position.y,
                ballTransform.position.z,
                rotationStriker[car.team]);
        }
        audio.playHit(impactStrength);
        emitBurst(ballTransform.position, car.paint, 18, 4.2F, 0.13F);
        shake = std::max(shake, 0.28F);
        previousBallVelocity = returnVelocity;
        return true;
    }

    void captureReplayFrame(bool force = false) {
        if (!competitiveMode() || (!force && simulationTick % 2U != 0U)) {
            return;
        }
        ReplayFrame frame;
        frame.ball = physics.transform(ball);
        for (std::size_t index = 0; index < cars.size(); ++index) {
            frame.cars[index] = physics.transform(cars[index].body);
        }
        replayFrames.push_back(frame);
        constexpr std::size_t MaximumReplayFrames = 300;
        if (replayFrames.size() > MaximumReplayFrames) {
            replayFrames.erase(replayFrames.begin());
        }
    }

    const ReplayFrame *currentReplayFrame() const {
        if (replayFrames.empty()) {
            return nullptr;
        }
        const std::size_t index = std::min(
            replayFrames.size() - 1,
            static_cast<std::size_t>(std::max(0.0F, replayPlaybackFrame)));
        return &replayFrames[index];
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
        winner = team;
        pendingGameOver = score[team] >= scoreLimit || overtime;
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
            if (academyLesson == AcademyLesson::AerialReturn && trainingReturnSuccessful) {
                completeAcademyLesson();
            } else if (academyLesson == AcademyLesson::TargetLanding
                && trainingReturnSuccessful && ballPosition.y <= BallRadius + 0.12F) {
                const float targetDistance = length2D(subtract(ballPosition, challengeTarget));
                if (targetDistance <= 5.2F) {
                    completeAcademyLesson();
                } else {
                    retryAcademyLesson("TARGET MISSED");
                    return;
                }
            }
            if (ballPosition.y <= BallRadius + 0.12F && !trainingReturnSuccessful) {
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
        tickBoostPads(FixedStep);
        tickPowerupSystem(FixedStep);
        aiBallTouchCooldown = std::max(0.0F, aiBallTouchCooldown - FixedStep);
        driveCar(cars[0], controls, FixedStep);
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
                        resetTrainingServe();
                    }
                }
                return;
            }
            physics.step(FixedStep);
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
            if (!trainingBallHasTouchedGround && position.y <= BallRadius + 0.12F) {
                resolvePracticeLanding(position);
            }
            previousBallZ = position.z;
            if (position.y < -5.0F || std::abs(position.x) > 22.0F || std::abs(position.z) > 30.0F) {
                resolvePracticeLanding(position, true);
            }
            return;
        }
        for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
            if (!isCarActive(index)) {
                continue;
            }
            if (gameMode == GameMode::LocalCoop && index == 1) {
                driveCar(cars[index], playerTwoControls, FixedStep);
            } else {
                driveCar(cars[index], aiControls(cars[index], FixedStep), FixedStep);
            }
        }

        physics.step(FixedStep);
        applyRookieBallAssist();
        recordBallTrail();
        rallyTime += FixedStep;
        if (!overtime) {
            matchTime = std::max(0.0F, matchTime - FixedStep);
        }

        const Transform ballTransform = physics.transform(ball);
        const Vec3 ballVelocity = physics.linearVelocity(ball);
        checkBallImpact(ballVelocity);
        if (state != MatchState::Playing) {
            return;
        }
        for (Car &car : cars) {
            if (!isCarActive(car.slot)) {
                continue;
            }
            if (tryAiBallTouch(car)) {
                break;
            }
        }
        if (state != MatchState::Playing) {
            return;
        }
        applyThirdTouchBoostIfCrossed();
        fastestBallSpeed = std::max(fastestBallSpeed, length(physics.linearVelocity(ball)));
        captureReplayFrame();
        previousBallZ = physics.transform(ball).position.z;

        if (rallyTime > 0.85F && ballTransform.position.y <= BallRadius + 0.12F) {
            scorePoint(ballTransform.position.z >= 0.0F ? 1 : 0);
            return;
        }

        if (!overtime && matchTime <= 0.0F) {
            if (score[0] == score[1]) {
                overtime = true;
            } else {
                winner = score[0] > score[1] ? 0 : 1;
                recordCareerMatch();
                recordArcadeCupResult();
                state = MatchState::GameOver;
            }
        }
    }

    void countdownFixedUpdate(Controls controls, Controls playerTwoControls = {}) {
        tickBoostPads(FixedStep);
        driveCar(cars[0], controls, FixedStep);
        if (competitiveMode()) {
            for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
                if (!isCarActive(index)) {
                    continue;
                }
                driveCar(cars[index], gameMode == GameMode::LocalCoop && index == 1 ? playerTwoControls : Controls{}, FixedStep);
            }
        }

        physics.step(FixedStep);
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
        if (state == MatchState::Title || state == MatchState::Loading) {
            const float angle = totalTime * 0.22F;
            desiredPosition = {std::sin(angle) * 31.0F, 12.5F, std::cos(angle) * 31.0F};
            desiredTarget = {0.0F, 1.8F, 0.0F};
        } else if (state == MatchState::GoalReplay && currentReplayFrame() != nullptr) {
            const Vec3 ballPosition = currentReplayFrame()->ball.position;
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
            const Vec3 ballPosition = physics.transform(ball).position;
            const Vec3 ballVelocity = physics.linearVelocity(ball);
            const float planarSpeed = length2D(ballVelocity);
            Vec3 trajectory{};
            if (planarSpeed > 0.35F) {
                trajectory = {ballVelocity.x / planarSpeed, 0.0F, ballVelocity.z / planarSpeed};
            } else {
                const Vec3 playerPosition = physics.transform(cars[0].body).position;
                const Vec3 fromPlayer = subtract(ballPosition, playerPosition);
                const float distance = std::max(0.1F, length2D(fromPlayer));
                trajectory = {fromPlayer.x / distance, 0.0F, fromPlayer.z / distance};
            }
            desiredPosition = {
                clamp(ballPosition.x - trajectory.x * 9.5F, -17.0F, 17.0F),
                clamp(ballPosition.y + 4.8F, 5.2F, 14.5F),
                clamp(ballPosition.z - trajectory.z * 9.5F, -23.0F, 23.0F)};
            desiredTarget = {
                ballPosition.x + trajectory.x * 2.2F,
                ballPosition.y + clamp(ballVelocity.y * 0.08F, -0.8F, 1.2F),
                ballPosition.z + trajectory.z * 2.2F};
            desiredFov = 62.0F;
        } else {
            const Transform player = physics.transform(cars[0].body);
            const Vec3 forward = forwardFromHeading(cars[0].heading);
            const float speedFraction = clamp(length(physics.linearVelocity(cars[0].body)) / 26.0F, 0.0F, 1.0F);
            const bool wideTeamView = gameMode == GameMode::ThreeVsThree;
            const float followDistance = wideTeamView ? 11.2F : 8.8F;
            desiredPosition = {
                player.position.x - forward.x * followDistance,
                player.position.y + (wideTeamView ? 6.4F : 5.2F),
                player.position.z - forward.z * followDistance};
            desiredTarget = {
                player.position.x + forward.x * (wideTeamView ? 4.2F : 3.2F),
                player.position.y + 1.25F,
                player.position.z + forward.z * (wideTeamView ? 4.2F : 3.2F)};
            desiredPosition.x = clamp(desiredPosition.x, -ArenaHalfWidth + 1.2F, ArenaHalfWidth - 1.2F);
            desiredPosition.z = clamp(desiredPosition.z, -ArenaHalfLength + 1.2F, ArenaHalfLength - 1.2F);
            desiredFov = (wideTeamView ? 63.0F : 58.0F) + speedFraction * 9.0F;
        }

        const float response = 1.0F - std::exp(-6.5F * deltaSeconds);
        camera.position = Vector3Lerp(camera.position, desiredPosition, response);
        camera.target = Vector3Lerp(camera.target, desiredTarget, response);
        camera.fovy += (desiredFov - camera.fovy) * response;
        if (shake > 0.0F) {
            camera.position.x += static_cast<float>(GetRandomValue(-100, 100)) * 0.01F * shake;
            camera.position.y += static_cast<float>(GetRandomValue(-100, 100)) * 0.006F * shake;
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
            const Vec3 playerVelocity = physics.linearVelocity(cars[slot].body);
            Vec3 viewDirection = forwardFromHeading(cars[slot].heading);
            Vector3 desiredTarget{};
            if (cameraMode == CameraMode::Ball) {
                const Vec3 towardBall = subtract(ballPosition, player.position);
                const float planarDistance = length2D(towardBall);
                if (planarDistance > 0.2F) {
                    viewDirection = {towardBall.x / planarDistance, 0.0F, towardBall.z / planarDistance};
                }
                desiredTarget = {ballPosition.x, ballPosition.y + 0.25F, ballPosition.z};
            } else {
                desiredTarget = {
                    player.position.x + viewDirection.x * 3.8F,
                    player.position.y + 1.15F,
                    player.position.z + viewDirection.z * 3.8F};
            }

            const Vector3 desiredPosition{
                player.position.x - viewDirection.x * 9.4F,
                player.position.y + 5.7F,
                player.position.z - viewDirection.z * 9.4F};
            const float speedFraction = clamp(length(playerVelocity) / 26.0F, 0.0F, 1.0F);
            const float desiredFov = 68.0F + speedFraction * 8.0F;
            const float response = 1.0F - std::exp(-7.5F * deltaSeconds);
            Camera3D &localCamera = localCoopCameras[static_cast<std::size_t>(slot)];
            localCamera.position = Vector3Lerp(localCamera.position, desiredPosition, response);
            localCamera.target = Vector3Lerp(localCamera.target, desiredTarget, response);
            localCamera.fovy += (desiredFov - localCamera.fovy) * response;
            localCamera.up = {0.0F, 1.0F, 0.0F};
            localCamera.projection = CAMERA_PERSPECTIVE;
        }
    }

    bool menuSelectPressed() const {
        return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)
            || (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
    }

    void applyPlayerCustomization() {
        cars[0].paint = BodyColors[bodyColorIndex].color;
        cars[0].wheelColor = WheelColors[wheelColorIndex].color;
        cars[0].spoilerColor = spoilerColorIndex == 1
            ? cars[0].paint
            : SpoilerColors[spoilerColorIndex].color;
    }

    void cycleCustomization(int direction) {
        if (customizeMenuIndex == 0) {
            bodyColorIndex = (bodyColorIndex + direction + static_cast<int>(BodyColors.size()))
                % static_cast<int>(BodyColors.size());
        } else if (customizeMenuIndex == 1) {
            wheelColorIndex = (wheelColorIndex + direction + static_cast<int>(WheelColors.size()))
                % static_cast<int>(WheelColors.size());
        } else if (customizeMenuIndex == 2) {
            spoilerColorIndex = (spoilerColorIndex + direction + static_cast<int>(SpoilerColors.size()))
                % static_cast<int>(SpoilerColors.size());
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
        int itemCount = 13;
        if (menuPage == MenuPage::MatchSetup) {
            selectionPointer = &matchSetupMenuIndex;
            itemCount = 6;
        } else if (menuPage == MenuPage::Customize) {
            selectionPointer = &customizeMenuIndex;
            itemCount = 4;
        } else if (menuPage == MenuPage::Controls) {
            selectionPointer = &controlsMenuIndex;
            itemCount = static_cast<int>(BindingCount) + 2;
        } else if (menuPage == MenuPage::About) {
            selectionPointer = &aboutMenuIndex;
            itemCount = 4;
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

        if (IsKeyPressed(KEY_ESCAPE) && menuPage != MenuPage::Main) {
            menuPage = MenuPage::Main;
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
            if (customizeMenuIndex == 3) {
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
        } else if (menuPage == MenuPage::About) {
            audio.play(audio.menuMove);
            if (aboutMenuIndex == 3) {
                menuPage = MenuPage::Main;
            } else {
                openAboutLink(aboutMenuIndex);
            }
        }
    }

    void handleGlobalInput() {
        if (IsKeyPressed(KEY_F1)) {
            showHelp = !showHelp;
        }
        if (state == MatchState::Title) {
            handleMenuInput();
            return;
        }
        if (state == MatchState::GoalReplay
            && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))) {
            finishPointSequence();
            return;
        }
        if (!arcadeCupActive && IsKeyPressed(KEY_ONE)) {
            difficulty = Difficulty::Rookie;
            applyDifficultyPhysics();
            saveSettings("DIFFICULTY SAVED");
        }
        if (!arcadeCupActive && IsKeyPressed(KEY_TWO)) {
            difficulty = Difficulty::Pro;
            applyDifficultyPhysics();
            saveSettings("DIFFICULTY SAVED");
        }
        if (academyActive
            && (state == MatchState::Playing || state == MatchState::ServeCountdown)
            && IsKeyPressed(KEY_TAB)) {
            touchNotice = std::string("SKIPPED  /  ") + academyLessonName();
            touchNoticeTimer = 1.2F;
            advanceAcademyLesson(true);
            audio.play(audio.menuMove);
        } else if (gameMode == GameMode::Training
            && (state == MatchState::Playing || state == MatchState::ServeCountdown)
            && IsKeyPressed(KEY_TAB)) {
            const int next = (static_cast<int>(trainingFeed) + 1) % 4;
            trainingFeed = static_cast<TrainingFeed>(next);
            resetTrainingServe();
            touchNotice = std::string("FEED: ") + trainingFeedName();
            touchNoticeTimer = 1.8F;
            audio.play(audio.menuMove);
        }
        if (academyActive && (state == MatchState::Playing || state == MatchState::ServeCountdown)
            && IsKeyPressed(KEY_G)) {
            academyGhostEnabled = !academyGhostEnabled;
            touchNotice = std::string("PB GHOST  /  ") + (academyGhostEnabled ? "ON" : "OFF");
            touchNoticeTimer = 1.6F;
            saveSettings(academyGhostEnabled ? "ACADEMY GHOST ON" : "ACADEMY GHOST OFF");
            audio.play(audio.menuMove);
        }
        if (!arcadeCupActive && IsKeyPressed(KEY_LEFT_BRACKET)) {
            ballElasticity = std::max(0.55F, ballElasticity - 0.05F);
            physics.setRestitution(ball, ballElasticity);
            saveSettings("BALL BOUNCE SAVED");
        }
        if (!arcadeCupActive && IsKeyPressed(KEY_RIGHT_BRACKET)) {
            ballElasticity = std::min(0.95F, ballElasticity + 0.05F);
            physics.setRestitution(ball, ballElasticity);
            saveSettings("BALL BOUNCE SAVED");
        }
        const bool gamepadCameraToggle = IsGamepadAvailable(0)
            && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP);
        if ((IsKeyPressed(boundKey(BindAction::Camera)) || gamepadCameraToggle)
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
                resetTrainingServe();
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
            accumulator = 0.0F;
        }

        if (IsKeyPressed(boundKey(BindAction::Pause))) {
            if (state == MatchState::Playing || state == MatchState::ServeCountdown) {
                pausedFrom = state;
                state = MatchState::Paused;
                accumulator = 0.0F;
            } else if (state == MatchState::Paused) {
                state = pausedFrom;
            }
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

    void update(float deltaSeconds) {
        totalTime += deltaSeconds;
        boostSoundCooldown = std::max(0.0F, boostSoundCooldown - deltaSeconds);
        cameraModeNotice = std::max(0.0F, cameraModeNotice - deltaSeconds);
        settingsNoticeTimer = std::max(0.0F, settingsNoticeTimer - deltaSeconds);
        touchNoticeTimer = std::max(0.0F, touchNoticeTimer - deltaSeconds);
        careerUnlockTimer = std::max(0.0F, careerUnlockTimer - deltaSeconds);
        thirdTouchBoostTimer = std::max(0.0F, thirdTouchBoostTimer - deltaSeconds);
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
            if (scriptedAerialTest) {
                aerialTestTimer += deltaSeconds;
                if (!aerialFirstJumpTriggered && aerialTestTimer >= 0.1F) {
                    controls.jumpPressed = true;
                    aerialFirstJumpTriggered = true;
                } else if (!aerialSecondJumpTriggered && aerialTestTimer >= 0.38F) {
                    controls.jumpPressed = true;
                    aerialSecondJumpTriggered = true;
                }
                if (aerialTestTimer >= 0.42F && aerialTestTimer <= 0.92F) {
                    controls.throttle = -1.0F;
                }
                controls.boostHeld = aerialTestTimer >= 0.78F && aerialTestTimer <= 1.85F;
            } else if (scriptedSprintTest) {
                sprintTestTimer += deltaSeconds;
                controls.throttle = 1.0F;
                controls.boostHeld = true;
            } else {
                controls = automatedPlayer
                    ? (state == MatchState::Playing && gameMode == GameMode::Match
                            ? aiControls(cars[0], deltaSeconds)
                            : Controls{})
                    : (gameMode == GameMode::LocalCoop ? keyboardControls() : playerControls());
                if (gameMode == GameMode::LocalCoop && !automatedPlayer) {
                    const net::PlayerInputPacket playerTwoPacket = makeInputPacket(gamepadControls(), 1);
                    playerTwoControls = controlsFromInputPacket(playerTwoPacket);
                }
            }
            accumulator = std::min(accumulator + deltaSeconds, 0.2F);
            bool firstStep = true;
            const MatchState activeState = state;
            while (accumulator >= FixedStep && state == activeState) {
                Controls stepControls = controls;
                Controls stepPlayerTwoControls = playerTwoControls;
                if (!firstStep) {
                    stepControls.jumpPressed = false;
                    stepControls.dodgePressed = false;
                    stepControls.powerupPressed = false;
                    stepPlayerTwoControls.jumpPressed = false;
                    stepPlayerTwoControls.dodgePressed = false;
                    stepPlayerTwoControls.powerupPressed = false;
                }
                if (activeState == MatchState::Playing) {
                    fixedUpdate(stepControls, stepPlayerTwoControls);
                } else {
                    countdownFixedUpdate(stepControls, stepPlayerTwoControls);
                }
                firstStep = false;
                accumulator -= FixedStep;
            }
            if (scriptedAerialTest) {
                const Transform aerialTransform = physics.transform(cars[0].body);
                const Vector3 aerialForward = Vector3RotateByQuaternion(
                    {0.0F, 0.0F, 1.0F},
                    toRay(aerialTransform.rotation));
                aerialPeakHeight = std::max(aerialPeakHeight, aerialTransform.position.y);
                aerialPeakForwardY = std::max(aerialPeakForwardY, aerialForward.y);
                aerialMaxJumpsUsed = std::max(aerialMaxJumpsUsed, cars[0].jumpsUsed);
                aerialTestComplete = aerialTestTimer >= 2.55F;
            } else if (scriptedSprintTest) {
                const float playerZ = physics.transform(cars[0].body).position.z;
                if (playerZ <= 2.0F) {
                    sprintCompletedCourse = true;
                    sprintFinishTime = sprintTestTimer;
                    sprintTestComplete = true;
                } else if (sprintTestTimer >= 4.0F) {
                    sprintFinishTime = sprintTestTimer;
                    sprintTestComplete = true;
                }
            }
        } else if (state == MatchState::PointWon) {
            pointTimer -= deltaSeconds;
            if (pointTimer <= 0.0F) {
                if (replayFrames.size() >= 30) {
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

        updateParticles(deltaSeconds);
        updateCamera(deltaSeconds);
        updateLocalCoopCameras(deltaSeconds);
    }

    void drawBlockyTree(Vector3 base, Color leaves) const {
        DrawCube({base.x, base.y + 2.25F, base.z}, 0.65F, 4.5F, 0.65F, Color{103, 67, 39, 255});
        DrawCube({base.x, base.y + 4.8F, base.z}, 2.7F, 2.0F, 2.7F, leaves);
        DrawCube({base.x - 0.8F, base.y + 5.7F, base.z + 0.25F}, 1.8F, 1.55F, 1.9F, scaledColor(leaves, 1.15F));
        DrawCube({base.x + 0.85F, base.y + 5.45F, base.z - 0.3F}, 1.7F, 1.6F, 1.8F, scaledColor(leaves, 0.86F));
    }

    void drawEnvironmentProps() const {
        if (activeArenaIndex == 0) {
            for (int side : {-1, 1}) {
                for (int index = -3; index <= 3; ++index) {
                    drawBlockyTree(
                        {static_cast<float>(index) * 4.1F, 0.0F, static_cast<float>(side) * 25.8F},
                        index % 2 == 0 ? Color{48, 139, 70, 255} : Color{31, 108, 61, 255});
                }
            }
            for (int side : {-1, 1}) {
                drawBlockyTree({static_cast<float>(side) * 20.2F, 0.0F, -13.0F}, Color{42, 127, 64, 255});
                drawBlockyTree({static_cast<float>(side) * 20.2F, 0.0F, 13.0F}, Color{56, 151, 72, 255});
            }
            for (int side : {-1, 1}) {
                drawBlockyTree({-9.5F, 6.0F, static_cast<float>(side) * 23.4F}, Color{43, 130, 59, 255});
                drawBlockyTree({9.5F, 6.0F, static_cast<float>(side) * 23.4F}, Color{52, 151, 67, 255});
            }
        } else if (activeArenaIndex == 1) {
            DrawCube({0.0F, -0.18F, 26.5F}, 13.5F, 0.2F, 5.6F, Color{37, 185, 224, 255});
            DrawCube({0.0F, -0.22F, -26.5F}, 13.5F, 0.2F, 5.6F, Color{37, 185, 224, 255});
            DrawCube({0.0F, -0.12F, 23.8F}, 14.3F, 0.12F, 0.28F, RAYWHITE);
            DrawCube({0.0F, -0.12F, -23.8F}, 14.3F, 0.12F, 0.28F, RAYWHITE);
            DrawCube({20.2F, -0.16F, 0.0F}, 5.0F, 0.18F, 15.0F, Color{37, 185, 224, 255});
            DrawCube({-20.2F, -0.16F, 0.0F}, 5.0F, 0.18F, 15.0F, Color{37, 185, 224, 255});
            for (int side : {-1, 1}) {
                for (int end : {-1, 1}) {
                    const Vector3 base{static_cast<float>(side) * 19.0F, 0.0F, static_cast<float>(end) * 9.0F};
                    DrawCube({base.x, 3.0F, base.z}, 0.62F, 6.0F, 0.62F, Color{139, 91, 48, 255});
                    DrawCube({base.x, 6.15F, base.z}, 5.0F, 0.4F, 0.72F, Color{45, 155, 87, 255});
                    DrawCube({base.x, 6.15F, base.z}, 0.72F, 0.4F, 5.0F, Color{38, 137, 79, 255});
                }
            }
            for (int side : {-1, 1}) {
                DrawCube({static_cast<float>(side) * 16.5F, 0.65F, 17.0F}, 2.6F, 0.2F, 2.6F, Color{255, 105, 90, 255});
                DrawCube({static_cast<float>(side) * 16.5F, 1.7F, 17.0F}, 0.18F, 2.1F, 0.18F, RAYWHITE);
            }
            for (int side : {-1, 1}) {
                for (int x : {-8, 8}) {
                    const Vector3 base{static_cast<float>(x), 6.0F, static_cast<float>(side) * 23.5F};
                    DrawCube({base.x, 8.1F, base.z}, 0.58F, 4.2F, 0.58F, Color{139, 91, 48, 255});
                    DrawCube({base.x, 10.3F, base.z}, 4.5F, 0.42F, 0.7F, Color{45, 155, 87, 255});
                    DrawCube({base.x, 10.3F, base.z}, 0.7F, 0.42F, 4.5F, Color{38, 137, 79, 255});
                }
            }
        } else if (activeArenaIndex == 2) {
            for (int side : {-1, 1}) {
                for (int index = -4; index <= 4; ++index) {
                    const float height = 4.5F + static_cast<float>((index * index + side + 7) % 5) * 1.6F;
                    const float x = static_cast<float>(index) * 4.0F;
                    const float z = static_cast<float>(side) * 27.0F;
                    DrawCube({x, height * 0.5F, z}, 3.2F, height, 3.0F, Color{20, 27, 48, 255});
                    for (int window = 1; window < static_cast<int>(height); window += 2) {
                        DrawCube({x, static_cast<float>(window), z - static_cast<float>(side) * 1.52F}, 1.3F, 0.35F, 0.08F,
                            (window + index) % 3 == 0 ? Color{255, 214, 93, 255} : Color{78, 156, 225, 255});
                    }
                }
            }
        } else {
            for (int side : {-1, 1}) {
                for (int end : {-1, 1}) {
                    const Vector3 base{static_cast<float>(side) * 19.5F, 0.0F, static_cast<float>(end) * 18.0F};
                    DrawCube({base.x, 1.1F, base.z}, 3.8F, 2.2F, 3.2F, Color{126, 65, 40, 255});
                    DrawCube({base.x + static_cast<float>(side), 2.55F, base.z}, 2.4F, 1.2F, 2.2F, Color{161, 80, 42, 255});
                }
            }
            for (int end : {-1, 1}) {
                for (int x : {-9, 9}) {
                    const Vector3 base{static_cast<float>(x), 0.0F, static_cast<float>(end) * 25.0F};
                    DrawCube({base.x, 1.9F, base.z}, 0.55F, 3.8F, 0.55F, Color{47, 129, 67, 255});
                    DrawCube({base.x + 0.85F, 2.35F, base.z}, 1.7F, 0.48F, 0.48F, Color{47, 129, 67, 255});
                    DrawCube({base.x - 0.7F, 1.45F, base.z}, 1.4F, 0.48F, 0.48F, Color{47, 129, 67, 255});
                }
            }
            for (int side : {-1, 1}) {
                DrawCube({-8.5F, 8.2F, static_cast<float>(side) * 23.7F}, 5.2F, 4.4F, 3.4F, Color{136, 64, 37, 255});
                DrawCube({8.5F, 9.0F, static_cast<float>(side) * 23.7F}, 6.0F, 5.8F, 3.6F, Color{157, 74, 39, 255});
            }
        }
    }

    void drawArena() const {
        const ArenaTheme &theme = activeArena();
        DrawPlane({0.0F, -0.34F, 0.0F}, {82.0F, 94.0F}, scaledColor(theme.skyTop, 0.45F));

        for (int tier = 0; tier < 4; ++tier) {
            const float x = 15.4F + static_cast<float>(tier) * 1.15F;
            const float y = 0.4F + static_cast<float>(tier) * 0.85F;
            const float height = 0.8F + static_cast<float>(tier) * 0.25F;
            const Color standColor = scaledColor(theme.floor, tier % 2 == 0 ? 0.72F : 0.9F);
            DrawCube({-x, y, 0.0F}, 1.1F, height, 45.0F, standColor);
            DrawCube({x, y, 0.0F}, 1.1F, height, 45.0F, standColor);
        }

        for (int side = -1; side <= 1; side += 2) {
            for (int z = -20; z <= 20; z += 2) {
                for (int row = 0; row < 3; ++row) {
                    const bool blueFan = ((z / 2) + row + side) % 3 != 0;
                    const Color crowd = blueFan ? scaledColor(theme.blueCourt, 1.55F) : scaledColor(theme.orangeCourt, 1.55F);
                    DrawCube(
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
        DrawCube({0.0F, -0.08F, 11.0F}, 27.6F, 0.08F, 21.6F, theme.blueCourt);
        DrawCube({0.0F, -0.075F, -11.0F}, 27.6F, 0.08F, 21.6F, theme.orangeCourt);

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
        DrawCube({-13.75F, 0.025F, 0.0F}, 0.08F, 0.05F, 43.4F, lineColor);
        DrawCube({13.75F, 0.025F, 0.0F}, 0.08F, 0.05F, 43.4F, lineColor);
        DrawCube({0.0F, 0.025F, -21.75F}, 27.5F, 0.05F, 0.08F, lineColor);
        DrawCube({0.0F, 0.025F, 21.75F}, 27.5F, 0.05F, 0.08F, lineColor);
        DrawCube({0.0F, 0.03F, 0.0F}, 27.5F, 0.06F, 0.1F, lineColor);

        for (int x = -12; x <= 12; x += 2) {
            DrawLine3D({static_cast<float>(x), 0.08F, -21.7F}, {static_cast<float>(x), 0.08F, 21.7F}, Color{91, 132, 144, 45});
        }
        for (int z = -20; z <= 20; z += 2) {
            DrawLine3D({-13.7F, 0.08F, static_cast<float>(z)}, {13.7F, 0.08F, static_cast<float>(z)}, Color{91, 132, 144, 45});
        }

        DrawCube({-13.55F, 1.45F, 0.0F}, 0.28F, 2.9F, 0.28F, theme.accent);
        DrawCube({13.55F, 1.45F, 0.0F}, 0.28F, 2.9F, 0.28F, theme.accent);
        DrawCube({0.0F, 2.56F, 0.0F}, 27.1F, 0.12F, 0.18F, theme.accent);
        for (int x = -13; x <= 13; ++x) {
            DrawLine3D({static_cast<float>(x), 0.1F, 0.0F}, {static_cast<float>(x), 2.52F, 0.0F}, Color{222, 235, 225, 160});
        }
        for (int row = 1; row <= 8; ++row) {
            const float y = 0.1F + static_cast<float>(row) * 0.29F;
            DrawLine3D({-13.5F, y, 0.0F}, {13.5F, y, 0.0F}, Color{222, 235, 225, 160});
        }

        const Color barrier = withAlpha(theme.cage, 65);
        DrawCube({-14.28F, 3.0F, 0.0F}, 0.35F, 6.0F, 44.0F, barrier);
        DrawCube({14.28F, 3.0F, 0.0F}, 0.35F, 6.0F, 44.0F, barrier);
        DrawCube({0.0F, 3.0F, -22.28F}, 28.0F, 6.0F, 0.35F, barrier);
        DrawCube({0.0F, 3.0F, 22.28F}, 28.0F, 6.0F, 0.35F, barrier);

        const Color cageLine = theme.cage;
        for (int z = -21; z <= 21; z += 3) {
            DrawLine3D({-14.08F, 0.1F, static_cast<float>(z)}, {-14.08F, 6.1F, static_cast<float>(z)}, cageLine);
            DrawLine3D({14.08F, 0.1F, static_cast<float>(z)}, {14.08F, 6.1F, static_cast<float>(z)}, cageLine);
        }
        for (int y = 1; y <= 6; ++y) {
            DrawLine3D({-14.08F, static_cast<float>(y), -22.0F}, {-14.08F, static_cast<float>(y), 22.0F}, cageLine);
            DrawLine3D({14.08F, static_cast<float>(y), -22.0F}, {14.08F, static_cast<float>(y), 22.0F}, cageLine);
        }

        DrawCube({-14.15F, 6.25F, 0.0F}, 0.25F, 0.18F, 44.0F, scaledColor(theme.blueCourt, 1.65F));
        DrawCube({14.15F, 6.25F, 0.0F}, 0.25F, 0.18F, 44.0F, scaledColor(theme.orangeCourt, 1.65F));

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
                DrawCube({static_cast<float>(index), 8.1F, static_cast<float>(side) * 22.35F}, 0.7F, 0.35F, 0.25F, lamp);
            }

            const Color endColor = side > 0 ? scaledColor(theme.blueCourt, 1.65F) : scaledColor(theme.orangeCourt, 1.65F);
            DrawCube({0.0F, 8.2F, static_cast<float>(side) * 22.7F}, 10.8F, 3.0F, 0.28F, Color{13, 21, 38, 255});
            DrawCube({0.0F, 9.62F, static_cast<float>(side) * 22.52F}, 10.8F, 0.16F, 0.18F, endColor);
            for (int bar = -4; bar <= 4; ++bar) {
                const float brightness = 0.55F + 0.45F * std::sin(totalTime * 2.2F + static_cast<float>(bar));
                DrawCube(
                    {static_cast<float>(bar) * 1.05F, 8.2F, static_cast<float>(side) * 22.5F},
                    0.62F,
                    0.2F + brightness * 0.32F,
                    0.14F,
                    withAlpha(endColor, static_cast<unsigned char>(130.0F + brightness * 120.0F)));
            }
        }

        drawEnvironmentProps();
    }

    void drawCarGeometry(
        Vector3 position,
        Quaternion rotation,
        Color paint,
        Color wheelColor,
        Color spoilerColor,
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
        DrawModelEx(cubeModel, position, axis, angle * RAD2DEG, {1.84F, 0.9F, 2.84F}, withAlpha(paint, opacity));

        const Vector3 cabinOffset = Vector3RotateByQuaternion({0.0F, 0.58F, -0.12F}, rotation);
        const Vector3 cabinPosition = Vector3Add(position, cabinOffset);
        DrawModelEx(cubeModel, cabinPosition, axis, angle * RAD2DEG, {1.35F, 0.58F, 1.25F},
            Color{30, 42, 60, opacity});

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
            car.paint,
            car.wheelColor,
            car.spoilerColor,
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
            Color{61, 222, 255, 255},
            Color{170, 236, 255, 255},
            Color{221, 92, 255, 255},
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
        DrawModel(ballModel, position, 1.0F, Color{250, 225, 85, 255});
        DrawSphereWires(position, BallRadius + 0.012F, 8, 12, Color{208, 76, 55, 210});
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
            if (!isCarActive(car.slot)) {
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
        const float landingTime = ballLandingTime();
        const Vec3 landing = predictBall(landingTime);
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
        const float pulse = 0.82F + 0.12F * std::sin(totalTime * 6.0F);
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
        const Vec3 landing = state == MatchState::ServeCountdown ? serveLandingTarget : predictBall(ballLandingTime());
        const int striker = strikerForTeam(0, landing);
        const int support = supportForTeam(0, striker, landing);
        for (int index = 0; index < CarsPerTeam; ++index) {
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

    void drawWorld(const Camera3D &viewCamera) const {
        BeginMode3D(viewCamera);
        drawArena();
        const ReplayFrame *replay = state == MatchState::GoalReplay ? currentReplayFrame() : nullptr;
        if (replay != nullptr) {
            for (std::size_t index = 0; index < cars.size(); ++index) {
                if (!isCarActive(static_cast<int>(index))) {
                    continue;
                }
                drawCarGeometry(
                    toRay(replay->cars[index].position),
                    toRay(replay->cars[index].rotation),
                    cars[index].paint,
                    cars[index].wheelColor,
                    cars[index].spoilerColor,
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
                if (isCarActive(index)) {
                    drawCar(cars[index]);
                }
            }
            drawBall();
            drawPowerupEffects();
        }
        drawParticles();
        EndMode3D();
    }

    void drawWorld() const {
        drawWorld(camera);
    }

    void drawLocalPlayerFocus(int slot) const {
        const Car &car = cars[static_cast<std::size_t>(slot)];
        const Transform transform = physics.transform(car.body);
        const Vector3 position = toRay(transform.position);
        const Color playerColor = slot == 0 ? SKYBLUE : GOLD;
        const float pulse = 0.5F + 0.5F * std::sin(totalTime * 5.0F + static_cast<float>(slot) * Pi);

        BeginMode3D(localCoopCameras[static_cast<std::size_t>(slot)]);
        rlDisableDepthTest();
        drawCarGeometry(
            position,
            toRay(transform.rotation),
            car.paint,
            car.wheelColor,
            car.spoilerColor,
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
            drawWorld(localCoopCameras[slot]);
            drawLocalPlayerFocus(static_cast<int>(slot));

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
        DrawText("P1 / KEYBOARD", 31, 195, 14, RAYWHITE);
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
        const Vec3 landing = state == MatchState::ServeCountdown ? serveLandingTarget : predictBall(ballLandingTime());
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
            if (!isCarActive(index)) {
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
                : "FREE PLAY";
        } else if (gameMode == GameMode::TargetChallenge) {
            timerText = TextFormat("SHOT %d / %d", std::min(trainingAttempts, TargetChallengeShots), TargetChallengeShots);
        } else if (overtime) {
            timerText = "OVERTIME";
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
        if (state == MatchState::Playing) {
            if (academyActive) {
                DrawRectangle(24, 116, 382, 54, Color{9, 14, 24, 220});
                DrawRectangle(24, 116, 6, 54, SKYBLUE);
                DrawText(academyLessonName(), 40, 123, 17, GOLD);
                DrawText(academyLessonInstruction(), 40, 145, 14, RAYWHITE);
                DrawRectangle(ScreenWidth - 254, 116, 230, 54, Color{9, 14, 24, 220});
                DrawRectangle(ScreenWidth - 30, 116, 6, 54, ORANGE);
                DrawText(TextFormat("TIME  %05.2f", academyRunTimer), ScreenWidth - 238, 123, 17, RAYWHITE);
                DrawText(TextFormat("RETRIES  %d", academyRetries), ScreenWidth - 238, 145, 14,
                    academyRetries == 0 ? SKYBLUE : ORANGE);
            } else if (gameMode == GameMode::Training) {
                DrawRectangle(24, 116, 250, 30, Color{9, 14, 24, 210});
                DrawText(TextFormat("FEED %s  [TAB]", trainingFeedName()), 34, 123, 15, SKYBLUE);
                DrawRectangle(ScreenWidth - 224, 116, 200, 30, Color{9, 14, 24, 210});
                DrawText(TextFormat("RETURNS %d / %d", trainingReturns, trainingAttempts),
                    ScreenWidth - 211, 123, 15, trainingReturns > 0 ? GOLD : RAYWHITE);
            } else if (gameMode == GameMode::TargetChallenge) {
                DrawRectangle(24, 116, 250, 30, Color{9, 14, 24, 210});
                DrawText(TextFormat("%s FEED  /  TARGET ACTIVE", currentTrainingFeedName()), 34, 123, 15, SKYBLUE);
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
            const Vec3 projectedLanding = predictBall(ballLandingTime());
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
        DrawText(
            TextFormat("SPEED %03d", static_cast<int>(length(playerVelocity) * 11.0F)),
            meterX + meterWidth + 22,
            meterY - 1,
            17,
            Color{188, 210, 226, 255});
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
            const Vec3 landingTarget = predictBall(ballLandingTime());
            const int striker = strikerForTeam(0, landingTarget);
            if (gameMode == GameMode::ThreeVsThree) {
                const int support = supportForTeam(0, striker, landingTarget);
                const char *role = striker == 0 ? "YOU: STRIKER" : (support == 0 ? "YOU: SETTER" : "YOU: BACK COVER");
                DrawText(role, ScreenWidth - 204, ScreenHeight - 58, 16,
                    striker == 0 ? GOLD : (support == 0 ? SKYBLUE : Color{180, 196, 215, 255}));
            } else {
                const bool partnerChasing = landingTarget.z > 0.0F && striker == 1;
                DrawText(
                    partnerChasing ? "TEAMMATE: CHASING" : "TEAMMATE: COVERING",
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
        const std::string cameraPrompt = std::string(cameraMode == CameraMode::Ball ? "CAM: BALL  [" : "CAM: CAR  [")
            + keyName(boundKey(BindAction::Camera)) + " / Y]";
        DrawText(
            cameraPrompt.c_str(),
            ScreenWidth - 189,
            ScreenHeight - 82,
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
            DrawRectangle(ScreenWidth / 2 - 142, 164, 284, 48, Color{7, 12, 22, 225});
            drawCentered(cameraMode == CameraMode::Ball ? "BALL CAM" : "CAR CAM", 177, 24, GOLD);
        }
        if (touchNoticeTimer > 0.0F) {
            DrawRectangle(ScreenWidth / 2 - 188, 278, 376, 42, Color{7, 12, 22, 225});
            drawCentered(touchNotice, 289, 19, thirdTouchBoostTimer > 0.0F ? GOLD : RAYWHITE);
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
        DrawText(label.c_str(), x + 24, y + 15, 22, active ? RAYWHITE : Color{174, 192, 211, 255});
    }

    void drawMainMenu() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 8, 17, 165});
        drawCentered("ROCKET", 42, 58, SKYBLUE);
        drawCentered("VOLLEY", 96, 58, ORANGE);
        drawCentered(activeArena().name, 167, 19, activeArena().accent);
        drawCentered("ARCADE CUP + ROCKET ACADEMY  /  2v2 + 3v3 + SPLIT-SCREEN", 188, 17, RAYWHITE);

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
        drawCompactMenuRow("QUIT", 12, mainMenuIndex, x, startY + rowStep * 12, width);

        drawCentered("W/S MOVE     A/D CHANGE     ENTER SELECT", 616, 17, Color{204, 218, 230, 255});
        drawCentered(std::string("PILOT ") + careerRankName() + "  /  "
            + std::to_string(unlockedCareerMilestoneCount()) + "/" + std::to_string(CareerMilestoneCount)
            + " MILESTONES  /  CUP BEST " + std::to_string(arcadeCupBestRound) + "/3",
            644, 15, Color{150, 174, 196, 255});
        if (settingsNoticeTimer > 0.0F) {
            drawCentered(settingsNotice, 671, 16, GOLD);
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
            cars[0].paint,
            cars[0].wheelColor,
            cars[0].spoilerColor,
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
        DrawText("The point cap or clock can end the match.", 720, 320, 17, RAYWHITE);
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
        DrawText("CUSTOMIZE", 74, 58, 46, SKYBLUE);
        DrawText("YOUR RIDE", 76, 105, 31, ORANGE);
        DrawText("W/S SELECT   A/D CHANGE   ENTER CYCLE", 76, 157, 16, Color{176, 197, 217, 255});

        drawMenuRow(
            std::string("BODY     < ") + BodyColors[bodyColorIndex].name + " >",
            0,
            customizeMenuIndex,
            78,
            222,
            500);
        drawMenuRow(
            std::string("WHEELS   < ") + WheelColors[wheelColorIndex].name + " >",
            1,
            customizeMenuIndex,
            78,
            286,
            500);
        const char *spoilerName = spoilerColorIndex == 1 ? "BODY MATCH" : SpoilerColors[spoilerColorIndex].name;
        drawMenuRow(
            std::string("SPOILER  < ") + spoilerName + " >",
            2,
            customizeMenuIndex,
            78,
            350,
            500);
        drawMenuRow("BACK", 3, customizeMenuIndex, 78, 430, 500);
        DrawText("ESC ALSO RETURNS", 78, 505, 16, Color{145, 170, 193, 255});
        drawCustomizerPreview();
    }

    void drawCompactMenuRow(const std::string &label, int index, int selected, int x, int y, int width) const {
        const bool active = index == selected;
        DrawRectangle(x, y, width, 31, active ? Color{38, 56, 82, 245} : Color{15, 24, 39, 225});
        DrawRectangle(x, y, 5, 31, active ? GOLD : Color{77, 96, 121, 180});
        if (active) {
            DrawText(">", x - 25, y + 3, 23, GOLD);
        }
        DrawText(label.c_str(), x + 16, y + 7, 16, active ? RAYWHITE : Color{174, 192, 211, 255});
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
        DrawText("Forward dodge", 760, 321, 18, RAYWHITE);
        DrawText("B / RT", 718, 361, 17, GOLD);
        DrawText("Boost", 800, 361, 18, RAYWHITE);
        DrawText("Y", 718, 401, 17, GOLD);
        DrawText("Toggle camera", 760, 401, 18, RAYWHITE);
        DrawText("LB", 718, 441, 17, Color{215, 95, 255, 255});
        DrawText("Use Power Volley ability", 760, 441, 18, RAYWHITE);
        DrawText("MENU ACCESS", 718, 486, 17, SKYBLUE);
        DrawText("W/S or arrows  /  Enter select", 718, 515, 17, RAYWHITE);
        DrawText("F1 help and Enter stay reserved.", 718, 551, 15, Color{164, 185, 205, 255});

        DrawText("ESC BACK", 68, 638, 16, Color{164, 185, 205, 255});
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
        }
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
        drawCentered(keyName(boundKey(BindAction::Forward)) + " / " + keyName(boundKey(BindAction::Reverse)) + "     Accelerate / brake", 222, 23, RAYWHITE);
        drawCentered(keyName(boundKey(BindAction::SteerLeft)) + " / " + keyName(boundKey(BindAction::SteerRight)) + "     Steer", 264, 23, RAYWHITE);
        drawCentered(keyName(boundKey(BindAction::Jump)) + " / A    Jump, then jump again", 306, 23, RAYWHITE);
        drawCentered("AIR: " + keyName(boundKey(BindAction::Reverse)) + " / STICK DOWN NOSE UP, " + keyName(boundKey(BindAction::Boost)) + " / RT BOOST", 348, 19, RAYWHITE);
        drawCentered(keyName(boundKey(BindAction::Dodge)) + " / X        Directional dodge / flip", 390, 23, RAYWHITE);
        drawCentered(keyName(boundKey(BindAction::Camera)) + " / Y        Toggle Car Cam / Ball Cam", 424, 21, RAYWHITE);
        drawCentered(keyName(boundKey(BindAction::Powerup)) + " / LB      Use Power Volley ability", 456, 21,
            Color{215, 95, 255, 255});
        drawCentered(keyName(boundKey(BindAction::Pause)) + " pause   1/2 AI   [/] ball bounce", 488, 19, RAYWHITE);
        drawCentered("Gamepad: stick drive, A jump, X dodge, B/RT boost", 518, 17, Color{176, 199, 219, 255});
        drawCentered(keyName(boundKey(BindAction::Restart)) + " reset   TAB feeds / Academy skip   G PB ghost", 546, 16, Color{176, 199, 219, 255});
        drawCentered("3 TEAM TOUCHES = POWER  /  4TH TOUCH = FAULT", 570, 15, GOLD);
        drawCentered("GOLD PADS: FULL / 10S   SMALL PADS: +28 / 4S", 593, 14, SKYBLUE);
        drawCentered("F1 TO CLOSE", 618, 16, Color{176, 199, 219, 255});
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
            drawCentered("LET THE BALL BOUNCE, PRACTICE YOUR LINE, THEN RESET THE FEED", 494, 17, SKYBLUE);
            drawCentered(keyName(boundKey(BindAction::Restart)) + " RESETS THE SERVE AT ANY TIME", 527, 16, Color{176, 199, 219, 255});
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
            } else {
                drawAboutMenu();
            }
        } else if (state == MatchState::Loading) {
            drawLoadingScreen();
        } else if (state == MatchState::ServeCountdown) {
            if (scriptedAerialTest) {
                DrawRectangle(ScreenWidth / 2 - 210, 160, 420, 62, Color{7, 12, 22, 215});
                DrawRectangle(ScreenWidth / 2 - 210, 160, 7, 62, SKYBLUE);
                drawCentered("DOUBLE JUMP + AERIAL BOOST", 176, 23, GOLD);
            } else if (academyActive) {
                DrawRectangle(ScreenWidth / 2 - 290, 184, 580, 278, Color{7, 12, 22, 228});
                DrawRectangle(ScreenWidth / 2 - 290, 184, 8, 278, GOLD);
                drawCentered(TextFormat("LESSON %d / %d", academyLessonIndex() + 1, AcademyLessonCount), 211, 18, SKYBLUE);
                drawCentered(academyLessonName(), 244, 30, RAYWHITE);
                drawCentered(TextFormat("%d", std::max(1, static_cast<int>(std::ceil(serveCountdown)))), 294, 104, GOLD);
                drawCentered(academyLessonInstruction(), 411, 18, Color{188, 207, 223, 255});
                drawCentered(std::string("TAB SKIP  /  R RESTART  /  G PB GHOST ")
                        + (academyGhostEnabled ? "ON" : "OFF"),
                    437, 14, academyBestGhost.empty() ? Color{145, 170, 193, 255} : Color{115, 235, 255, 255});
            } else {
                DrawRectangle(ScreenWidth / 2 - 118, 205, 236, 238, Color{7, 12, 22, 225});
                DrawRectangle(ScreenWidth / 2 - 118, 205, 8, 238, GOLD);
                drawCentered(gameMode == GameMode::Training
                        ? "PRACTICE FEED"
                        : (gameMode == GameMode::TargetChallenge
                                ? "TARGET SHOT"
                                : (arcadeCupActive ? arcadeCupRoundName() : "GET READY")),
                    229, 24, RAYWHITE);
                drawCentered(TextFormat("%d", std::max(1, static_cast<int>(std::ceil(serveCountdown)))), 273, 112, GOLD);
                const std::string serveMessage = gameMode == GameMode::Training
                    ? "BALL INCOMING  /  " + keyName(boundKey(BindAction::Restart)) + " RESETS"
                    : (gameMode == GameMode::TargetChallenge
                            ? std::string("SHOT ") + std::to_string(trainingAttempts) + "  /  " + currentTrainingFeedName()
                            : (servingCar == 0 ? "YOU SERVE  /  HIT THE TOSS" : "AI SERVING  /  TAKE YOUR LANE"));
                drawCentered(serveMessage, 402, 17, Color{188, 207, 223, 255});
            }
        } else if (state == MatchState::Paused) {
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
            drawCentered(
                pausePrompt,
                345,
                22,
                RAYWHITE);
        } else if (state == MatchState::PointWon) {
            DrawRectangle(0, 210, ScreenWidth, 175, Color{7, 10, 18, 220});
            drawCentered(scoringTeam == 0 ? "BLUE SCORES!" : "ORANGE SCORES!", 242, 48, scoringTeam == 0 ? SKYBLUE : ORANGE);
            drawCentered(TextFormat("%d  -  %d", score[0], score[1]), 309, 30, RAYWHITE);
        } else if (state == MatchState::GoalReplay) {
            DrawRectangle(22, 100, 244, 68, Color{7, 10, 18, 225});
            DrawRectangle(22, 100, 7, 68, scoringTeam == 0 ? SKYBLUE : ORANGE);
            DrawText("INSTANT REPLAY", 43, 111, 23, GOLD);
            DrawText("SPACE / ENTER TO SKIP", 43, 140, 14, Color{186, 204, 220, 255});
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
                if (powerVolleyActive()) {
                    drawCentered(TextFormat("POWER-UPS USED  %d - %d", matchPowerupsUsed[0], matchPowerupsUsed[1]),
                        452, 17, Color{215, 95, 255, 255});
                }
                drawCentered("PRESS ENTER TO PLAY AGAIN", powerVolleyActive() ? 492 : 474, 23, GOLD);
                drawCentered(keyName(boundKey(BindAction::MainMenu)) + " MAIN MENU",
                    powerVolleyActive() ? 533 : 515, 17, Color{176, 199, 219, 255});
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
            drawCentered(servingCar == 0 ? "SERVE LIVE  /  HIT THE BALL" : "SERVE LIVE  /  AI APPROACHING", 183, 21, GOLD);
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
        BeginDrawing();
        ClearBackground(Color{8, 14, 27, 255});
        if (localSplitView) {
            drawLocalCoopViews();
        } else {
            drawBackdrop();
            drawWorld();
        }
        drawOverlay();
        if (localSplitView) {
            drawLocalCoopLabels();
        }
        EndDrawing();
    }

    int run() {
        float smokeElapsed = 0.0F;
        bool menuCaptured = false;
        bool cityArenaCaptured = false;
        bool customizeCaptured = false;
        bool controlsCaptured = false;
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
        bool replayTriggered = false;
        bool replayCaptured = false;
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
                }
                if (aboutCaptured && !arcadeCupStarted && smokeElapsed >= 2.85F) {
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
                    physics.setTransform(ball, {0.0F, BallRadius + 0.1F, 10.0F}, {});
                    physics.setLinearVelocity(ball, {});
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
                    powerupAiPassed = aiControls(cars[4], FixedStep).powerupPressed;

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
                    physics.setTransform(ball, {challengeTarget.x + 0.35F, BallRadius + 0.06F, challengeTarget.z + 0.25F}, {});
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
                        TakeScreenshot("rocket_volley_gameplay_ball_smoke.png");
                        ballCameraCaptured = true;
                    }
                }
                if (ballCameraCaptured && !replayTriggered && state == MatchState::Playing
                    && replayFrames.size() >= 30) {
                    scorePoint(0);
                    replayTriggered = true;
                }
                if (replayTriggered && !replayCaptured && state == MatchState::GoalReplay) {
                    replayPassed = currentReplayFrame() != nullptr && replayFrames.size() >= 30;
                    TakeScreenshot("rocket_volley_replay_smoke.png");
                    replayCaptured = true;
                    finishPointSequence();
                }
                if (!aerialTestStarted && ballCameraCaptured && replayCaptured
                    && std::max(bestRallyTouches, rallyTouches) >= 3) {
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
            TraceLog(LOG_INFO, "SMOKE: arcade_cup=%d%d%d best=%d/3 crowns=%d",
                arcadeCupLoadingCaptured, arcadeCupResultsCaptured, arcadeCupLogicPassed,
                arcadeCupBestRound, arcadeCupTitles);
            TraceLog(LOG_INFO, "SMOKE: pilot_record=%d rank=%s xp=%d matches=%d wins=%d milestones=%d/%d",
                careerProgressionPassed, careerRankName(), careerXp, careerMatches, careerWins,
                unlockedCareerMilestoneCount(), CareerMilestoneCount);
            if (!menuCaptured || !cityArenaCaptured || !customizeCaptured || !controlsCaptured || !aboutCaptured
                || !matchSetupCaptured || !matchSettingsPassed
                || !bindingTestPassed || !aboutLinksVerified || !loadingCaptured || !countdownCaptured
                || !careerProgressionPassed
                || !arcadeCupLoadingCaptured || !arcadeCupResultsCaptured || !arcadeCupLogicPassed
                || !multiplayerProtocolPassed || !localCoopPassed || !localCoopCaptured
                || !splitScreenPassed
                || !boostPadEconomyPassed || !boostAiRoutingPassed
                || !powerupGrantPassed || !haymakerPassed || !freezerPassed || !magnetizerPassed || !powerupAiPassed
                || !threeVsThreeLoadingSeen || !threeVsThreeCaptured || !threeVsThreePassed || !touchRulePassed
                || !trainingLoadingCaptured || !trainingCountdownCaptured || !trainingPlayCaptured
                || !trainingGroundPassed || !trainingResetPassed || !trainingFeedsPassed || !rookieSpeedPassed || !proSpeedPassed
                || !academyLoadingCaptured || !academyCountdownCaptured || !academyPlayCaptured
                || !academyObjectivesPassed || !academyResultsCaptured || !academyPersistencePassed
                || !academyGhostCodecPassed || !academyGhostPlaybackPassed || !academyGhostRecordPassed || !academySkipIntegrityPassed
                || !challengeLoadingCaptured || !challengeCountdownCaptured || !challengePlayCaptured
                || !challengeScoringPassed || !challengeResultsCaptured
                || !arenaTestPassed || !teammateLanePassed || !trueServeTossPassed || !trueServeContactPassed
                || BallRadius < 1.07F
                || !carCameraCaptured || !ballCameraCaptured || !replayPassed || !aerialCaptured || !aerialTestComplete
                || bestRallyTouches < 3 || aerialPeakHeight < 6.0F || aerialPeakForwardY < 0.3F
                || aerialMaxJumpsUsed != 2 || !sprintCompletedCourse || sprintFinishTime > 2.2F) {
                TraceLog(
                    LOG_ERROR,
                    "SMOKE: gate failed captures=%d%d%d%d%d%d%d%d%d training=%d%d%d%d speeds=%d%d arena=%d teammate=%d serve=%d%d binding=%d links=%d rally=%d aerial=(%.2f,%.2f,%d) sprint=(%d,%.2f)",
                    menuCaptured,
                    customizeCaptured,
                    controlsCaptured,
                    aboutCaptured,
                    loadingCaptured,
                    countdownCaptured,
                    carCameraCaptured,
                    ballCameraCaptured,
                    aerialCaptured,
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

Game::Game(bool smokeTest) : impl_(std::make_unique<Impl>(smokeTest)) {}

Game::~Game() = default;

int Game::run() {
    return impl_->run();
}

} // namespace rv
