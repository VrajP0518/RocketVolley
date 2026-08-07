#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rv::net {

inline constexpr std::uint32_t ProtocolMagic = 0x52564F4CU; // RVOL
inline constexpr std::uint16_t ProtocolVersion = 1;

enum class PacketType : std::uint8_t {
    PlayerInput = 1,
    WorldSnapshot = 2,
};

enum InputFlags : std::uint8_t {
    JumpPressed = 1U << 0U,
    DodgePressed = 1U << 1U,
    BoostHeld = 1U << 2U,
};

struct PlayerInputPacket {
    std::uint32_t sequence = 0;
    std::uint32_t clientTick = 0;
    std::uint8_t playerSlot = 0;
    float throttle = 0.0F;
    float steer = 0.0F;
    std::uint8_t flags = 0;
};

struct BodySnapshot {
    std::array<float, 3> position{};
    std::array<float, 3> velocity{};
    float heading = 0.0F;
};

struct WorldSnapshotPacket {
    std::uint32_t sequence = 0;
    std::uint32_t serverTick = 0;
    std::uint8_t arenaIndex = 0;
    std::array<std::uint8_t, 2> score{};
    std::uint8_t state = 0;
    float matchTime = 0.0F;
    std::int8_t possessionTeam = -1;
    std::array<std::uint8_t, 2> teamTouches{};
    BodySnapshot ball{};
    std::array<BodySnapshot, 4> cars{};
};

[[nodiscard]] std::vector<std::uint8_t> encodePlayerInput(const PlayerInputPacket &packet);
[[nodiscard]] std::optional<PlayerInputPacket> decodePlayerInput(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> encodeWorldSnapshot(const WorldSnapshotPacket &packet);
[[nodiscard]] std::optional<WorldSnapshotPacket> decodeWorldSnapshot(std::span<const std::uint8_t> bytes);
[[nodiscard]] bool protocolSelfTest();

} // namespace rv::net
