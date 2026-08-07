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
    Sound music{};

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
        music = synthesize(rate, 8.0F, [](float t) {
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
        SetSoundVolume(music, 0.22F);
    }

    void updateMusic() const {
        if (ready && !IsSoundPlaying(music)) {
            PlaySound(music);
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
        UnloadSound(music);
        CloseAudioDevice();
        ready = false;
    }
};

enum class MatchState {
    Title,
    Playing,
    Paused,
    PointWon,
    GameOver,
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
};

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
    BodyHandle ball = InvalidBody;
    std::array<Car, 4> cars{};
    std::vector<Particle> particles;
    Camera3D camera{};
    MatchState state = MatchState::Title;
    Difficulty difficulty = Difficulty::Pro;
    std::array<int, 2> score{0, 0};
    float matchTime = 180.0F;
    float rallyTime = 0.0F;
    float pointTimer = 0.0F;
    float accumulator = 0.0F;
    float totalTime = 0.0F;
    float shake = 0.0F;
    float hitSoundCooldown = 0.0F;
    float boostSoundCooldown = 0.0F;
    float ballElasticity = 0.82F;
    Vec3 previousBallVelocity{};
    int scoringTeam = 0;
    int winner = 0;
    bool pendingGameOver = false;
    bool overtime = false;
    bool showHelp = false;

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
        }
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
        physics.setTransform(ball, {0.0F, 5.4F, direction * 0.8F}, {});
        physics.setLinearVelocity(ball, {static_cast<float>(GetRandomValue(-15, 15)) * 0.1F, 4.0F, direction * 8.4F});
        physics.setAngularVelocity(ball, {0.0F, 4.5F, 2.0F});
        previousBallVelocity = physics.linearVelocity(ball);
        rallyTime = 0.0F;
        accumulator = 0.0F;
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
        state = MatchState::Playing;
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
        if (IsKeyPressed(KEY_R)) {
            startMatch();
        }

        if (state == MatchState::Title) {
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)
                || (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT))) {
                startMatch();
            }
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            if (state == MatchState::Playing) {
                state = MatchState::Paused;
                accumulator = 0.0F;
            } else if (state == MatchState::Paused) {
                state = MatchState::Playing;
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
        audio.updateMusic();

        if (state == MatchState::Playing) {
            Controls controls = playerControls();
            accumulator = std::min(accumulator + deltaSeconds, 0.2F);
            bool firstStep = true;
            while (accumulator >= FixedStep && state == MatchState::Playing) {
                Controls stepControls = controls;
                if (!firstStep) {
                    stepControls.jumpPressed = false;
                }
                fixedUpdate(stepControls);
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
                    state = MatchState::Playing;
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

    void drawCar(const Car &car) const {
        const Transform transform = physics.transform(car.body);
        const Quaternion rotation = toRay(transform.rotation);
        Vector3 axis{};
        float angle = 0.0F;
        QuaternionToAxisAngle(rotation, &axis, &angle);
        if (Vector3Length(axis) < 0.001F) {
            axis = {0.0F, 1.0F, 0.0F};
        }
        const Vector3 position = toRay(transform.position);

        DrawCylinder({position.x, 0.035F, position.z}, 1.12F, 1.12F, 0.025F, 16, Color{0, 0, 0, 90});
        DrawModelEx(cubeModel, position, axis, angle * RAD2DEG, {1.84F, 0.9F, 2.84F}, car.paint);

        const Vector3 cabinOffset = Vector3RotateByQuaternion({0.0F, 0.58F, -0.12F}, rotation);
        const Vector3 cabinPosition = Vector3Add(position, cabinOffset);
        DrawModelEx(cubeModel, cabinPosition, axis, angle * RAD2DEG, {1.35F, 0.58F, 1.25F}, Color{30, 42, 60, 255});

        constexpr std::array<Vector3, 4> wheelOffsets{
            Vector3{-0.98F, -0.35F, -0.86F}, Vector3{0.98F, -0.35F, -0.86F},
            Vector3{-0.98F, -0.35F, 0.86F}, Vector3{0.98F, -0.35F, 0.86F}};
        for (Vector3 offset : wheelOffsets) {
            const Vector3 wheelPosition = Vector3Add(position, Vector3RotateByQuaternion(offset, rotation));
            DrawSphere(wheelPosition, 0.34F, Color{18, 20, 25, 255});
        }

        const Vector3 noseOffset = Vector3RotateByQuaternion({0.0F, 0.03F, 1.46F}, rotation);
        const Vector3 nosePosition = Vector3Add(position, noseOffset);
        DrawModelEx(cubeModel, nosePosition, axis, angle * RAD2DEG, {1.45F, 0.22F, 0.12F}, car.team == 0 ? WHITE : GOLD);
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
            DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 8, 17, 155});
            drawCentered("ROCKET", 154, 74, SKYBLUE);
            drawCentered("VOLLEY", 226, 74, ORANGE);
            drawCentered("2v2 RETRO CAR VOLLEYBALL", 329, 25, RAYWHITE);
            const Color pulse = withAlpha(GOLD, static_cast<unsigned char>(180 + 75 * (0.5F + 0.5F * std::sin(totalTime * 4.0F))));
            drawCentered("PRESS ENTER TO KICK OFF", 413, 27, pulse);
            drawCentered("1 ROOKIE AI     2 PRO AI", 466, 19, Color{184, 201, 218, 255});
            drawCentered("FIRST TO 7 WINS", 514, 18, Color{184, 201, 218, 255});
        } else if (state == MatchState::Paused) {
            DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 7, 14, 190});
            drawCentered("PAUSED", 270, 55, GOLD);
            drawCentered("ESC TO RESUME  /  R TO RESTART", 345, 22, RAYWHITE);
        } else if (state == MatchState::PointWon) {
            DrawRectangle(0, 210, ScreenWidth, 175, Color{7, 10, 18, 220});
            drawCentered(scoringTeam == 0 ? "BLUE SCORES!" : "ORANGE SCORES!", 242, 48, scoringTeam == 0 ? SKYBLUE : ORANGE);
            drawCentered(TextFormat("%d  -  %d", score[0], score[1]), 309, 30, RAYWHITE);
        } else if (state == MatchState::GameOver) {
            DrawRectangle(0, 0, ScreenWidth, ScreenHeight, Color{4, 7, 14, 195});
            drawCentered(winner == 0 ? "BLUE WINS" : "ORANGE WINS", 222, 62, winner == 0 ? SKYBLUE : ORANGE);
            drawCentered(TextFormat("FINAL  %d - %d", score[0], score[1]), 310, 30, RAYWHITE);
            drawCentered("PRESS ENTER TO PLAY AGAIN", 382, 24, GOLD);
            drawCentered("R RESTARTS AT ANY TIME", 427, 17, Color{176, 199, 219, 255});
        }

        if (showHelp) {
            drawHelp();
        }
    }

    void draw() const {
        BeginDrawing();
        ClearBackground(Color{8, 14, 27, 255});
        drawWorld();
        drawOverlay();
        EndDrawing();
    }

    int run(bool smokeTest) {
        if (smokeTest) {
            startMatch();
        }
        float smokeElapsed = 0.0F;
        bool screenshotCaptured = false;
        while (!WindowShouldClose()) {
            const float deltaSeconds = std::min(GetFrameTime(), 0.1F);
            update(deltaSeconds);
            draw();
            if (smokeTest) {
                smokeElapsed += deltaSeconds;
                if (!screenshotCaptured && smokeElapsed >= 0.6F) {
                    TakeScreenshot("rocket_volley_smoke.png");
                    screenshotCaptured = true;
                }
                if (smokeElapsed >= 6.0F) {
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
