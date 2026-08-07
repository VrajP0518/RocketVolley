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
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rv {
namespace {

constexpr int ScreenWidth = 1280;
constexpr int ScreenHeight = 720;
constexpr float ArenaHalfWidth = 12.0F;
constexpr float ArenaHalfLength = 18.0F;
constexpr float BallRadius = 0.72F;
constexpr float FixedStep = 1.0F / 120.0F;
constexpr float Pi = std::numbers::pi_v<float>;
constexpr const char *ArenaName = "NEON METEOR DOME";

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
};

enum class Difficulty {
    Rookie,
    Pro,
};

struct Controls {
    float throttle = 0.0F;
    float steer = 0.0F;
    bool jumpPressed = false;
    bool boostHeld = false;
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
    bool dodgeAvailable = true;
    Color paint = WHITE;
    Color wheelColor{18, 20, 25, 255};
    Color spoilerColor = GOLD;
};

struct ColorChoice {
    const char *name;
    Color color;
};

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
    {-7.2F, 0.04F, -12.8F}, {7.2F, 0.04F, -12.8F}, {-7.2F, 0.04F, 12.8F}, {7.2F, 0.04F, 12.8F},
    {-6.0F, 0.04F, -5.8F}, {6.0F, 0.04F, -5.8F}, {-6.0F, 0.04F, 5.8F}, {6.0F, 0.04F, 5.8F}}};

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
    int bodyColorIndex = 0;
    int wheelColorIndex = 0;
    int spoilerColorIndex = 0;
    int countdownCue = 3;
    int scoringTeam = 0;
    int winner = 0;
    int receivingTeam = 0;
    int receivingCar = 0;
    std::array<int, 2> rotationStriker{0, 2};
    int rallyTouches = 0;
    int bestRallyTouches = 0;
    bool pendingGameOver = false;
    bool overtime = false;
    bool showHelp = false;
    bool shouldExit = false;
    bool automatedPlayer = false;
    bool smokeTestMode = false;

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

    void createArena() {
        physics.createStaticBox({0.0F, -0.5F, 0.0F}, {ArenaHalfWidth, 0.5F, ArenaHalfLength});
        physics.createStaticBox({-12.45F, 2.6F, 0.0F}, {0.45F, 2.6F, 18.45F});
        physics.createStaticBox({12.45F, 2.6F, 0.0F}, {0.45F, 2.6F, 18.45F});
        physics.createStaticBox({0.0F, 2.6F, -18.45F}, {12.45F, 2.6F, 0.45F});
        physics.createStaticBox({0.0F, 2.6F, 18.45F}, {12.45F, 2.6F, 0.45F});
        physics.createStaticBox({0.0F, 1.18F, 0.0F}, {11.75F, 1.18F, 0.16F});
    }

    void createActors() {
        ball = physics.createDynamicSphere({0.0F, 5.0F, 0.0F}, BallRadius, 4.2F, 0.82F);

        constexpr std::array<Color, 4> colors{
            Color{43, 199, 255, 255}, Color{80, 112, 255, 255},
            Color{255, 76, 87, 255}, Color{255, 154, 54, 255}};
        for (int index = 0; index < static_cast<int>(cars.size()); ++index) {
            Car &car = cars[index];
            car.body = physics.createDynamicBox({0.0F, 0.58F, 0.0F}, {0.92F, 0.45F, 1.42F}, 140.0F, 0.22F);
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
        car.dodgeAvailable = true;
        physics.setTransform(car.body, position, yawRotation(heading));
        physics.setLinearVelocity(car.body, {});
        physics.setAngularVelocity(car.body, {});
    }

    void resetRound(int receivingTeam) {
        this->receivingTeam = receivingTeam;
        receivingCar = receivingTeam == 0 ? 0 : 2;
        rotationStriker = {0, 2};
        const float direction = receivingTeam == 0 ? 1.0F : -1.0F;
        const float targetX = static_cast<float>(GetRandomValue(-18, 18)) * 0.1F;
        serveLandingTarget = {targetX, BallRadius + 0.08F, direction * 8.6F};
        const float supportX = targetX >= 0.0F ? -5.2F : 5.2F;

        if (receivingTeam == 0) {
            resetCar(cars[0], {targetX, 0.62F, 8.6F}, Pi);
            resetCar(cars[1], {supportX, 0.62F, 13.0F}, Pi);
            resetCar(cars[2], {-4.4F, 0.62F, -7.0F}, 0.0F);
            resetCar(cars[3], {4.4F, 0.62F, -12.5F}, 0.0F);
        } else {
            resetCar(cars[0], {-4.4F, 0.62F, 7.0F}, Pi);
            resetCar(cars[1], {4.4F, 0.62F, 12.5F}, Pi);
            resetCar(cars[2], {targetX, 0.62F, -8.6F}, 0.0F);
            resetCar(cars[3], {supportX, 0.62F, -13.0F}, 0.0F);
        }

        servePosition = {
            -targetX * 0.25F,
            9.0F,
            -direction * 1.5F};
        constexpr float serveFlightTime = 1.45F;
        serveVelocity = {
            (serveLandingTarget.x - servePosition.x) / serveFlightTime,
            (serveLandingTarget.y - servePosition.y + 9.0F * serveFlightTime * serveFlightTime) / serveFlightTime,
            (serveLandingTarget.z - servePosition.z) / serveFlightTime};
        physics.setTransform(ball, servePosition, {});
        physics.setLinearVelocity(ball, {});
        physics.setAngularVelocity(ball, {});
        previousBallVelocity = {};
        aiBallTouchCooldown = 0.0F;
        rallyTouchCooldown = 0.0F;
        bestRallyTouches = std::max(bestRallyTouches, rallyTouches);
        rallyTouches = 0;
        rallyTime = 0.0F;
        serveCountdown = 3.0F;
        countdownCue = 4;
        accumulator = 0.0F;
    }

    void launchServe() {
        physics.setTransform(ball, servePosition, {});
        physics.setLinearVelocity(ball, serveVelocity);
        physics.setAngularVelocity(ball, {0.0F, 3.6F, 1.6F});
        previousBallVelocity = serveVelocity;
        rallyTime = 0.0F;
        accumulator = 0.0F;
        state = MatchState::Playing;
    }

    void startMatch() {
        score = {0, 0};
        matchTime = 180.0F;
        overtime = false;
        pendingGameOver = false;
        scoringTeam = 0;
        winner = 0;
        rallyTouches = 0;
        bestRallyTouches = 0;
        particles.clear();
        resetRound(0);
        state = MatchState::ServeCountdown;
    }

    void beginLoadingMatch() {
        loadingTimer = 0.0F;
        accumulator = 0.0F;
        showHelp = false;
        state = MatchState::Loading;
    }

    Controls playerControls() const {
        Controls controls;
        controls.throttle = static_cast<float>(IsKeyDown(KEY_W)) - static_cast<float>(IsKeyDown(KEY_S));
        controls.steer = static_cast<float>(IsKeyDown(KEY_A)) - static_cast<float>(IsKeyDown(KEY_D));
        controls.jumpPressed = IsKeyPressed(KEY_SPACE);
        controls.boostHeld = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

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
            car.dodgeAvailable = true;
        } else {
            car.airborneTime += deltaSeconds;
        }

        const float planarSpeed = length2D(velocity);
        const float turnFactor = clamp(0.36F + planarSpeed / 14.0F, 0.36F, 1.0F);
        const float turnRate = (!car.human || automatedPlayer) ? 3.05F : 2.35F;
        car.heading = wrapAngle(car.heading + controls.steer * turnRate * turnFactor * deltaSeconds);
        const Vec3 forward = forwardFromHeading(car.heading);

        const bool boosting = controls.boostHeld && controls.throttle > -0.1F && car.boost > 0.0F;
        const float targetSpeed = boosting ? 21.5F : 13.5F;
        const float desiredX = forward.x * controls.throttle * targetSpeed;
        const float desiredZ = forward.z * controls.throttle * targetSpeed;
        const float traction = grounded ? 6.5F : 0.9F;
        velocity.x += (desiredX - velocity.x) * clamp(traction * deltaSeconds, 0.0F, 1.0F);
        velocity.z += (desiredZ - velocity.z) * clamp(traction * deltaSeconds, 0.0F, 1.0F);

        if (std::abs(controls.throttle) < 0.05F && grounded) {
            const float coast = std::max(0.0F, 1.0F - 2.2F * deltaSeconds);
            velocity.x *= coast;
            velocity.z *= coast;
        }

        if (boosting) {
            velocity.x += forward.x * 14.0F * deltaSeconds;
            velocity.z += forward.z * 14.0F * deltaSeconds;
            car.boost = std::max(0.0F, car.boost - 28.0F * deltaSeconds);
            if (car.human && boostSoundCooldown <= 0.0F) {
                audio.play(audio.boost);
                boostSoundCooldown = 0.24F;
            }
            if (GetRandomValue(0, 7) == 0) {
                emitBurst(
                    {transform.position.x - forward.x * 1.5F, transform.position.y, transform.position.z - forward.z * 1.5F},
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

        const float speed = length2D(velocity);
        if (speed > 23.0F) {
            const float scale = 23.0F / speed;
            velocity.x *= scale;
            velocity.z *= scale;
        }
        physics.setLinearVelocity(car.body, velocity);

        if (controls.jumpPressed && car.jumpCooldown <= 0.0F) {
            if (grounded) {
                physics.addImpulse(car.body, {0.0F, 980.0F, 0.0F});
                car.jumpCooldown = 0.16F;
                emitBurst({transform.position.x, 0.1F, transform.position.z}, LIGHTGRAY, 9, 2.2F, 0.13F);
                if (car.human) {
                    audio.play(audio.jump);
                }
            } else if (car.dodgeAvailable && car.airborneTime < 0.75F) {
                physics.addImpulse(car.body, {forward.x * 480.0F, 190.0F, forward.z * 480.0F});
                physics.setAngularVelocity(car.body, {forward.z * 8.5F, 0.0F, -forward.x * 8.5F});
                car.dodgeAvailable = false;
                car.jumpCooldown = 0.25F;
                if (car.human) {
                    audio.play(audio.jump);
                }
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
        Vec3 predicted{
            ballTransform.position.x + velocity.x * time,
            ballTransform.position.y + velocity.y * time - 9.0F * time * time,
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
        const float discriminant = velocity.y * velocity.y + 36.0F * c;
        if (discriminant <= 0.0F) {
            return 0.18F;
        }
        const float root = (velocity.y + std::sqrt(discriminant)) / 18.0F;
        return clamp(root, 0.08F, 2.4F);
    }

    float ballLandingTime() const {
        return std::max(0.18F, ballTimeToHeight(BallRadius));
    }

    int strikerForTeam(int team, Vec3 landingTarget) const {
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
        return secondDistance + 1.4F < firstDistance ? second : first;
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

            if (ballThreatensTeam && striker) {
                target = contactTarget;
                if (target.z * teamDirection < 0.8F) {
                    target = landingTarget;
                }
                target.z += teamDirection * (pro ? 0.2F : 0.1F);
            } else {
                const bool backSlot = car.slot == 1 || car.slot == 3;
                const float homeX = backSlot ? 2.6F : -2.6F;
                target = {homeX, 0.0F, teamDirection * (backSlot ? 10.2F : 7.0F)};
                if (ballThreatensTeam && !striker) {
                    target.x = clamp(landingTarget.x * -0.55F, -6.5F, 6.5F);
                    target.z = teamDirection * 10.8F;
                }
            }

            target.x = clamp(target.x, -10.2F, 10.2F);
            target.z = car.team == 0
                ? clamp(target.z, 1.25F, 16.1F)
                : clamp(target.z, -16.1F, -1.25F);
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
            && ballThreatensTeam
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
        const float horizontalReach = difficulty == Difficulty::Pro ? 2.72F : 2.45F;
        const float verticalReach = difficulty == Difficulty::Pro ? 3.35F : 2.95F;
        if (length2D(offset) > horizontalReach || offset.y < -0.45F || offset.y > verticalReach) {
            return false;
        }

        const float teamDirection = car.team == 0 ? 1.0F : -1.0F;
        if (ballTransform.position.z * teamDirection < -0.8F) {
            return false;
        }

        const float flightTime = difficulty == Difficulty::Pro ? 1.5F : 1.3F;
        const Vec3 target{
            clamp(-ballTransform.position.x * 0.35F
                    + std::sin(totalTime * 1.9F + static_cast<float>(car.slot)) * 2.0F,
                -8.2F,
                8.2F),
            BallRadius + 0.08F,
            -teamDirection * (difficulty == Difficulty::Pro ? 9.2F : 7.8F)};
        Vec3 returnVelocity{
            (target.x - ballTransform.position.x) / flightTime,
            (target.y - ballTransform.position.y + 9.0F * flightTime * flightTime) / flightTime,
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

    void fixedUpdate(Controls controls) {
        aiBallTouchCooldown = std::max(0.0F, aiBallTouchCooldown - FixedStep);
        driveCar(cars[0], controls, FixedStep);
        for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
            driveCar(cars[index], aiControls(cars[index], FixedStep), FixedStep);
        }

        physics.step(FixedStep);
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
        for (int index = 1; index < static_cast<int>(cars.size()); ++index) {
            driveCar(cars[index], {}, FixedStep);
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
        if (state == MatchState::Title || state == MatchState::Loading) {
            const float angle = totalTime * 0.22F;
            desiredPosition = {std::sin(angle) * 23.0F, 10.0F, std::cos(angle) * 23.0F};
            desiredTarget = {0.0F, 1.6F, 0.0F};
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
        } else {
            const Transform player = physics.transform(cars[0].body);
            const Vec3 forward = forwardFromHeading(cars[0].heading);
            desiredPosition = {
                player.position.x - forward.x * 8.8F,
                player.position.y + 5.2F,
                player.position.z - forward.z * 8.8F};
            desiredTarget = {
                player.position.x + forward.x * 3.2F,
                player.position.y + 1.25F,
                player.position.z + forward.z * 3.2F};
        }

        const float response = 1.0F - std::exp(-6.5F * deltaSeconds);
        camera.position = Vector3Lerp(camera.position, desiredPosition, response);
        camera.target = Vector3Lerp(camera.target, desiredTarget, response);
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
    }

    void handleMenuInput() {
        const bool gamepadAvailable = IsGamepadAvailable(0);
        const bool up = IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)
            || (gamepadAvailable && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP));
        const bool down = IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN)
            || (gamepadAvailable && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN));
        const bool left = IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)
            || (gamepadAvailable && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT));
        const bool right = IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)
            || (gamepadAvailable && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT));

        int &selection = menuPage == MenuPage::Main ? mainMenuIndex : customizeMenuIndex;
        const int itemCount = 4;
        if (up || down) {
            selection = (selection + (down ? 1 : -1) + itemCount) % itemCount;
            audio.play(audio.menuMove);
        }

        if (menuPage == MenuPage::Customize && (left || right)) {
            cycleCustomization(right ? 1 : -1);
        }

        if (IsKeyPressed(KEY_ESCAPE) && menuPage == MenuPage::Customize) {
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
                menuPage = MenuPage::Customize;
                customizeMenuIndex = 0;
            } else if (mainMenuIndex == 2) {
                difficulty = difficulty == Difficulty::Pro ? Difficulty::Rookie : Difficulty::Pro;
            } else {
                shouldExit = true;
            }
        } else if (customizeMenuIndex == 3) {
            audio.play(audio.menuMove);
            menuPage = MenuPage::Main;
        } else {
            cycleCustomization(1);
        }
    }

    void handleGlobalInput() {
        if (IsKeyPressed(KEY_F1)) {
            showHelp = !showHelp;
        }
        if (IsKeyPressed(KEY_ONE)) {
            difficulty = Difficulty::Rookie;
        }
        if (IsKeyPressed(KEY_TWO)) {
            difficulty = Difficulty::Pro;
        }
        if (IsKeyPressed(KEY_LEFT_BRACKET)) {
            ballElasticity = std::max(0.55F, ballElasticity - 0.05F);
            physics.setRestitution(ball, ballElasticity);
        }
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
            ballElasticity = std::min(0.95F, ballElasticity + 0.05F);
            physics.setRestitution(ball, ballElasticity);
        }
        const bool gamepadCameraToggle = IsGamepadAvailable(0)
            && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP);
        if ((IsKeyPressed(KEY_C) || gamepadCameraToggle)
            && state != MatchState::Title
            && state != MatchState::Loading) {
            cameraMode = cameraMode == CameraMode::Car ? CameraMode::Ball : CameraMode::Car;
            cameraModeNotice = 1.5F;
            audio.play(audio.menuMove);
        }
        if (IsKeyPressed(KEY_R) && state != MatchState::Title) {
            startMatch();
        }
        if (IsKeyPressed(KEY_M) && state != MatchState::Title) {
            state = MatchState::Title;
            menuPage = MenuPage::Main;
            showHelp = false;
            accumulator = 0.0F;
        }

        if (state == MatchState::Title) {
            handleMenuInput();
        } else if (IsKeyPressed(KEY_ESCAPE)) {
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
        handleGlobalInput();
        audio.updateMusic(state == MatchState::Title || state == MatchState::Loading);

        if (state == MatchState::Loading) {
            loadingTimer += deltaSeconds;
            if (loadingTimer >= 1.6F) {
                startMatch();
            }
        } else if (state == MatchState::Playing || state == MatchState::ServeCountdown) {
            Controls controls = automatedPlayer && state == MatchState::Playing
                ? aiControls(cars[0], deltaSeconds)
                : (automatedPlayer ? Controls{} : playerControls());
            accumulator = std::min(accumulator + deltaSeconds, 0.2F);
            bool firstStep = true;
            const MatchState activeState = state;
            while (accumulator >= FixedStep && state == activeState) {
                Controls stepControls = controls;
                if (!firstStep) {
                    stepControls.jumpPressed = false;
                }
                if (activeState == MatchState::Playing) {
                    fixedUpdate(stepControls);
                } else {
                    countdownFixedUpdate(stepControls);
                }
                firstStep = false;
                accumulator -= FixedStep;
            }
        } else if (state == MatchState::PointWon) {
            pointTimer -= deltaSeconds;
            if (pointTimer <= 0.0F) {
                if (pendingGameOver) {
                    state = MatchState::GameOver;
                } else {
                    resetRound(1 - scoringTeam);
                    state = MatchState::ServeCountdown;
                }
            }
        }

        updateParticles(deltaSeconds);
        updateCamera(deltaSeconds);
    }

    void drawArena() const {
        DrawPlane({0.0F, -0.34F, 0.0F}, {70.0F, 78.0F}, Color{10, 16, 29, 255});

        for (int tier = 0; tier < 4; ++tier) {
            const float x = 13.4F + static_cast<float>(tier) * 1.15F;
            const float y = 0.4F + static_cast<float>(tier) * 0.85F;
            const float height = 0.8F + static_cast<float>(tier) * 0.25F;
            const Color standColor = tier % 2 == 0 ? Color{24, 39, 61, 255} : Color{31, 48, 73, 255};
            DrawCube({-x, y, 0.0F}, 1.1F, height, 37.0F, standColor);
            DrawCube({x, y, 0.0F}, 1.1F, height, 37.0F, standColor);
        }

        for (int side = -1; side <= 1; side += 2) {
            for (int z = -16; z <= 16; z += 2) {
                for (int row = 0; row < 3; ++row) {
                    const bool blueFan = ((z / 2) + row + side) % 3 != 0;
                    const Color crowd = blueFan ? Color{59, 187, 255, 255} : Color{255, 91, 118, 255};
                    DrawCube(
                        {static_cast<float>(side) * (13.05F + static_cast<float>(row) * 1.1F),
                            2.65F + static_cast<float>(row) * 0.82F,
                            static_cast<float>(z)},
                        0.28F,
                        0.28F,
                        0.72F,
                        crowd);
                }
            }
        }

        DrawPlane({0.0F, 0.005F, 0.0F}, {24.0F, 36.0F}, Color{31, 42, 58, 255});
        DrawCube({0.0F, -0.08F, 9.0F}, 23.6F, 0.08F, 17.6F, Color{22, 88, 119, 255});
        DrawCube({0.0F, -0.075F, -9.0F}, 23.6F, 0.08F, 17.6F, Color{118, 40, 55, 255});

        DrawCylinder({0.0F, 0.018F, 0.0F}, 5.0F, 5.0F, 0.018F, 40, Color{89, 161, 190, 38});
        DrawCylinderWires({0.0F, 0.035F, 0.0F}, 5.0F, 5.0F, 0.02F, 40, Color{191, 228, 239, 165});

        for (const Vec3 padPosition : BoostPadPositions) {
            const Vector3 pad = toRay(padPosition);
            const bool blueSide = pad.z > 0.0F;
            const Color glow = blueSide ? Color{54, 211, 255, 190} : Color{255, 148, 45, 190};
            DrawCylinder(pad, 0.62F, 0.62F, 0.035F, 12, withAlpha(glow, 70));
            DrawCylinderWires(pad, 0.62F, 0.62F, 0.04F, 12, glow);
        }

        const Color lineColor{205, 230, 235, 200};
        DrawCube({-11.75F, 0.025F, 0.0F}, 0.08F, 0.05F, 35.4F, lineColor);
        DrawCube({11.75F, 0.025F, 0.0F}, 0.08F, 0.05F, 35.4F, lineColor);
        DrawCube({0.0F, 0.025F, -17.75F}, 23.5F, 0.05F, 0.08F, lineColor);
        DrawCube({0.0F, 0.025F, 17.75F}, 23.5F, 0.05F, 0.08F, lineColor);
        DrawCube({0.0F, 0.03F, 0.0F}, 23.5F, 0.06F, 0.1F, lineColor);

        for (int x = -10; x <= 10; x += 2) {
            DrawLine3D({static_cast<float>(x), 0.08F, -17.7F}, {static_cast<float>(x), 0.08F, 17.7F}, Color{91, 132, 144, 45});
        }
        for (int z = -16; z <= 16; z += 2) {
            DrawLine3D({-11.7F, 0.08F, static_cast<float>(z)}, {11.7F, 0.08F, static_cast<float>(z)}, Color{91, 132, 144, 45});
        }

        DrawCube({-11.55F, 1.35F, 0.0F}, 0.28F, 2.7F, 0.28F, GOLD);
        DrawCube({11.55F, 1.35F, 0.0F}, 0.28F, 2.7F, 0.28F, GOLD);
        DrawCube({0.0F, 2.36F, 0.0F}, 23.1F, 0.12F, 0.18F, GOLD);
        for (int x = -11; x <= 11; ++x) {
            DrawLine3D({static_cast<float>(x), 0.1F, 0.0F}, {static_cast<float>(x), 2.32F, 0.0F}, Color{222, 235, 225, 160});
        }
        for (int row = 1; row <= 7; ++row) {
            const float y = 0.1F + static_cast<float>(row) * 0.28F;
            DrawLine3D({-11.5F, y, 0.0F}, {11.5F, y, 0.0F}, Color{222, 235, 225, 160});
        }

        const Color barrier{61, 112, 146, 65};
        DrawCube({-12.28F, 2.5F, 0.0F}, 0.35F, 5.0F, 36.0F, barrier);
        DrawCube({12.28F, 2.5F, 0.0F}, 0.35F, 5.0F, 36.0F, barrier);
        DrawCube({0.0F, 2.5F, -18.28F}, 24.0F, 5.0F, 0.35F, barrier);
        DrawCube({0.0F, 2.5F, 18.28F}, 24.0F, 5.0F, 0.35F, barrier);

        const Color cageLine{96, 178, 211, 115};
        for (int z = -18; z <= 18; z += 3) {
            DrawLine3D({-12.08F, 0.1F, static_cast<float>(z)}, {-12.08F, 5.1F, static_cast<float>(z)}, cageLine);
            DrawLine3D({12.08F, 0.1F, static_cast<float>(z)}, {12.08F, 5.1F, static_cast<float>(z)}, cageLine);
        }
        for (int y = 1; y <= 5; ++y) {
            DrawLine3D({-12.08F, static_cast<float>(y), -18.0F}, {-12.08F, static_cast<float>(y), 18.0F}, cageLine);
            DrawLine3D({12.08F, static_cast<float>(y), -18.0F}, {12.08F, static_cast<float>(y), 18.0F}, cageLine);
        }

        DrawCube({-12.15F, 5.25F, 0.0F}, 0.25F, 0.18F, 36.0F, SKYBLUE);
        DrawCube({12.15F, 5.25F, 0.0F}, 0.25F, 0.18F, 36.0F, ORANGE);

        if (state == MatchState::ServeCountdown) {
            const Color landingColor = receivingTeam == 0 ? SKYBLUE : ORANGE;
            DrawCylinder(
                {serveLandingTarget.x, 0.045F, serveLandingTarget.z},
                1.45F,
                1.45F,
                0.025F,
                24,
                withAlpha(landingColor, 58));
            DrawCylinderWires(
                {serveLandingTarget.x, 0.055F, serveLandingTarget.z},
                1.45F,
                1.45F,
                0.04F,
                24,
                landingColor);
        }

        for (int side = -1; side <= 1; side += 2) {
            for (int index = -8; index <= 8; index += 2) {
                const Color lamp = (index / 2 + side) % 2 == 0 ? SKYBLUE : PINK;
                DrawCube({static_cast<float>(index), 7.2F, static_cast<float>(side) * 18.35F}, 0.7F, 0.35F, 0.25F, lamp);
            }
        }

        for (int cornerX : {-1, 1}) {
            for (int cornerZ : {-1, 1}) {
                const Color meteorColor = cornerZ > 0 ? SKYBLUE : ORANGE;
                DrawCube(
                    {static_cast<float>(cornerX) * 13.0F, 5.6F, static_cast<float>(cornerZ) * 18.9F},
                    0.3F,
                    11.2F,
                    0.3F,
                    meteorColor);
                const Vector3 beacon{
                    static_cast<float>(cornerX) * 13.0F,
                    11.55F + 0.18F * std::sin(totalTime * 2.4F + static_cast<float>(cornerX + cornerZ)),
                    static_cast<float>(cornerZ) * 18.9F};
                DrawSphere(beacon, 0.62F, withAlpha(meteorColor, 220));
                DrawSphereWires(beacon, 0.86F, 8, 12, GOLD);
                DrawLine3D(
                    beacon,
                    {beacon.x - static_cast<float>(cornerX) * 2.8F, beacon.y + 2.1F, beacon.z - static_cast<float>(cornerZ) * 2.8F},
                    withAlpha(GOLD, 150));
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
        DrawRectangleGradientV(
            0,
            0,
            ScreenWidth,
            ScreenHeight,
            Color{7, 16, 39, 255},
            Color{47, 24, 67, 255});
        DrawCircleGradient({1040.0F, 122.0F}, 260.0F, Color{83, 64, 155, 85}, Color{20, 26, 55, 0});
        DrawCircleGradient({210.0F, 178.0F}, 210.0F, Color{25, 137, 188, 70}, Color{15, 24, 50, 0});
        DrawCircleGradient({1050.0F, 122.0F}, 74.0F, Color{255, 154, 70, 255}, Color{92, 45, 126, 255});
        DrawEllipseLines(1050, 122, 118.0F, 28.0F, Color{255, 220, 128, 180});
        DrawEllipseLines(1050, 122, 103.0F, 23.0F, Color{255, 117, 107, 110});
        for (int streak = 0; streak < 5; ++streak) {
            const float drift = std::fmod(totalTime * (32.0F + streak * 5.0F) + streak * 210.0F, 1450.0F);
            const int x = static_cast<int>(1450.0F - drift);
            const int y = 64 + streak * 51;
            DrawLineEx({static_cast<float>(x), static_cast<float>(y)}, {static_cast<float>(x - 48), static_cast<float>(y + 20)}, 2.0F, withAlpha(GOLD, 105));
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
            DrawRectangle(x, ScreenHeight - height - 42, width, height, Color{8, 13, 28, 210});
            if (index % 2 == 0) {
                DrawRectangle(x + 9, ScreenHeight - height - 22, 5, 5, index % 4 == 0 ? SKYBLUE : ORANGE);
            }
        }
    }

    void drawWorld() const {
        BeginMode3D(camera);
        drawArena();
        for (const Car &car : cars) {
            drawCar(car);
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
        DrawText(TextFormat("%d", score[0]), ScreenWidth / 2 - 92, 17, 42, SKYBLUE);
        DrawText("-", ScreenWidth / 2 - 7, 20, 36, LIGHTGRAY);
        DrawText(TextFormat("%d", score[1]), ScreenWidth / 2 + 66, 17, 42, ORANGE);

        std::string timerText;
        if (overtime) {
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

        DrawText("BLUE", 24, 18, 22, SKYBLUE);
        DrawText("ORANGE", ScreenWidth - 117, 18, 22, ORANGE);
        DrawText(difficulty == Difficulty::Pro ? "AI: PRO [2]" : "AI: ROOKIE [1]", 24, 50, 16, Color{180, 196, 215, 255});
        DrawText(TextFormat("BALL BOUNCE %d%%  [ / ]", static_cast<int>(ballElasticity * 100.0F)), ScreenWidth - 232, 50, 16, Color{180, 196, 215, 255});
        DrawText(ArenaName, 24, 91, 15, withAlpha(GOLD, 205));

        constexpr int meterWidth = 220;
        const int meterX = 26;
        const int meterY = ScreenHeight - 48;
        DrawRectangle(meterX, meterY, meterWidth, 18, Color{12, 18, 28, 220});
        DrawRectangle(meterX + 3, meterY + 3, static_cast<int>((meterWidth - 6) * cars[0].boost / 100.0F), 12, GOLD);
        DrawText("BOOST", meterX, meterY - 21, 16, RAYWHITE);
        if (state == MatchState::Playing) {
            const Vec3 landingTarget = predictBall(ballLandingTime());
            const bool partnerChasing = landingTarget.z > 0.0F && strikerForTeam(0, landingTarget) == 1;
            DrawText(
                partnerChasing ? "TEAMMATE: CHASING" : "TEAMMATE: COVERING",
                ScreenWidth - 232,
                ScreenHeight - 58,
                16,
                partnerChasing ? SKYBLUE : Color{180, 196, 215, 255});
        }
        DrawText(
            cameraMode == CameraMode::Ball ? "CAM: BALL  [C / Y]" : "CAM: CAR  [C / Y]",
            ScreenWidth - 189,
            ScreenHeight - 82,
            16,
            cameraMode == CameraMode::Ball ? GOLD : Color{180, 196, 215, 255});
        DrawText("F1 CONTROLS", ScreenWidth - 129, ScreenHeight - 31, 16, Color{180, 196, 215, 255});

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
        drawCentered("ROCKET", 70, 64, SKYBLUE);
        drawCentered("VOLLEY", 130, 64, ORANGE);
        drawCentered(ArenaName, 203, 20, GOLD);
        drawCentered("2v2 RETRO CAR VOLLEYBALL", 230, 18, RAYWHITE);

        const int x = ScreenWidth / 2 - 235;
        constexpr int width = 470;
        drawMenuRow("START MATCH", 0, mainMenuIndex, x, 276, width);
        drawMenuRow("CUSTOMIZE CAR", 1, mainMenuIndex, x, 338, width);
        drawMenuRow(
            difficulty == Difficulty::Pro ? "AI DIFFICULTY: PRO" : "AI DIFFICULTY: ROOKIE",
            2,
            mainMenuIndex,
            x,
            400,
            width);
        drawMenuRow("QUIT", 3, mainMenuIndex, x, 462, width);

        drawCentered("W / S MOVE     ENTER SELECT", 558, 19, Color{204, 218, 230, 255});
        drawCentered("FIRST TO 7 WINS", 599, 16, Color{150, 174, 196, 255});
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

    void drawHelp() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{5, 8, 15, 215});
        DrawRectangle(ScreenWidth / 2 - 330, 120, 660, 470, Color{18, 27, 43, 248});
        DrawRectangle(ScreenWidth / 2 - 330, 120, 8, 470, GOLD);
        drawCentered("CONTROLS", 154, 34, GOLD);
        drawCentered("W / S     Accelerate / brake", 222, 23, RAYWHITE);
        drawCentered("A / D     Steer", 264, 23, RAYWHITE);
        drawCentered("SPACE     Jump, then dodge in mid-air", 306, 23, RAYWHITE);
        drawCentered("SHIFT     Boost", 348, 23, RAYWHITE);
        drawCentered("C / Y     Toggle Car Cam / Ball Cam", 390, 23, RAYWHITE);
        drawCentered("ESC       Pause", 432, 23, RAYWHITE);
        drawCentered("1 / 2     Rookie / Pro AI", 474, 23, RAYWHITE);
        drawCentered("[ / ] bounce   Gamepad: stick, A jump, B/RT boost", 516, 19, Color{176, 199, 219, 255});
        drawCentered("F1 TO CLOSE", 558, 18, GOLD);
    }

    void drawLoadingScreen() const {
        DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 8, 18, 242});
        DrawCircleGradient({ScreenWidth / 2.0F, 255.0F}, 190.0F, Color{48, 139, 190, 75}, Color{4, 8, 18, 0});
        drawCentered(ArenaName, 174, 24, GOLD);
        drawCentered("PREPARING ARENA", 218, 48, RAYWHITE);
        drawCentered("Synchronizing cars, cameras, and meteor shields", 288, 18, Color{176, 199, 219, 255});
        constexpr int barWidth = 580;
        const int barX = (ScreenWidth - barWidth) / 2;
        const float progress = clamp(loadingTimer / 1.6F, 0.0F, 1.0F);
        DrawRectangle(barX, 350, barWidth, 24, Color{17, 27, 44, 255});
        DrawRectangle(barX + 4, 354, static_cast<int>((barWidth - 8) * progress), 16, GOLD);
        DrawRectangleLines(barX, 350, barWidth, 24, Color{95, 126, 154, 255});
        drawCentered(TextFormat("%d%%", static_cast<int>(progress * 100.0F)), 391, 19, RAYWHITE);
        drawCentered("TIP: C OR GAMEPAD Y SWITCHES BETWEEN CAR CAM AND BALL CAM", 494, 17, SKYBLUE);
        drawCentered("EVERY SERVE STARTS WITH A 3 - 2 - 1 COUNTDOWN", 527, 16, Color{176, 199, 219, 255});
    }

    void drawOverlay() const {
        if (state != MatchState::Title && state != MatchState::Loading) {
            drawHud();
        }

        if (state == MatchState::Title) {
            if (menuPage == MenuPage::Main) {
                drawMainMenu();
            } else {
                drawCustomizeMenu();
            }
        } else if (state == MatchState::Loading) {
            drawLoadingScreen();
        } else if (state == MatchState::ServeCountdown) {
            DrawRectangle(ScreenWidth / 2 - 118, 205, 236, 238, Color{7, 12, 22, 225});
            DrawRectangle(ScreenWidth / 2 - 118, 205, 8, 238, GOLD);
            drawCentered("GET READY", 229, 24, RAYWHITE);
            drawCentered(TextFormat("%d", std::max(1, static_cast<int>(std::ceil(serveCountdown)))), 273, 112, GOLD);
            drawCentered(receivingCar == 0 ? "YOU ARE THE RECEIVER" : "AI RECEIVER IS SET", 402, 17, Color{188, 207, 223, 255});
        } else if (state == MatchState::Paused) {
            DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 7, 14, 190});
            drawCentered("PAUSED", 270, 55, GOLD);
            drawCentered("ESC RESUME  /  R RESTART  /  M MAIN MENU", 345, 22, RAYWHITE);
        } else if (state == MatchState::PointWon) {
            DrawRectangle(0, 210, ScreenWidth, 175, Color{7, 10, 18, 220});
            drawCentered(scoringTeam == 0 ? "BLUE SCORES!" : "ORANGE SCORES!", 242, 48, scoringTeam == 0 ? SKYBLUE : ORANGE);
            drawCentered(TextFormat("%d  -  %d", score[0], score[1]), 309, 30, RAYWHITE);
        } else if (state == MatchState::GameOver) {
            DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 7, 14, 195});
            drawCentered(winner == 0 ? "BLUE WINS" : "ORANGE WINS", 222, 62, winner == 0 ? SKYBLUE : ORANGE);
            drawCentered(TextFormat("FINAL  %d - %d", score[0], score[1]), 310, 30, RAYWHITE);
            drawCentered("PRESS ENTER TO PLAY AGAIN", 382, 24, GOLD);
            drawCentered("M MAIN MENU", 427, 17, Color{176, 199, 219, 255});
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
        bool matchStarted = false;
        bool loadingCaptured = false;
        bool countdownCaptured = false;
        bool carCameraCaptured = false;
        bool ballCameraActivated = false;
        bool ballCameraCaptured = false;
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
                if (!matchStarted && smokeElapsed >= 1.5F) {
                    menuPage = MenuPage::Main;
                    automatedPlayer = true;
                    beginLoadingMatch();
                    matchStarted = true;
                }
                if (!loadingCaptured && state == MatchState::Loading && smokeElapsed >= 1.9F) {
                    TakeScreenshot("rocket_volley_loading_smoke.png");
                    loadingCaptured = true;
                }
                if (!countdownCaptured && state == MatchState::ServeCountdown && serveCountdown <= 2.55F) {
                    TakeScreenshot("rocket_volley_countdown_smoke.png");
                    countdownCaptured = true;
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
                if ((ballCameraCaptured && bestRallyTouches >= 5) || smokeElapsed >= 22.0F) {
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
            if (!menuCaptured || !customizeCaptured || !loadingCaptured || !countdownCaptured
                || !carCameraCaptured || !ballCameraCaptured || bestRallyTouches < 5) {
                TraceLog(
                    LOG_ERROR,
                    "SMOKE: gate failed captures=%d%d%d%d%d%d rally=%d (needed all captures and 5 touches)",
                    menuCaptured,
                    customizeCaptured,
                    loadingCaptured,
                    countdownCaptured,
                    carCameraCaptured,
                    ballCameraCaptured,
                    bestRallyTouches);
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
