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
    ServeCountdown,
    Playing,
    Paused,
    PointWon,
    GameOver,
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
    std::array<int, 2> score{0, 0};
    float matchTime = 180.0F;
    float rallyTime = 0.0F;
    float pointTimer = 0.0F;
    float serveCountdown = 3.0F;
    float accumulator = 0.0F;
    float totalTime = 0.0F;
    float shake = 0.0F;
    float hitSoundCooldown = 0.0F;
    float boostSoundCooldown = 0.0F;
    float ballElasticity = 0.82F;
    Vec3 servePosition{};
    Vec3 serveVelocity{};
    Vec3 previousBallVelocity{};
    int mainMenuIndex = 0;
    int customizeMenuIndex = 0;
    int bodyColorIndex = 0;
    int wheelColorIndex = 0;
    int spoilerColorIndex = 0;
    int countdownCue = 3;
    int scoringTeam = 0;
    int winner = 0;
    bool pendingGameOver = false;
    bool overtime = false;
    bool showHelp = false;
    bool shouldExit = false;

    Impl() {
        SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
        InitWindow(ScreenWidth, ScreenHeight, "Rocket Volley");
        if (!IsWindowReady()) {
            throw std::runtime_error("raylib could not create a window");
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
        car.aiTarget = position;
        car.dodgeAvailable = true;
        physics.setTransform(car.body, position, yawRotation(heading));
        physics.setLinearVelocity(car.body, {});
        physics.setAngularVelocity(car.body, {});
    }

    void resetRound(int receivingTeam) {
        resetCar(cars[0], {-3.7F, 0.62F, 10.5F}, Pi);
        resetCar(cars[1], {4.1F, 0.62F, 7.4F}, Pi);
        resetCar(cars[2], {-4.1F, 0.62F, -7.4F}, 0.0F);
        resetCar(cars[3], {3.7F, 0.62F, -10.5F}, 0.0F);

        const float direction = receivingTeam == 0 ? 1.0F : -1.0F;
        servePosition = {
            static_cast<float>(GetRandomValue(-16, 16)) * 0.1F,
            9.0F,
            -direction * 1.5F};
        serveVelocity = {
            static_cast<float>(GetRandomValue(-8, 8)) * 0.1F,
            2.0F,
            direction * 8.0F};
        physics.setTransform(ball, servePosition, {});
        physics.setLinearVelocity(ball, {});
        physics.setAngularVelocity(ball, {});
        previousBallVelocity = {};
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
        particles.clear();
        resetRound(GetRandomValue(0, 1));
        state = MatchState::ServeCountdown;
    }

    Controls playerControls() const {
        Controls controls;
        controls.throttle = static_cast<float>(IsKeyDown(KEY_W)) - static_cast<float>(IsKeyDown(KEY_S));
        controls.steer = static_cast<float>(IsKeyDown(KEY_D)) - static_cast<float>(IsKeyDown(KEY_A));
        controls.jumpPressed = IsKeyPressed(KEY_SPACE);
        controls.boostHeld = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        if (IsGamepadAvailable(0)) {
            const float stickX = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
            const float stickY = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
            if (std::abs(stickX) > 0.16F) {
                controls.steer = stickX;
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

        if (grounded) {
            car.airborneTime = 0.0F;
            car.dodgeAvailable = true;
        } else {
            car.airborneTime += deltaSeconds;
        }

        const float planarSpeed = length2D(velocity);
        const float turnFactor = clamp(0.36F + planarSpeed / 14.0F, 0.36F, 1.0F);
        car.heading = wrapAngle(car.heading + controls.steer * 2.35F * turnFactor * deltaSeconds);
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

    float ballLandingTime() const {
        const Transform ballTransform = physics.transform(ball);
        const Vec3 velocity = physics.linearVelocity(ball);
        const float c = ballTransform.position.y - BallRadius;
        const float discriminant = velocity.y * velocity.y + 36.0F * c;
        if (discriminant <= 0.0F) {
            return 0.5F;
        }
        const float root = (velocity.y + std::sqrt(discriminant)) / 18.0F;
        return clamp(root, 0.18F, 2.4F);
    }

    Controls aiControls(Car &car, float deltaSeconds) {
        const Transform carTransform = physics.transform(car.body);
        const Transform ballTransform = physics.transform(ball);
        car.aiThinkTimer -= deltaSeconds;

        if (car.aiThinkTimer <= 0.0F) {
            const bool pro = difficulty == Difficulty::Pro;
            car.aiThinkTimer = pro ? 0.07F : 0.24F;
            const float teamDirection = car.team == 0 ? 1.0F : -1.0F;
            const float time = pro ? ballLandingTime() * 0.78F : 0.36F;
            Vec3 target = pro ? predictBall(time) : ballTransform.position;
            const bool ballOnOwnSide = ballTransform.position.z * teamDirection > 0.0F;

            if (!ballOnOwnSide) {
                const float homeX = (car.slot % 2 == 0) ? -3.8F : 3.8F;
                target = {homeX, 0.0F, teamDirection * (car.slot % 2 == 0 ? 8.7F : 6.2F)};
            } else if (car.slot == 1 || car.slot == 3) {
                target.x += pro ? 0.35F : std::sin(totalTime * 1.7F + static_cast<float>(car.slot)) * 2.2F;
                target.z += teamDirection * 0.65F;
            }

            target.x = clamp(target.x, -10.2F, 10.2F);
            if (car.team == 0) {
                target.z = clamp(target.z, 1.3F, 16.2F);
            } else {
                target.z = clamp(target.z, -16.2F, -1.3F);
            }
            car.aiTarget = target;
        }

        const Vec3 toTarget = subtract(car.aiTarget, carTransform.position);
        const float desiredHeading = std::atan2(toTarget.x, toTarget.z);
        const float difference = wrapAngle(desiredHeading - car.heading);
        const float distance = length2D(toTarget);
        Controls controls;
        controls.steer = clamp(difference * 1.65F, -1.0F, 1.0F);
        controls.throttle = std::abs(difference) < 2.25F ? 1.0F : -0.35F;
        controls.boostHeld = difficulty == Difficulty::Pro && distance > 7.0F && std::abs(difference) < 0.42F;

        const float distanceToBall = length(subtract(ballTransform.position, carTransform.position));
        const bool ballApproachable = car.team == 0 ? ballTransform.position.z > -0.7F : ballTransform.position.z < 0.7F;
        const float jumpRange = difficulty == Difficulty::Pro ? 3.5F : 2.7F;
        controls.jumpPressed = ballApproachable
            && distanceToBall < jumpRange
            && ballTransform.position.y > 1.0F
            && ballTransform.position.y < (difficulty == Difficulty::Pro ? 4.7F : 3.2F)
            && car.jumpCooldown <= 0.0F;
        return controls;
    }

    void checkBallImpact(Vec3 ballVelocity) {
        hitSoundCooldown = std::max(0.0F, hitSoundCooldown - FixedStep);
        const float velocityChange = length(subtract(ballVelocity, previousBallVelocity));
        if (velocityChange > 4.1F && hitSoundCooldown <= 0.0F) {
            const Vec3 ballPosition = physics.transform(ball).position;
            audio.play(audio.hit);
            emitBurst(ballPosition, Color{255, 224, 92, 255}, 12, clamp(velocityChange * 0.22F, 2.0F, 5.0F), 0.11F);
            shake = std::max(shake, clamp(velocityChange * 0.025F, 0.12F, 0.5F));
            hitSoundCooldown = 0.11F;
        }
        previousBallVelocity = ballVelocity;
    }

    void scorePoint(int team) {
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
            Controls ai = aiControls(cars[index], FixedStep);
            ai.jumpPressed = false;
            ai.boostHeld = false;
            driveCar(cars[index], ai, FixedStep);
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
        if (state == MatchState::Title) {
            const float angle = totalTime * 0.22F;
            desiredPosition = {std::sin(angle) * 23.0F, 10.0F, std::cos(angle) * 23.0F};
            desiredTarget = {0.0F, 1.6F, 0.0F};
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
                startMatch();
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
            startMatch();
        }
    }

    void update(float deltaSeconds) {
        totalTime += deltaSeconds;
        boostSoundCooldown = std::max(0.0F, boostSoundCooldown - deltaSeconds);
        handleGlobalInput();
        audio.updateMusic(state == MatchState::Title);

        if (state == MatchState::Playing || state == MatchState::ServeCountdown) {
            Controls controls = playerControls();
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
        DrawPlane({0.0F, 0.005F, 0.0F}, {24.0F, 36.0F}, Color{31, 42, 58, 255});
        DrawCube({0.0F, -0.08F, 9.0F}, 23.6F, 0.08F, 17.6F, Color{25, 77, 105, 255});
        DrawCube({0.0F, -0.075F, -9.0F}, 23.6F, 0.08F, 17.6F, Color{102, 37, 51, 255});

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

        DrawCube({-12.28F, 2.5F, 0.0F}, 0.35F, 5.0F, 36.0F, Color{18, 24, 34, 210});
        DrawCube({12.28F, 2.5F, 0.0F}, 0.35F, 5.0F, 36.0F, Color{18, 24, 34, 210});
        DrawCube({0.0F, 2.5F, -18.28F}, 24.0F, 5.0F, 0.35F, Color{18, 24, 34, 210});
        DrawCube({0.0F, 2.5F, 18.28F}, 24.0F, 5.0F, 0.35F, Color{18, 24, 34, 210});

        for (int side = -1; side <= 1; side += 2) {
            for (int index = -8; index <= 8; index += 2) {
                const Color lamp = (index / 2 + side) % 2 == 0 ? SKYBLUE : PINK;
                DrawCube({static_cast<float>(index), 7.2F, static_cast<float>(side) * 18.35F}, 0.7F, 0.35F, 0.25F, lamp);
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

        DrawText("BLUE", 24, 18, 22, SKYBLUE);
        DrawText("ORANGE", ScreenWidth - 117, 18, 22, ORANGE);
        DrawText(difficulty == Difficulty::Pro ? "AI: PRO [2]" : "AI: ROOKIE [1]", 24, 50, 16, Color{180, 196, 215, 255});
        DrawText(TextFormat("BALL BOUNCE %d%%  [ / ]", static_cast<int>(ballElasticity * 100.0F)), ScreenWidth - 232, 50, 16, Color{180, 196, 215, 255});

        constexpr int meterWidth = 220;
        const int meterX = 26;
        const int meterY = ScreenHeight - 48;
        DrawRectangle(meterX, meterY, meterWidth, 18, Color{12, 18, 28, 220});
        DrawRectangle(meterX + 3, meterY + 3, static_cast<int>((meterWidth - 6) * cars[0].boost / 100.0F), 12, GOLD);
        DrawText("BOOST", meterX, meterY - 21, 16, RAYWHITE);
        DrawText("F1 CONTROLS", ScreenWidth - 129, ScreenHeight - 31, 16, Color{180, 196, 215, 255});
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
        drawCentered("2v2 RETRO CAR VOLLEYBALL", 211, 21, RAYWHITE);

        const int x = ScreenWidth / 2 - 235;
        constexpr int width = 470;
        drawMenuRow("START MATCH", 0, mainMenuIndex, x, 282, width);
        drawMenuRow("CUSTOMIZE CAR", 1, mainMenuIndex, x, 344, width);
        drawMenuRow(
            difficulty == Difficulty::Pro ? "AI DIFFICULTY: PRO" : "AI DIFFICULTY: ROOKIE",
            2,
            mainMenuIndex,
            x,
            406,
            width);
        drawMenuRow("QUIT", 3, mainMenuIndex, x, 468, width);

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
        drawCentered("ESC       Pause", 390, 23, RAYWHITE);
        drawCentered("1 / 2     Rookie / Pro AI", 432, 23, RAYWHITE);
        drawCentered("[ / ]     Adjust ball bounce", 474, 23, RAYWHITE);
        drawCentered("Gamepad: left stick, A jump, B or RT boost", 516, 20, Color{176, 199, 219, 255});
        drawCentered("F1 TO CLOSE", 558, 18, GOLD);
    }

    void drawOverlay() const {
        if (state != MatchState::Title) {
            drawHud();
        }

        if (state == MatchState::Title) {
            if (menuPage == MenuPage::Main) {
                drawMainMenu();
            } else {
                drawCustomizeMenu();
            }
        } else if (state == MatchState::ServeCountdown) {
            DrawRectangle(ScreenWidth / 2 - 118, 205, 236, 238, Color{7, 12, 22, 225});
            DrawRectangle(ScreenWidth / 2 - 118, 205, 8, 238, GOLD);
            drawCentered("GET READY", 229, 24, RAYWHITE);
            drawCentered(TextFormat("%d", std::max(1, static_cast<int>(std::ceil(serveCountdown)))), 273, 112, GOLD);
            drawCentered("MOVE INTO POSITION", 407, 17, Color{188, 207, 223, 255});
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
        drawWorld();
        drawOverlay();
        EndDrawing();
    }

    int run(bool smokeTest) {
        float smokeElapsed = 0.0F;
        bool menuCaptured = false;
        bool customizeCaptured = false;
        bool matchStarted = false;
        bool countdownCaptured = false;
        bool gameplayCaptured = false;
        while (!WindowShouldClose() && !shouldExit) {
            const float deltaSeconds = std::min(GetFrameTime(), 0.1F);
            update(deltaSeconds);
            draw();
            if (smokeTest) {
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
                    startMatch();
                    matchStarted = true;
                }
                if (!countdownCaptured && smokeElapsed >= 2.0F) {
                    TakeScreenshot("rocket_volley_countdown_smoke.png");
                    countdownCaptured = true;
                }
                if (!gameplayCaptured && smokeElapsed >= 5.3F) {
                    TakeScreenshot("rocket_volley_gameplay_smoke.png");
                    gameplayCaptured = true;
                }
                if (smokeElapsed >= 7.0F) {
                    break;
                }
            }
        }
        return 0;
    }
};

Game::Game() : impl_(std::make_unique<Impl>()) {}

Game::~Game() = default;

int Game::run(bool smokeTest) {
    return impl_->run(smokeTest);
}

} // namespace rv
