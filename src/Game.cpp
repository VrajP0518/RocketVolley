#include "rocket_volley/Game.hpp"

#include "rocket_volley/MathTypes.hpp"
#include "rocket_volley/PhysicsWorld.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <raylib.h>
#include <raymath.h>
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
    GameOver,
};

enum class CameraMode {
    Car,
    Ball,
};

enum class MenuPage {
    Main,
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
    Training,
};

struct Controls {
    float throttle = 0.0F;
    float steer = 0.0F;
    bool jumpPressed = false;
    bool dodgePressed = false;
    bool boostHeld = false;
};

enum class BindAction : std::size_t {
    Forward,
    Reverse,
    SteerLeft,
    SteerRight,
    Jump,
    Boost,
    Dodge,
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
    "TOGGLE CAMERA",
    "PAUSE",
    "RESTART MATCH",
    "MAIN MENU",
};
constexpr std::array<const char *, BindingCount> BindingSettingNames{
    "forward", "reverse", "steer_left", "steer_right", "jump", "boost",
    "dodge", "camera", "pause", "restart", "main_menu"};
constexpr std::array<int, BindingCount> DefaultBindings{
    KEY_W,
    KEY_S,
    KEY_A,
    KEY_D,
    KEY_SPACE,
    KEY_LEFT_SHIFT,
    KEY_E,
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
    float boostPadCooldown = 0.0F;
    Vec3 aiTarget{};
    int jumpsUsed = 0;
    bool dodgeAvailable = true;
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
    {"NEON METEOR DOME", {7, 16, 39, 255}, {47, 24, 67, 255}, {31, 42, 58, 255},
        {22, 88, 119, 255}, {118, 40, 55, 255}, {255, 202, 44, 255}, {96, 178, 211, 115}},
    {"SOLAR REEF GARDENS", {3, 37, 52, 255}, {8, 77, 83, 255}, {24, 61, 66, 255},
        {16, 121, 132, 255}, {194, 75, 65, 255}, {255, 230, 112, 255}, {71, 224, 204, 115}},
    {"LUNAR CIRCUIT", {15, 10, 39, 255}, {42, 22, 76, 255}, {42, 39, 66, 255},
        {73, 70, 156, 255}, {121, 57, 145, 255}, {155, 244, 92, 255}, {178, 132, 255, 115}},
    {"EMBER CROWN COLISEUM", {39, 9, 13, 255}, {91, 28, 20, 255}, {61, 37, 36, 255},
        {44, 91, 119, 255}, {154, 54, 28, 255}, {255, 161, 52, 255}, {255, 111, 69, 115}},
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

struct Particle {
    Vec3 position{};
    Vec3 velocity{};
    float life = 0.0F;
    float initialLife = 0.0F;
    Color color = WHITE;
    float size = 0.12F;
};

float mirroredArenaCoordinate(float coordinate, float limit) {
    while (coordinate > limit || coordinate < -limit) {
        if (coordinate > limit) {
            coordinate = 2.0F * limit - coordinate;
        } else if (coordinate < -limit) {
            coordinate = -2.0F * limit - coordinate;
        }
    }
    return coordinate;
}

} // namespace

struct Game::Impl {
    PhysicsWorld physics;
    AudioKit audio;
    Model cubeModel{};
    Model ballModel{};
    RenderTexture2D previewTarget{};
    BodyHandle ball = InvalidBody;
    std::array<Car, 4> cars{};
    std::vector<Particle> particles;
    Camera3D camera{};
    MatchState state = MatchState::Title;
    MatchState pausedFrom = MatchState::Playing;
    MenuPage menuPage = MenuPage::Main;
    Difficulty difficulty = Difficulty::Pro;
    GameMode gameMode = GameMode::Match;
    GameMode pendingGameMode = GameMode::Match;
    CameraMode cameraMode = CameraMode::Car;
    std::array<int, 2> score{0, 0};
    float matchTime = 180.0F;
    float rallyTime = 0.0F;
    float pointTimer = 0.0F;
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
    Vec3 servePosition{};
    Vec3 serveVelocity{};
    Vec3 serveLandingTarget{};
    Vec3 previousBallVelocity{};
    int mainMenuIndex = 0;
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
    int servingCar = 2;
    std::array<int, 2> rotationStriker{0, 2};
    int rallyTouches = 0;
    int bestRallyTouches = 0;
    std::array<int, BindingCount> bindings = DefaultBindings;
    int bindingCaptureIndex = -1;
    std::string settingsNotice;
    float settingsNoticeTimer = 0.0F;
    bool pendingGameOver = false;
    bool overtime = false;
    bool showHelp = false;
    bool shouldExit = false;
    bool automatedPlayer = false;
    bool serveInProgress = false;
    bool trainingBallHasTouchedGround = false;
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

        camera.position = {0.0F, 8.0F, 18.0F};
        camera.target = {0.0F, 1.5F, 2.0F};
        camera.up = {0.0F, 1.0F, 0.0F};
        camera.fovy = 58.0F;
        camera.projection = CAMERA_PERSPECTIVE;

        loadSettings();
        createArena();
        createActors();
        resetRound(1);
    }

    ~Impl() {
        if (IsWindowReady()) {
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
        }

        bodyColorIndex = std::clamp(bodyColorIndex, 0, static_cast<int>(BodyColors.size()) - 1);
        wheelColorIndex = std::clamp(wheelColorIndex, 0, static_cast<int>(WheelColors.size()) - 1);
        spoilerColorIndex = std::clamp(spoilerColorIndex, 0, static_cast<int>(SpoilerColors.size()) - 1);
        arenaSelection = std::clamp(arenaSelection, 0, static_cast<int>(ArenaThemes.size()));
        if (arenaSelection > 0) {
            activeArenaIndex = arenaSelection - 1;
        }
        bindings = bindingsAreUnique(loadedBindings) ? loadedBindings : DefaultBindings;
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
        output << "version=1\n";
        for (std::size_t index = 0; index < BindingCount; ++index) {
            output << BindingSettingNames[index] << '=' << bindings[index] << '\n';
        }
        output << "body=" << bodyColorIndex << '\n';
        output << "wheels=" << wheelColorIndex << '\n';
        output << "spoiler=" << spoilerColorIndex << '\n';
        output << "arena=" << arenaSelection << '\n';
        output << "difficulty=" << (difficulty == Difficulty::Pro ? 1 : 0) << '\n';
        output << "ball_bounce=" << static_cast<int>(std::round(ballElasticity * 100.0F)) << '\n';
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

        constexpr std::array<Color, 4> colors{
            Color{43, 199, 255, 255}, Color{80, 112, 255, 255},
            Color{255, 76, 87, 255}, Color{255, 154, 54, 255}};
        for (int index = 0; index < static_cast<int>(cars.size()); ++index) {
            Car &car = cars[index];
            car.body = physics.createDynamicBox({0.0F, 0.58F, 0.0F}, {0.92F, 0.45F, 1.42F}, 140.0F, 0.22F);
            physics.setGravityFactor(car.body, 0.72F);
            car.team = index < 2 ? 0 : 1;
            car.slot = index;
            car.human = index == 0;
            car.paint = colors[index];
            car.wheelColor = Color{18, 20, 25, 255};
            car.spoilerColor = index < 2 ? SKYBLUE : ORANGE;
        }
        cars[0].paint = BodyColors[bodyColorIndex].color;
        cars[0].wheelColor = WheelColors[wheelColorIndex].color;
        cars[0].spoilerColor = SpoilerColors[spoilerColorIndex].color;
    }

    void resetCar(Car &car, Vec3 position, float heading) {
        car.heading = heading;
        car.boost = 100.0F;
        car.jumpCooldown = 0.0F;
        car.airborneTime = 0.0F;
        car.aiThinkTimer = 0.0F;
        car.touchCooldown = 0.0F;
        car.boostPadCooldown = 0.0F;
        car.aiTarget = position;
        car.jumpsUsed = 0;
        car.dodgeAvailable = true;
        physics.setTransform(car.body, position, yawRotation(heading));
        physics.setLinearVelocity(car.body, {});
        physics.setAngularVelocity(car.body, {});
    }

    void resetRound(int nextServingTeam) {
        gameMode = GameMode::Match;
        servingTeam = nextServingTeam;
        receivingTeam = 1 - servingTeam;
        servingCar = servingTeam == 0 ? 0 : 2;
        receivingCar = receivingTeam == 0 ? 0 : 2;
        rotationStriker = {0, 2};
        rotationStriker[servingTeam] = servingCar;
        rotationStriker[receivingTeam] = receivingCar;
        const float servingSide = servingTeam == 0 ? 1.0F : -1.0F;
        const float receivingSide = -servingSide;
        const float targetX = static_cast<float>(GetRandomValue(-25, 25)) * 0.1F;
        const float serveX = static_cast<float>(GetRandomValue(-22, 22)) * 0.1F;
        serveLandingTarget = {targetX, BallRadius + 0.08F, receivingSide * 10.6F};
        const float supportX = targetX >= 0.0F ? -6.0F : 6.0F;

        if (servingTeam == 0) {
            resetCar(cars[0], {serveX, 0.62F, 19.1F}, Pi);
            resetCar(cars[1], {serveX >= 0.0F ? -5.8F : 5.8F, 0.62F, 13.7F}, Pi);
            resetCar(cars[2], {targetX, 0.62F, -9.8F}, 0.0F);
            resetCar(cars[3], {supportX, 0.62F, -15.7F}, 0.0F);
        } else {
            resetCar(cars[0], {targetX, 0.62F, 9.8F}, Pi);
            resetCar(cars[1], {supportX, 0.62F, 15.7F}, Pi);
            resetCar(cars[2], {serveX, 0.62F, -19.1F}, 0.0F);
            resetCar(cars[3], {serveX >= 0.0F ? -5.8F : 5.8F, 0.62F, -13.7F}, 0.0F);
        }

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
        serveInProgress = true;
        serveCountdown = 3.0F;
        countdownCue = 4;
        accumulator = 0.0F;
    }

    void launchServe() {
        physics.setTransform(ball, servePosition, {});
        if (gameMode == GameMode::Training) {
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
        accumulator = 0.0F;
        state = MatchState::Playing;
    }

    void startMatch() {
        gameMode = GameMode::Match;
        score = {0, 0};
        matchTime = 180.0F;
        overtime = false;
        pendingGameOver = false;
        scoringTeam = 0;
        winner = 0;
        rallyTouches = 0;
        bestRallyTouches = 0;
        particles.clear();
        resetRound(1);
        state = MatchState::ServeCountdown;
    }

    void resetTrainingServe() {
        gameMode = GameMode::Training;
        applyDifficultyPhysics();
        servingTeam = 1;
        receivingTeam = 0;
        servingCar = -1;
        receivingCar = 0;
        resetCar(cars[0], {0.0F, 0.62F, 10.8F}, Pi);
        for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
            resetCar(cars[index], {32.0F + static_cast<float>(index) * 3.0F, -8.0F, 28.0F}, 0.0F);
        }

        const float targetX = static_cast<float>(GetRandomValue(-32, 32)) * 0.1F;
        servePosition = {
            static_cast<float>(GetRandomValue(-22, 22)) * 0.1F,
            7.2F,
            -15.0F};
        serveLandingTarget = {targetX, BallRadius + 0.08F, 8.8F};
        const float flightTime = difficulty == Difficulty::Rookie ? 2.2F : 1.72F;
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
        rallyTouches = 0;
        rallyTime = 0.0F;
        serveInProgress = false;
        trainingBallHasTouchedGround = false;
        serveCountdown = 3.0F;
        countdownCue = 4;
        accumulator = 0.0F;
        state = MatchState::ServeCountdown;
    }

    void startTraining() {
        score = {0, 0};
        bestRallyTouches = 0;
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
        beginLoading(GameMode::Training);
    }

    Controls playerControls() const {
        Controls controls;
        controls.throttle = static_cast<float>(IsKeyDown(boundKey(BindAction::Forward)))
            - static_cast<float>(IsKeyDown(boundKey(BindAction::Reverse)));
        controls.steer = static_cast<float>(IsKeyDown(boundKey(BindAction::SteerLeft)))
            - static_cast<float>(IsKeyDown(boundKey(BindAction::SteerRight)));
        controls.jumpPressed = IsKeyPressed(boundKey(BindAction::Jump));
        controls.dodgePressed = IsKeyPressed(boundKey(BindAction::Dodge));
        controls.boostHeld = IsKeyDown(boundKey(BindAction::Boost));

        if (IsGamepadAvailable(0)) {
            const float stickX = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
            const float stickY = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
            if (std::abs(stickX) > 0.16F) {
                controls.steer = -stickX;
            }
            if (std::abs(stickY) > 0.16F) {
                controls.throttle = -stickY;
            }
            controls.jumpPressed = controls.jumpPressed
                || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
            controls.dodgePressed = controls.dodgePressed
                || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
            controls.boostHeld = controls.boostHeld
                || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)
                || GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.25F;
        }
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

    void driveCar(Car &car, Controls controls, float deltaSeconds) {
        Transform transform = physics.transform(car.body);
        Vec3 velocity = physics.linearVelocity(car.body);
        const bool grounded = transform.position.y < 0.72F && std::abs(velocity.y) < 2.2F;
        car.jumpCooldown = std::max(0.0F, car.jumpCooldown - deltaSeconds);
        car.touchCooldown = std::max(0.0F, car.touchCooldown - deltaSeconds);
        car.boostPadCooldown = std::max(0.0F, car.boostPadCooldown - deltaSeconds);

        if (grounded) {
            car.airborneTime = 0.0F;
            car.jumpsUsed = 0;
            car.dodgeAvailable = true;
        } else {
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
            const Vec3 boostDirection = grounded ? forward : aerialForward;
            const float boostAcceleration = grounded ? 15.5F : 22.5F;
            velocity.x += boostDirection.x * boostAcceleration * deltaSeconds;
            velocity.y += boostDirection.y * boostAcceleration * deltaSeconds;
            velocity.z += boostDirection.z * boostAcceleration * deltaSeconds;
            car.boost = std::max(0.0F, car.boost - 28.0F * deltaSeconds);
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
        } else {
            car.boost = std::min(100.0F, car.boost + 9.0F * deltaSeconds);
        }

        if (grounded && car.boostPadCooldown <= 0.0F) {
            for (const Vec3 pad : BoostPadPositions) {
                if (length2D(subtract(transform.position, pad)) < 1.15F) {
                    car.boost = std::min(100.0F, car.boost + 32.0F);
                    car.boostPadCooldown = 2.2F;
                    emitBurst({pad.x, 0.12F, pad.z}, car.team == 0 ? SKYBLUE : ORANGE, 12, 3.0F, 0.1F);
                    if (car.human) {
                        audio.play(audio.boost);
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

        if (controls.jumpPressed && car.jumpCooldown <= 0.0F) {
            if (grounded) {
                physics.addImpulse(car.body, {0.0F, 900.0F, 0.0F});
                car.jumpsUsed = 1;
                car.jumpCooldown = 0.16F;
                emitBurst({transform.position.x, 0.1F, transform.position.z}, LIGHTGRAY, 9, 2.2F, 0.13F);
                if (car.human) {
                    audio.play(audio.jump);
                }
            } else if (car.jumpsUsed == 1 && car.airborneTime < 1.35F) {
                physics.addImpulse(car.body, {0.0F, 720.0F, 0.0F});
                car.jumpsUsed = 2;
                car.dodgeAvailable = false;
                car.jumpCooldown = 0.18F;
                if (car.human) {
                    audio.play(audio.jump);
                }
            }
        }

        if (controls.dodgePressed && !grounded && car.dodgeAvailable && car.airborneTime < 1.35F) {
            const Vec3 dodgeForward = forwardFromHeading(car.heading);
            physics.addImpulse(car.body, {dodgeForward.x * 540.0F, 150.0F, dodgeForward.z * 540.0F});
            physics.setAngularVelocity(car.body, {dodgeForward.z * 8.5F, 0.0F, -dodgeForward.x * 8.5F});
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
        const Vec3 velocity = physics.linearVelocity(ball);
        const float halfGravity = difficulty == Difficulty::Rookie ? 5.22F : 9.0F;
        Vec3 predicted{
            ballTransform.position.x + velocity.x * time,
            ballTransform.position.y + velocity.y * time - halfGravity * time * time,
            ballTransform.position.z + velocity.z * time};
        predicted.x = mirroredArenaCoordinate(predicted.x, ArenaHalfWidth - BallRadius - 0.25F);
        predicted.z = mirroredArenaCoordinate(predicted.z, ArenaHalfLength - BallRadius - 0.25F);
        predicted.y = std::max(BallRadius, predicted.y);
        return predicted;
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
        if (team == 1) {
            return rotationStriker[1];
        }
        if (automatedPlayer) {
            return rotationStriker[0];
        }
        const int first = team == 0 ? 0 : 2;
        const int second = first + 1;
        const float firstDistance = length2D(subtract(landingTarget, physics.transform(cars[first].body).position));
        const float secondDistance = length2D(subtract(landingTarget, physics.transform(cars[second].body).position));
        return secondDistance + 3.8F < firstDistance ? second : first;
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
        const bool striker = strikerForTeam(car.team, landingTarget) == car.slot;
        car.aiThinkTimer -= deltaSeconds;

        if (car.aiThinkTimer <= 0.0F) {
            const bool pro = difficulty == Difficulty::Pro;
            car.aiThinkTimer = pro ? 0.055F : 0.13F;
            Vec3 target{};

            if (serveInProgress && car.slot == servingCar) {
                target = ballTransform.position;
                target.z += teamDirection * 0.45F;
            } else if (ballThreatensTeam && striker) {
                target = contactTarget;
                if (target.z * teamDirection < 0.8F) {
                    target = landingTarget;
                }
                target.z += teamDirection * (pro ? 0.2F : 0.1F);
            } else {
                const bool backSlot = car.slot == 1 || car.slot == 3;
                const float homeX = backSlot ? 2.6F : -2.6F;
                target = {homeX, 0.0F, teamDirection * (backSlot ? 13.5F : 9.0F)};
                if (ballThreatensTeam && !striker) {
                    target.x = clamp(landingTarget.x * -0.55F, -6.5F, 6.5F);
                    target.z = teamDirection * 13.5F;
                }
            }

            if (car.team == 0 && car.slot == 1 && !striker) {
                const Vec3 playerPosition = physics.transform(cars[0].body).position;
                target.x = playerPosition.x >= 0.0F ? -6.8F : 6.8F;
                target.z = 15.6F;
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
        return controls;
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
                bool carWasClose = false;
                for (const Car &car : cars) {
                    if (length(subtract(ballPosition, physics.transform(car.body).position)) < 3.1F) {
                        carWasClose = true;
                        break;
                    }
                }
                if (carWasClose) {
                    ++rallyTouches;
                    serveInProgress = false;
                    bestRallyTouches = std::max(bestRallyTouches, rallyTouches);
                    rallyTouchCooldown = 0.14F;
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
        const int teamFirstCar = car.team == 0 ? 0 : 2;
        const int teamSecondCar = teamFirstCar + 1;
        rotationStriker[car.team] = car.slot == teamFirstCar ? teamSecondCar : teamFirstCar;
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

    void scorePoint(int team) {
        if (automatedPlayer) {
            const Vec3 ballPosition = physics.transform(ball).position;
            const Vec3 car0Position = physics.transform(cars[0].body).position;
            const Vec3 car1Position = physics.transform(cars[1].body).position;
            const Vec3 car2Position = physics.transform(cars[2].body).position;
            const Vec3 car3Position = physics.transform(cars[3].body).position;
            TraceLog(
                LOG_WARNING,
                "RALLY: point team=%d after_touches=%d ball=(%.1f,%.1f,%.1f) car_z=(%.1f,%.1f,%.1f,%.1f) distances=(%.1f,%.1f,%.1f,%.1f)",
                team,
                rallyTouches,
                ballPosition.x,
                ballPosition.y,
                ballPosition.z,
                car0Position.z,
                car1Position.z,
                car2Position.z,
                car3Position.z,
                length2D(subtract(ballPosition, car0Position)),
                length2D(subtract(ballPosition, car1Position)),
                length2D(subtract(ballPosition, car2Position)),
                length2D(subtract(ballPosition, car3Position)));
        }
        ++score[team];
        scoringTeam = team;
        winner = team;
        pendingGameOver = score[team] >= 7 || overtime;
        pointTimer = 2.1F;
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
        constexpr float RookieMaximumBallSpeed = 15.0F;
        if (speed > RookieMaximumBallSpeed) {
            const float scale = RookieMaximumBallSpeed / speed;
            velocity.x *= scale;
            velocity.y *= scale;
            velocity.z *= scale;
            physics.setLinearVelocity(ball, velocity);
        }
    }

    void fixedUpdate(Controls controls) {
        aiBallTouchCooldown = std::max(0.0F, aiBallTouchCooldown - FixedStep);
        driveCar(cars[0], controls, FixedStep);
        if (gameMode == GameMode::Training) {
            physics.step(FixedStep);
            rallyTime += FixedStep;
            applyRookieBallAssist();
            checkBallImpact(physics.linearVelocity(ball));
            const Vec3 position = physics.transform(ball).position;
            if (position.y <= BallRadius + 0.12F) {
                trainingBallHasTouchedGround = true;
            }
            if (position.y < -5.0F || std::abs(position.x) > 22.0F || std::abs(position.z) > 30.0F) {
                resetTrainingServe();
            }
            return;
        }
        for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
            driveCar(cars[index], aiControls(cars[index], FixedStep), FixedStep);
        }

        physics.step(FixedStep);
        applyRookieBallAssist();
        rallyTime += FixedStep;
        if (!overtime) {
            matchTime = std::max(0.0F, matchTime - FixedStep);
        }

        const Transform ballTransform = physics.transform(ball);
        const Vec3 ballVelocity = physics.linearVelocity(ball);
        checkBallImpact(ballVelocity);
        for (Car &car : cars) {
            if (tryAiBallTouch(car)) {
                break;
            }
        }

        if (rallyTime > 0.85F && ballTransform.position.y <= BallRadius + 0.12F) {
            scorePoint(ballTransform.position.z >= 0.0F ? 1 : 0);
            return;
        }

        if (!overtime && matchTime <= 0.0F) {
            if (score[0] == score[1]) {
                overtime = true;
            } else {
                winner = score[0] > score[1] ? 0 : 1;
                state = MatchState::GameOver;
            }
        }
    }

    void countdownFixedUpdate(Controls controls) {
        driveCar(cars[0], controls, FixedStep);
        if (gameMode == GameMode::Match) {
            for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
                driveCar(cars[index], {}, FixedStep);
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
    }

    void updateCamera(float deltaSeconds) {
        Vector3 desiredPosition{};
        Vector3 desiredTarget{};
        float desiredFov = 58.0F;
        if (state == MatchState::Title || state == MatchState::Loading) {
            const float angle = totalTime * 0.22F;
            desiredPosition = {std::sin(angle) * 31.0F, 12.5F, std::cos(angle) * 31.0F};
            desiredTarget = {0.0F, 1.8F, 0.0F};
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
            desiredPosition = {
                player.position.x - forward.x * 8.8F,
                player.position.y + 5.2F,
                player.position.z - forward.z * 8.8F};
            desiredTarget = {
                player.position.x + forward.x * 3.2F,
                player.position.y + 1.25F,
                player.position.z + forward.z * 3.2F};
            desiredFov = 58.0F + speedFraction * 9.0F;
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
        int itemCount = 8;
        if (menuPage == MenuPage::Customize) {
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
        if (menuPage == MenuPage::Main && mainMenuIndex == 2 && (left || right)) {
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
                beginLoadingMatch();
            } else if (mainMenuIndex == 1) {
                beginLoadingTraining();
            } else if (mainMenuIndex == 2) {
                cycleArena(1);
            } else if (mainMenuIndex == 3) {
                menuPage = MenuPage::Customize;
                customizeMenuIndex = 0;
            } else if (mainMenuIndex == 4) {
                menuPage = MenuPage::Controls;
                controlsMenuIndex = 0;
            } else if (mainMenuIndex == 5) {
                difficulty = difficulty == Difficulty::Pro ? Difficulty::Rookie : Difficulty::Pro;
                applyDifficultyPhysics();
                saveSettings("DIFFICULTY SAVED");
            } else if (mainMenuIndex == 6) {
                menuPage = MenuPage::About;
                aboutMenuIndex = 0;
            } else {
                shouldExit = true;
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
        } else {
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
        if (IsKeyPressed(KEY_ONE)) {
            difficulty = Difficulty::Rookie;
            applyDifficultyPhysics();
            saveSettings("DIFFICULTY SAVED");
        }
        if (IsKeyPressed(KEY_TWO)) {
            difficulty = Difficulty::Pro;
            applyDifficultyPhysics();
            saveSettings("DIFFICULTY SAVED");
        }
        if (IsKeyPressed(KEY_LEFT_BRACKET)) {
            ballElasticity = std::max(0.55F, ballElasticity - 0.05F);
            physics.setRestitution(ball, ballElasticity);
            saveSettings("BALL BOUNCE SAVED");
        }
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
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
            if (gameMode == GameMode::Training) {
                resetTrainingServe();
            } else {
                startMatch();
            }
        }
        if (IsKeyPressed(boundKey(BindAction::MainMenu)) && state != MatchState::Title) {
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
            && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))) {
            beginLoadingMatch();
        }
    }

    void update(float deltaSeconds) {
        totalTime += deltaSeconds;
        boostSoundCooldown = std::max(0.0F, boostSoundCooldown - deltaSeconds);
        cameraModeNotice = std::max(0.0F, cameraModeNotice - deltaSeconds);
        settingsNoticeTimer = std::max(0.0F, settingsNoticeTimer - deltaSeconds);
        handleGlobalInput();
        audio.updateMusic(state == MatchState::Title || state == MatchState::Loading);

        if (state == MatchState::Loading) {
            loadingTimer += deltaSeconds;
            if (loadingTimer >= 1.6F) {
                if (pendingGameMode == GameMode::Training) {
                    startTraining();
                } else {
                    startMatch();
                }
            }
        } else if (state == MatchState::Playing || state == MatchState::ServeCountdown) {
            Controls controls{};
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
                    : playerControls();
            }
            accumulator = std::min(accumulator + deltaSeconds, 0.2F);
            bool firstStep = true;
            const MatchState activeState = state;
            while (accumulator >= FixedStep && state == activeState) {
                Controls stepControls = controls;
                if (!firstStep) {
                    stepControls.jumpPressed = false;
                    stepControls.dodgePressed = false;
                }
                if (activeState == MatchState::Playing) {
                    fixedUpdate(stepControls);
                } else {
                    countdownFixedUpdate(stepControls);
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
                if (pendingGameOver) {
                    state = MatchState::GameOver;
                } else {
                    resetRound(scoringTeam);
                    state = MatchState::ServeCountdown;
                }
            }
        }

        updateParticles(deltaSeconds);
        updateCamera(deltaSeconds);
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

        for (const Vec3 padPosition : BoostPadPositions) {
            const Vector3 pad = toRay(padPosition);
            const bool blueSide = pad.z > 0.0F;
            const Color glow = withAlpha(blueSide ? scaledColor(theme.blueCourt, 1.7F) : scaledColor(theme.orangeCourt, 1.7F), 210);
            const float pulse = 0.62F + 0.08F * std::sin(totalTime * 4.0F + padPosition.x + padPosition.z);
            DrawCylinder(pad, pulse, pulse, 0.035F, 12, withAlpha(glow, 70));
            DrawCylinderWires(pad, pulse, pulse, 0.04F, 12, glow);
            DrawSphere({pad.x, 0.18F + 0.06F * std::sin(totalTime * 5.0F + pad.z), pad.z}, 0.11F, glow);
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
            const Vec3 markerPosition = gameMode == GameMode::Training ? serveLandingTarget : servePosition;
            const Color landingColor = gameMode == GameMode::Training
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

        for (int cornerX : {-1, 1}) {
            for (int cornerZ : {-1, 1}) {
                const Color meteorColor = cornerZ > 0 ? scaledColor(theme.blueCourt, 1.65F) : scaledColor(theme.orangeCourt, 1.65F);
                DrawCube(
                    {static_cast<float>(cornerX) * 15.0F, 6.3F, static_cast<float>(cornerZ) * 22.9F},
                    0.3F,
                    12.6F,
                    0.3F,
                    meteorColor);
                const Vector3 beacon{
                    static_cast<float>(cornerX) * 15.0F,
                    13.0F + 0.18F * std::sin(totalTime * 2.4F + static_cast<float>(cornerX + cornerZ)),
                    static_cast<float>(cornerZ) * 22.9F};
                DrawSphere(beacon, 0.62F, withAlpha(meteorColor, 220));
                DrawSphereWires(beacon, 0.86F, 8, 12, theme.accent);
                DrawLine3D(
                    beacon,
                    {beacon.x - static_cast<float>(cornerX) * 2.8F, beacon.y + 2.1F, beacon.z - static_cast<float>(cornerZ) * 2.8F},
                    withAlpha(theme.accent, 150));
            }
        }
    }

    void drawCarGeometry(
        Vector3 position,
        Quaternion rotation,
        Color paint,
        Color wheelColor,
        Color spoilerColor,
        bool drawShadow) const {
        Vector3 axis{};
        float angle = 0.0F;
        QuaternionToAxisAngle(rotation, &axis, &angle);
        if (Vector3Length(axis) < 0.001F) {
            axis = {0.0F, 1.0F, 0.0F};
        }
        if (drawShadow) {
            DrawCylinder({position.x, 0.035F, position.z}, 1.12F, 1.12F, 0.025F, 16, Color{0, 0, 0, 90});
        }
        DrawModelEx(cubeModel, position, axis, angle * RAD2DEG, {1.84F, 0.9F, 2.84F}, paint);

        const Vector3 cabinOffset = Vector3RotateByQuaternion({0.0F, 0.58F, -0.12F}, rotation);
        const Vector3 cabinPosition = Vector3Add(position, cabinOffset);
        DrawModelEx(cubeModel, cabinPosition, axis, angle * RAD2DEG, {1.35F, 0.58F, 1.25F}, Color{30, 42, 60, 255});

        constexpr std::array<Vector3, 4> wheelOffsets{
            Vector3{-0.98F, -0.35F, -0.86F}, Vector3{0.98F, -0.35F, -0.86F},
            Vector3{-0.98F, -0.35F, 0.86F}, Vector3{0.98F, -0.35F, 0.86F}};
        for (Vector3 offset : wheelOffsets) {
            const Vector3 wheelPosition = Vector3Add(position, Vector3RotateByQuaternion(offset, rotation));
            DrawSphere(wheelPosition, 0.34F, wheelColor);
        }

        const Vector3 noseOffset = Vector3RotateByQuaternion({0.0F, 0.03F, 1.46F}, rotation);
        const Vector3 nosePosition = Vector3Add(position, noseOffset);
        DrawModelEx(cubeModel, nosePosition, axis, angle * RAD2DEG, {1.45F, 0.22F, 0.12F}, spoilerColor);

        for (float side : {-0.58F, 0.58F}) {
            const Vector3 strutOffset = Vector3RotateByQuaternion({side, 0.54F, -1.18F}, rotation);
            DrawModelEx(
                cubeModel,
                Vector3Add(position, strutOffset),
                axis,
                angle * RAD2DEG,
                {0.11F, 0.58F, 0.12F},
                spoilerColor);
        }
        const Vector3 wingOffset = Vector3RotateByQuaternion({0.0F, 0.84F, -1.36F}, rotation);
        DrawModelEx(
            cubeModel,
            Vector3Add(position, wingOffset),
            axis,
            angle * RAD2DEG,
            {1.82F, 0.15F, 0.4F},
            spoilerColor);
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

    void drawBall() const {
        const Transform transform = physics.transform(ball);
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

    void drawParticles() const {
        for (const Particle &particle : particles) {
            const float fraction = clamp(particle.life / particle.initialLife, 0.0F, 1.0F);
            DrawCubeV(
                toRay(particle.position),
                {particle.size * fraction, particle.size * fraction, particle.size * fraction},
                withAlpha(particle.color, static_cast<unsigned char>(255.0F * fraction)));
        }
    }

    void drawBackdrop() const {
        const ArenaTheme &theme = activeArena();
        DrawRectangleGradientV(
            0,
            0,
            ScreenWidth,
            ScreenHeight,
            theme.skyTop,
            theme.skyBottom);
        const float planetX = activeArenaIndex % 2 == 0 ? 1050.0F : 214.0F;
        const float planetY = activeArenaIndex == 2 ? 148.0F : 122.0F;
        const float planetRadius = activeArenaIndex == 3 ? 96.0F : 74.0F;
        DrawCircleGradient({planetX, planetY}, 260.0F, withAlpha(theme.accent, 72), withAlpha(theme.skyTop, 0));
        DrawCircleGradient({ScreenWidth - planetX, 178.0F}, 210.0F, withAlpha(theme.blueCourt, 68), withAlpha(theme.skyTop, 0));
        DrawCircleGradient({planetX, planetY}, planetRadius, scaledColor(theme.accent, 1.15F), scaledColor(theme.orangeCourt, 0.72F));
        if (activeArenaIndex != 3) {
            DrawEllipseLines(static_cast<int>(planetX), static_cast<int>(planetY), 118.0F, 28.0F, withAlpha(theme.accent, 180));
            DrawEllipseLines(static_cast<int>(planetX), static_cast<int>(planetY), 103.0F, 23.0F, withAlpha(theme.orangeCourt, 140));
        } else {
            for (int ray = 0; ray < 12; ++ray) {
                const float angle = static_cast<float>(ray) * Pi / 6.0F + totalTime * 0.06F;
                DrawLineEx(
                    {planetX + std::cos(angle) * 112.0F, planetY + std::sin(angle) * 112.0F},
                    {planetX + std::cos(angle) * 151.0F, planetY + std::sin(angle) * 151.0F},
                    3.0F,
                    withAlpha(theme.accent, 125));
            }
        }
        for (int streak = 0; streak < 5; ++streak) {
            const float drift = std::fmod(totalTime * (32.0F + streak * 5.0F) + streak * 210.0F, 1450.0F);
            const int x = static_cast<int>(1450.0F - drift);
            const int y = 64 + streak * 51;
            DrawLineEx({static_cast<float>(x), static_cast<float>(y)}, {static_cast<float>(x - 48), static_cast<float>(y + 20)}, 2.0F, withAlpha(theme.accent, 105));
        }
        for (int index = 0; index < 55; ++index) {
            const int x = (index * 173 + 41) % ScreenWidth;
            const int y = (index * 67 + 29) % 360;
            const float pulse = 0.65F + 0.35F * std::sin(totalTime * 1.8F + static_cast<float>(index));
            DrawCircle(x, y, index % 7 == 0 ? 2.0F : 1.0F, withAlpha(RAYWHITE, static_cast<unsigned char>(90.0F * pulse)));
        }
        for (int index = 0; index < 22; ++index) {
            const int width = 34 + (index * 19) % 46;
            const int height = 42 + (index * 31) % 120;
            const int x = index * 64 - 36;
            DrawRectangle(x, ScreenHeight - height - 42, width, height, withAlpha(scaledColor(theme.skyTop, 0.58F), 220));
            if (index % 2 == 0) {
                DrawRectangle(x + 9, ScreenHeight - height - 22, 5, 5,
                    index % 4 == 0 ? scaledColor(theme.blueCourt, 1.65F) : scaledColor(theme.orangeCourt, 1.65F));
            }
        }
    }

    void drawWorld() const {
        BeginMode3D(camera);
        drawArena();
        for (int index = 0; index < static_cast<int>(cars.size()); ++index) {
            if (gameMode != GameMode::Training || index == 0) {
                drawCar(cars[index]);
            }
        }
        drawBall();
        drawParticles();
        EndMode3D();
    }

    void drawHud() const {
        DrawRectangle(0, 0, ScreenWidth, 82, Color{9, 14, 24, 230});
        DrawRectangle(ScreenWidth / 2 - 142, 12, 284, 58, Color{24, 34, 52, 245});
        DrawRectangle(ScreenWidth / 2 - 142, 12, 7, 58, SKYBLUE);
        DrawRectangle(ScreenWidth / 2 + 135, 12, 7, 58, ORANGE);
        if (gameMode == GameMode::Training) {
            drawCentered("TRAINING", 28, 27, GOLD);
        } else {
            DrawText(TextFormat("%d", score[0]), ScreenWidth / 2 - 92, 17, 42, SKYBLUE);
            DrawText("-", ScreenWidth / 2 - 7, 20, 36, LIGHTGRAY);
            DrawText(TextFormat("%d", score[1]), ScreenWidth / 2 + 66, 17, 42, ORANGE);
        }

        std::string timerText;
        if (gameMode == GameMode::Training) {
            timerText = "FREE PLAY";
        } else if (overtime) {
            timerText = "OVERTIME";
        } else {
            const int seconds = static_cast<int>(std::ceil(matchTime));
            timerText = TextFormat("%d:%02d", seconds / 60, seconds % 60);
        }
        drawCentered(timerText, 88, 26, overtime ? GOLD : RAYWHITE);
        if (state == MatchState::Playing && rallyTouches > 0) {
            DrawRectangle(ScreenWidth / 2 - 76, 121, 152, 30, Color{9, 14, 24, 210});
            drawCentered(TextFormat("RALLY  %d", rallyTouches), 126, 18, GOLD);
        }

        DrawText(gameMode == GameMode::Training ? "SOLO" : "BLUE", 24, 18, 22, SKYBLUE);
        DrawText(gameMode == GameMode::Training ? "NO SCORE" : "ORANGE", ScreenWidth - 117, 18, 22, ORANGE);
        const char *difficultyLabel = gameMode == GameMode::Training
            ? (difficulty == Difficulty::Pro ? "FEED: PRO [2]" : "FEED: ROOKIE [1]")
            : (difficulty == Difficulty::Pro ? "AI: PRO [2]" : "AI: ROOKIE [1]");
        DrawText(difficultyLabel, 24, 50, 16, Color{180, 196, 215, 255});
        DrawText(
            difficulty == Difficulty::Pro ? "BALL SPEED: REGULAR" : "BALL SPEED: LEARNING",
            ScreenWidth - 224,
            50,
            16,
            Color{180, 196, 215, 255});
        DrawText(activeArena().name, 24, 91, 15, withAlpha(activeArena().accent, 225));

        constexpr int meterWidth = 220;
        const int meterX = 26;
        const int meterY = ScreenHeight - 48;
        DrawRectangle(meterX, meterY, meterWidth, 18, Color{12, 18, 28, 220});
        DrawRectangle(meterX + 3, meterY + 3, static_cast<int>((meterWidth - 6) * cars[0].boost / 100.0F), 12, GOLD);
        DrawText("BOOST", meterX, meterY - 21, 16, RAYWHITE);
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
        if (state == MatchState::Playing && gameMode == GameMode::Match) {
            const Vec3 landingTarget = predictBall(ballLandingTime());
            const bool partnerChasing = landingTarget.z > 0.0F && strikerForTeam(0, landingTarget) == 1;
            DrawText(
                partnerChasing ? "TEAMMATE: CHASING" : "TEAMMATE: COVERING",
                ScreenWidth - 232,
                ScreenHeight - 58,
                16,
                partnerChasing ? SKYBLUE : Color{180, 196, 215, 255});
        }
        const std::string cameraPrompt = std::string(cameraMode == CameraMode::Ball ? "CAM: BALL  [" : "CAM: CAR  [")
            + keyName(boundKey(BindAction::Camera)) + " / Y]";
        DrawText(
            cameraPrompt.c_str(),
            ScreenWidth - 189,
            ScreenHeight - 82,
            16,
            cameraMode == CameraMode::Ball ? GOLD : Color{180, 196, 215, 255});
        DrawText("F1 CONTROLS", ScreenWidth - 129, ScreenHeight - 31, 16, Color{180, 196, 215, 255});
        if (gameMode == GameMode::Training) {
            DrawText(
                ("RESET SERVE  [" + keyName(boundKey(BindAction::Restart)) + "]").c_str(),
                ScreenWidth - 244,
                ScreenHeight - 58,
                16,
                GOLD);
        }

        if (cameraModeNotice > 0.0F) {
            DrawRectangle(ScreenWidth / 2 - 142, 164, 284, 48, Color{7, 12, 22, 225});
            drawCentered(cameraMode == CameraMode::Ball ? "BALL CAM" : "CAR CAM", 177, 24, GOLD);
        }
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
        drawCentered("2v2 RETRO CAR VOLLEYBALL  /  DOUBLE-JUMP AERIALS", 194, 17, RAYWHITE);

        const int x = ScreenWidth / 2 - 285;
        constexpr int width = 570;
        constexpr int startY = 224;
        constexpr int rowStep = 42;
        drawCompactMenuRow("START 2V2 MATCH", 0, mainMenuIndex, x, startY, width);
        drawCompactMenuRow("TRAINING  /  SOLO SERVE PRACTICE", 1, mainMenuIndex, x, startY + rowStep, width);
        drawCompactMenuRow("ARENA     < " + arenaSelectionName() + " >", 2, mainMenuIndex, x, startY + rowStep * 2, width);
        drawCompactMenuRow("CUSTOMIZE CAR", 3, mainMenuIndex, x, startY + rowStep * 3, width);
        drawCompactMenuRow("CONTROLS", 4, mainMenuIndex, x, startY + rowStep * 4, width);
        drawCompactMenuRow(
            difficulty == Difficulty::Pro ? "AI DIFFICULTY: PRO" : "AI DIFFICULTY: ROOKIE",
            5,
            mainMenuIndex,
            x,
            startY + rowStep * 5,
            width);
        drawCompactMenuRow("ABOUT", 6, mainMenuIndex, x, startY + rowStep * 6, width);
        drawCompactMenuRow("QUIT", 7, mainMenuIndex, x, startY + rowStep * 7, width);

        drawCentered("W/S MOVE     A/D CHANGE     ENTER SELECT", 585, 17, Color{204, 218, 230, 255});
        drawCentered("FIRST TO 7 WINS  /  SETTINGS SAVE AUTOMATICALLY", 620, 15, Color{150, 174, 196, 255});
        if (settingsNoticeTimer > 0.0F) {
            drawCentered(settingsNotice, 654, 16, GOLD);
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
        constexpr int rowStart = 132;
        constexpr int rowStep = 36;
        for (std::size_t index = 0; index < BindingCount; ++index) {
            const std::string label = std::string(BindingLabels[index]) + "   [ " + keyName(bindings[index]) + " ]";
            drawCompactMenuRow(label, static_cast<int>(index), controlsMenuIndex, rowX, rowStart + static_cast<int>(index) * rowStep, rowWidth);
        }
        drawCompactMenuRow("RESET DEFAULTS", static_cast<int>(BindingCount), controlsMenuIndex, rowX, 540, rowWidth);
        drawCompactMenuRow("BACK", static_cast<int>(BindingCount) + 1, controlsMenuIndex, rowX, 580, rowWidth);

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
        DrawText("MENU ACCESS", 718, 454, 17, SKYBLUE);
        DrawText("W/S or arrows  /  Enter select", 718, 483, 17, RAYWHITE);
        DrawText("F1 help and Enter stay reserved.", 718, 524, 15, Color{164, 185, 205, 255});

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
        DrawText("ABOUT", 70, 53, 48, SKYBLUE);
        DrawText("ROCKET VOLLEY", 72, 112, 31, ORANGE);
        DrawText(TextFormat("VERSION %s", ROCKET_VOLLEY_VERSION), 74, 161, 18, GOLD);

        DrawRectangle(70, 205, 535, 350, Color{12, 21, 35, 238});
        DrawRectangle(70, 205, 6, 350, SKYBLUE);
        DrawText("CREATED BY VRAJ PATEL", 100, 235, 24, RAYWHITE);
        DrawText("@VrajP0518", 100, 271, 17, SKYBLUE);
        DrawText("Original retro 2v2 car volleyball", 100, 326, 18, RAYWHITE);
        DrawText("built in C++20 with raylib and Jolt Physics.", 100, 356, 18, RAYWHITE);
        DrawText("CURRENT MODE", 100, 410, 16, GOLD);
        DrawText("2v2 vs AI plus solo serve training", 100, 439, 17, RAYWHITE);
        DrawText("ONLINE MULTIPLAYER", 100, 482, 16, GOLD);
        DrawText("Planned on the roadmap / not yet live", 100, 511, 17, RAYWHITE);

        DrawText("PROJECT LINKS", 680, 205, 21, GOLD);
        drawMenuRow("OPEN GITHUB REPOSITORY", 0, aboutMenuIndex, 680, 249, 510);
        drawMenuRow("REPORT AN ISSUE", 1, aboutMenuIndex, 680, 313, 510);
        drawMenuRow("VIEW RELEASE / ONLINE ROADMAP", 2, aboutMenuIndex, 680, 377, 510);
        drawMenuRow("BACK", 3, aboutMenuIndex, 680, 457, 510);
        DrawText("raylib: zlib/libpng  /  Jolt Physics: MIT", 680, 539, 15, Color{162, 185, 205, 255});
        DrawText("Links open in your default browser.", 680, 571, 15, Color{162, 185, 205, 255});
        DrawText("W / S MOVE     ENTER OPEN     ESC BACK", 680, 624, 16, RAYWHITE);
    }

    void drawHelp() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{5, 8, 15, 215});
        DrawRectangle(ScreenWidth / 2 - 330, 120, 660, 470, Color{18, 27, 43, 248});
        DrawRectangle(ScreenWidth / 2 - 330, 120, 8, 470, GOLD);
        drawCentered("CONTROLS", 154, 34, GOLD);
        drawCentered(keyName(boundKey(BindAction::Forward)) + " / " + keyName(boundKey(BindAction::Reverse)) + "     Accelerate / brake", 222, 23, RAYWHITE);
        drawCentered(keyName(boundKey(BindAction::SteerLeft)) + " / " + keyName(boundKey(BindAction::SteerRight)) + "     Steer", 264, 23, RAYWHITE);
        drawCentered(keyName(boundKey(BindAction::Jump)) + " / A    Jump, then jump again", 306, 23, RAYWHITE);
        drawCentered("AIR: " + keyName(boundKey(BindAction::Reverse)) + " / STICK DOWN NOSE UP, " + keyName(boundKey(BindAction::Boost)) + " / RT BOOST", 348, 19, RAYWHITE);
        drawCentered(keyName(boundKey(BindAction::Dodge)) + " / X        Forward dodge", 390, 23, RAYWHITE);
        drawCentered(keyName(boundKey(BindAction::Camera)) + " / Y        Toggle Car Cam / Ball Cam", 432, 23, RAYWHITE);
        drawCentered(keyName(boundKey(BindAction::Pause)) + " pause   1/2 AI   [/] ball bounce", 474, 20, RAYWHITE);
        drawCentered("Gamepad: left stick, A jump, X dodge, B or RT boost", 508, 18, Color{176, 199, 219, 255});
        drawCentered(keyName(boundKey(BindAction::Restart)) + " resets the match or training serve", 538, 17, Color{176, 199, 219, 255});
        drawCentered("F1 TO CLOSE", 568, 18, GOLD);
    }

    void drawLoadingScreen() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 8, 18, 242});
        DrawCircleGradient({ScreenWidth / 2.0F, 255.0F}, 190.0F, Color{48, 139, 190, 75}, Color{4, 8, 18, 0});
        drawCentered(activeArena().name, 174, 24, activeArena().accent);
        drawCentered(pendingGameMode == GameMode::Training ? "PREPARING TRAINING" : "PREPARING ARENA", 218, 48, RAYWHITE);
        drawCentered(
            pendingGameMode == GameMode::Training
                ? "Loading a solo court and repeatable serve feed"
                : "Synchronizing cars, cameras, and meteor shields",
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
        if (pendingGameMode == GameMode::Training) {
            drawCentered("LET THE BALL BOUNCE, PRACTICE YOUR LINE, THEN RESET THE FEED", 494, 17, SKYBLUE);
            drawCentered(keyName(boundKey(BindAction::Restart)) + " RESETS THE SERVE AT ANY TIME", 527, 16, Color{176, 199, 219, 255});
        } else {
            drawCentered("TIP: " + keyName(boundKey(BindAction::Jump)) + " TWICE, TILT NOSE-UP WITH " + keyName(boundKey(BindAction::Reverse)) + ", THEN BOOST", 494, 17, SKYBLUE);
            drawCentered("THE SERVING CAR MUST HIT THE TOSS BEFORE IT DROPS", 527, 16, Color{176, 199, 219, 255});
        }
    }

    void drawOverlay() const {
        if (state != MatchState::Title && state != MatchState::Loading) {
            drawHud();
        }

        if (state == MatchState::Title) {
            if (menuPage == MenuPage::Main) {
                drawMainMenu();
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
            } else {
                DrawRectangle(ScreenWidth / 2 - 118, 205, 236, 238, Color{7, 12, 22, 225});
                DrawRectangle(ScreenWidth / 2 - 118, 205, 8, 238, GOLD);
                drawCentered(gameMode == GameMode::Training ? "PRACTICE FEED" : "GET READY", 229, 24, RAYWHITE);
                drawCentered(TextFormat("%d", std::max(1, static_cast<int>(std::ceil(serveCountdown)))), 273, 112, GOLD);
                const std::string serveMessage = gameMode == GameMode::Training
                    ? "BALL INCOMING  /  " + keyName(boundKey(BindAction::Restart)) + " RESETS"
                    : (servingCar == 0 ? "YOU SERVE  /  HIT THE TOSS" : "AI SERVING  /  TAKE YOUR LANE");
                drawCentered(serveMessage, 402, 17, Color{188, 207, 223, 255});
            }
        } else if (state == MatchState::Paused) {
            DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 7, 14, 190});
            drawCentered("PAUSED", 270, 55, GOLD);
            const std::string pausePrompt = keyName(boundKey(BindAction::Pause)) + " RESUME  /  "
                + keyName(boundKey(BindAction::Restart))
                + (gameMode == GameMode::Training ? " RESET SERVE  /  " : " RESTART  /  ")
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
        } else if (state == MatchState::GameOver) {
            DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 7, 14, 195});
            drawCentered(winner == 0 ? "BLUE WINS" : "ORANGE WINS", 222, 62, winner == 0 ? SKYBLUE : ORANGE);
            drawCentered(TextFormat("FINAL  %d - %d", score[0], score[1]), 310, 30, RAYWHITE);
            drawCentered("PRESS ENTER TO PLAY AGAIN", 382, 24, GOLD);
            drawCentered(keyName(boundKey(BindAction::MainMenu)) + " MAIN MENU", 427, 17, Color{176, 199, 219, 255});
        } else if (state == MatchState::Playing && serveInProgress && gameMode == GameMode::Match) {
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
        BeginDrawing();
        ClearBackground(Color{8, 14, 27, 255});
        drawBackdrop();
        drawWorld();
        drawOverlay();
        EndDrawing();
    }

    int run() {
        float smokeElapsed = 0.0F;
        bool menuCaptured = false;
        bool customizeCaptured = false;
        bool controlsCaptured = false;
        bool aboutCaptured = false;
        bool bindingTestPassed = false;
        bool aboutLinksVerified = false;
        bool trainingStarted = false;
        bool trainingLoadingCaptured = false;
        bool trainingCountdownCaptured = false;
        bool trainingPlayCaptured = false;
        bool trainingGroundPassed = false;
        bool trainingResetPassed = false;
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
                    TakeScreenshot("rocket_volley_menu_smoke.png");
                    menuCaptured = true;
                }
                if (menuCaptured && !customizeCaptured && smokeElapsed >= 0.8F) {
                    menuPage = MenuPage::Customize;
                }
                if (!customizeCaptured && smokeElapsed >= 1.2F) {
                    TakeScreenshot("rocket_volley_customize_smoke.png");
                    customizeCaptured = true;
                }
                if (customizeCaptured && !controlsCaptured && smokeElapsed >= 1.35F) {
                    menuPage = MenuPage::Controls;
                }
                if (!controlsCaptured && smokeElapsed >= 1.7F) {
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
                if (controlsCaptured && !aboutCaptured && smokeElapsed >= 1.85F) {
                    menuPage = MenuPage::About;
                }
                if (!aboutCaptured && smokeElapsed >= 2.2F) {
                    openAboutLink(0);
                    openAboutLink(1);
                    openAboutLink(2);
                    aboutLinksVerified = true;
                    TakeScreenshot("rocket_volley_about_smoke.png");
                    aboutCaptured = true;
                }
                if (!trainingStarted && smokeElapsed >= 2.55F) {
                    menuPage = MenuPage::Main;
                    automatedPlayer = true;
                    arenaSelection = 0;
                    activeArenaIndex = 0;
                    chooseArenaForSession();
                    arenaTestPassed = ArenaThemes.size() >= 4 && activeArenaIndex != 0;
                    arenaSelection = 2;
                    activeArenaIndex = 1;
                    difficulty = Difficulty::Rookie;
                    applyDifficultyPhysics();
                    beginLoadingTraining();
                    trainingStarted = true;
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
                    resetTrainingServe();
                    trainingResetPassed = state == MatchState::ServeCountdown
                        && gameMode == GameMode::Training
                        && rallyTouches == 0;
                    arenaSelection = 4;
                    activeArenaIndex = 3;
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
                    teammateLanePassed = strikerForTeam(0, serveLandingTarget) == 0;
                    aiControls(cars[1], deltaSeconds);
                    teammateLanePassed = teammateLanePassed
                        && length2D(subtract(cars[1].aiTarget, physics.transform(cars[0].body).position)) >= 4.0F;
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
                if (!carCameraCaptured && state == MatchState::Playing && rallyTouches >= 1) {
                    cameraMode = CameraMode::Car;
                    TakeScreenshot("rocket_volley_gameplay_car_smoke.png");
                    carCameraCaptured = true;
                }
                if (carCameraCaptured && !ballCameraActivated && state == MatchState::Playing) {
                    cameraMode = CameraMode::Ball;
                    cameraModeNotice = 1.5F;
                    ballCameraActivated = true;
                    ballCameraElapsed = 0.0F;
                }
                if (ballCameraActivated && !ballCameraCaptured) {
                    ballCameraElapsed += deltaSeconds;
                    if (state == MatchState::Playing && ballCameraElapsed >= 1.0F) {
                        TakeScreenshot("rocket_volley_gameplay_ball_smoke.png");
                        ballCameraCaptured = true;
                    }
                }
                if (!aerialTestStarted && ballCameraCaptured && bestRallyTouches >= 5) {
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
                    resetCar(cars[2], {-10.0F, 0.62F, -18.0F}, 0.0F);
                    resetCar(cars[3], {10.0F, 0.62F, -18.0F}, 0.0F);
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
                    resetCar(cars[2], {-10.0F, 0.62F, -19.0F}, 0.0F);
                    resetCar(cars[3], {10.0F, 0.62F, -19.0F}, 0.0F);
                }
                if (sprintTestComplete || smokeElapsed >= 32.0F) {
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
            if (!menuCaptured || !customizeCaptured || !controlsCaptured || !aboutCaptured
                || !bindingTestPassed || !aboutLinksVerified || !loadingCaptured || !countdownCaptured
                || !trainingLoadingCaptured || !trainingCountdownCaptured || !trainingPlayCaptured
                || !trainingGroundPassed || !trainingResetPassed || !rookieSpeedPassed || !proSpeedPassed
                || !arenaTestPassed || !teammateLanePassed || !trueServeTossPassed || !trueServeContactPassed
                || BallRadius < 1.07F
                || !carCameraCaptured || !ballCameraCaptured || !aerialCaptured || !aerialTestComplete
                || bestRallyTouches < 5 || aerialPeakHeight < 6.0F || aerialPeakForwardY < 0.3F
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
