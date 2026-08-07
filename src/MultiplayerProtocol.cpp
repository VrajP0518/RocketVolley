#include "rocket_volley/MultiplayerProtocol.hpp"

#include <bit>
#include <cmath>
#include <cstddef>

namespace rv::net {
namespace {

void appendU8(std::vector<std::uint8_t> &bytes, std::uint8_t value) {
    bytes.push_back(value);
}

void appendU16(std::vector<std::uint8_t> &bytes, std::uint16_t value) {
    appendU8(bytes, static_cast<std::uint8_t>(value & 0xFFU));
    appendU8(bytes, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        appendU8(bytes, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void appendFloat(std::vector<std::uint8_t> &bytes, float value) {
    appendU32(bytes, std::bit_cast<std::uint32_t>(value));
}

bool readU8(std::span<const std::uint8_t> bytes, std::size_t &offset, std::uint8_t &value) {
    if (offset >= bytes.size()) return false;
    value = bytes[offset++];
    return true;
}

bool readU16(std::span<const std::uint8_t> bytes, std::size_t &offset, std::uint16_t &value) {
    std::uint8_t low = 0;
    std::uint8_t high = 0;
    if (!readU8(bytes, offset, low) || !readU8(bytes, offset, high)) return false;
    value = static_cast<std::uint16_t>(low) | (static_cast<std::uint16_t>(high) << 8U);
    return true;
}

bool readU32(std::span<const std::uint8_t> bytes, std::size_t &offset, std::uint32_t &value) {
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        std::uint8_t part = 0;
        if (!readU8(bytes, offset, part)) return false;
        value |= static_cast<std::uint32_t>(part) << shift;
    }
    return true;
}

bool readFloat(std::span<const std::uint8_t> bytes, std::size_t &offset, float &value) {
    std::uint32_t bits = 0;
    if (!readU32(bytes, offset, bits)) return false;
    value = std::bit_cast<float>(bits);
    return std::isfinite(value);
}

void appendHeader(std::vector<std::uint8_t> &bytes, PacketType type) {
    appendU32(bytes, ProtocolMagic);
    appendU16(bytes, ProtocolVersion);
    appendU8(bytes, static_cast<std::uint8_t>(type));
}

bool readHeader(std::span<const std::uint8_t> bytes, std::size_t &offset, PacketType expected) {
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint8_t type = 0;
    return readU32(bytes, offset, magic)
        && readU16(bytes, offset, version)
        && readU8(bytes, offset, type)
        && magic == ProtocolMagic
        && version == ProtocolVersion
        && type == static_cast<std::uint8_t>(expected);
}

void appendBody(std::vector<std::uint8_t> &bytes, const BodySnapshot &body) {
    for (float value : body.position) appendFloat(bytes, value);
    for (float value : body.velocity) appendFloat(bytes, value);
    appendFloat(bytes, body.heading);
}

bool readBody(std::span<const std::uint8_t> bytes, std::size_t &offset, BodySnapshot &body) {
    for (float &value : body.position) if (!readFloat(bytes, offset, value)) return false;
    for (float &value : body.velocity) if (!readFloat(bytes, offset, value)) return false;
    return readFloat(bytes, offset, body.heading);
}

} // namespace

std::vector<std::uint8_t> encodePlayerInput(const PlayerInputPacket &packet) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(25);
    appendHeader(bytes, PacketType::PlayerInput);
    appendU32(bytes, packet.sequence);
    appendU32(bytes, packet.clientTick);
    appendU8(bytes, packet.playerSlot);
    appendFloat(bytes, packet.throttle);
    appendFloat(bytes, packet.steer);
    appendU8(bytes, packet.flags);
    return bytes;
}

std::optional<PlayerInputPacket> decodePlayerInput(std::span<const std::uint8_t> bytes) {
    std::size_t offset = 0;
    PlayerInputPacket packet;
    if (!readHeader(bytes, offset, PacketType::PlayerInput)
        || !readU32(bytes, offset, packet.sequence)
        || !readU32(bytes, offset, packet.clientTick)
        || !readU8(bytes, offset, packet.playerSlot)
        || !readFloat(bytes, offset, packet.throttle)
        || !readFloat(bytes, offset, packet.steer)
        || !readU8(bytes, offset, packet.flags)
        || offset != bytes.size()
        || packet.playerSlot >= 4
        || std::abs(packet.throttle) > 1.0F
        || std::abs(packet.steer) > 1.0F) {
        return std::nullopt;
    }
    return packet;
}

std::vector<std::uint8_t> encodeWorldSnapshot(const WorldSnapshotPacket &packet) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(162);
    appendHeader(bytes, PacketType::WorldSnapshot);
    appendU32(bytes, packet.sequence);
    appendU32(bytes, packet.serverTick);
    appendU8(bytes, packet.arenaIndex);
    appendU8(bytes, packet.score[0]);
    appendU8(bytes, packet.score[1]);
    appendU8(bytes, packet.state);
    appendFloat(bytes, packet.matchTime);
    appendU8(bytes, static_cast<std::uint8_t>(packet.possessionTeam));
    appendU8(bytes, packet.teamTouches[0]);
    appendU8(bytes, packet.teamTouches[1]);
    appendBody(bytes, packet.ball);
    for (const BodySnapshot &car : packet.cars) appendBody(bytes, car);
    return bytes;
}

std::optional<WorldSnapshotPacket> decodeWorldSnapshot(std::span<const std::uint8_t> bytes) {
    std::size_t offset = 0;
    WorldSnapshotPacket packet;
    std::uint8_t possession = 0;
    if (!readHeader(bytes, offset, PacketType::WorldSnapshot)
        || !readU32(bytes, offset, packet.sequence)
        || !readU32(bytes, offset, packet.serverTick)
        || !readU8(bytes, offset, packet.arenaIndex)
        || !readU8(bytes, offset, packet.score[0])
        || !readU8(bytes, offset, packet.score[1])
        || !readU8(bytes, offset, packet.state)
        || !readFloat(bytes, offset, packet.matchTime)
        || !readU8(bytes, offset, possession)
        || !readU8(bytes, offset, packet.teamTouches[0])
        || !readU8(bytes, offset, packet.teamTouches[1])
        || !readBody(bytes, offset, packet.ball)) {
        return std::nullopt;
    }
    packet.possessionTeam = static_cast<std::int8_t>(possession);
    for (BodySnapshot &car : packet.cars) if (!readBody(bytes, offset, car)) return std::nullopt;
    if (offset != bytes.size() || packet.arenaIndex >= 4 || packet.possessionTeam < -1
        || packet.possessionTeam > 1 || packet.teamTouches[0] > 3 || packet.teamTouches[1] > 3) {
        return std::nullopt;
    }
    return packet;
}

bool protocolSelfTest() {
    PlayerInputPacket input;
    input.sequence = 42;
    input.clientTick = 9001;
    input.playerSlot = 1;
    input.throttle = 0.75F;
    input.steer = -0.25F;
    input.flags = JumpPressed | BoostHeld;
    const std::vector<std::uint8_t> encodedInput = encodePlayerInput(input);
    const std::optional<PlayerInputPacket> decodedInput = decodePlayerInput(encodedInput);
    if (!decodedInput.has_value()
        || decodedInput->sequence != input.sequence
        || decodedInput->clientTick != input.clientTick
        || decodedInput->playerSlot != input.playerSlot
        || decodedInput->throttle != input.throttle
        || decodedInput->steer != input.steer
        || decodedInput->flags != input.flags) {
        return false;
    }

    std::vector<std::uint8_t> corruptInput = encodedInput;
    corruptInput[0] ^= 0xFFU;
    if (decodePlayerInput(corruptInput).has_value()) return false;

    WorldSnapshotPacket snapshot;
    snapshot.sequence = 17;
    snapshot.serverTick = 1200;
    snapshot.arenaIndex = 3;
    snapshot.score = {4, 2};
    snapshot.state = 1;
    snapshot.matchTime = 91.5F;
    snapshot.possessionTeam = 0;
    snapshot.teamTouches = {2, 1};
    snapshot.ball.position = {1.0F, 2.0F, 3.0F};
    snapshot.ball.velocity = {-1.0F, 0.5F, 8.0F};
    snapshot.ball.heading = 0.4F;
    for (std::size_t index = 0; index < snapshot.cars.size(); ++index) {
        snapshot.cars[index].position = {
            static_cast<float>(index), 0.6F, static_cast<float>(index) * -2.0F};
        snapshot.cars[index].velocity = {0.1F, 0.0F, 2.0F + static_cast<float>(index)};
        snapshot.cars[index].heading = static_cast<float>(index) * 0.25F;
    }
    const std::vector<std::uint8_t> encodedSnapshot = encodeWorldSnapshot(snapshot);
    const std::optional<WorldSnapshotPacket> decodedSnapshot = decodeWorldSnapshot(encodedSnapshot);
    if (!decodedSnapshot.has_value()
        || decodedSnapshot->sequence != snapshot.sequence
        || decodedSnapshot->serverTick != snapshot.serverTick
        || decodedSnapshot->arenaIndex != snapshot.arenaIndex
        || decodedSnapshot->score != snapshot.score
        || decodedSnapshot->state != snapshot.state
        || decodedSnapshot->matchTime != snapshot.matchTime
        || decodedSnapshot->possessionTeam != snapshot.possessionTeam
        || decodedSnapshot->teamTouches != snapshot.teamTouches
        || decodedSnapshot->ball.position != snapshot.ball.position
        || decodedSnapshot->ball.velocity != snapshot.ball.velocity
        || decodedSnapshot->ball.heading != snapshot.ball.heading) {
        return false;
    }
    for (std::size_t index = 0; index < snapshot.cars.size(); ++index) {
        if (decodedSnapshot->cars[index].position != snapshot.cars[index].position
            || decodedSnapshot->cars[index].velocity != snapshot.cars[index].velocity
            || decodedSnapshot->cars[index].heading != snapshot.cars[index].heading) {
            return false;
        }
    }
    return true;
}

} // namespace rv::net
