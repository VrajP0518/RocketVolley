# Rocket Volley release and online roadmap

## Product decision

The primary release should be a portable Windows download: one versioned ZIP containing `RocketVolley.exe` and the five-line player README. Players extract it and run the executable; there is no installer, account, launcher, or admin requirement. Build a Release configuration and run `cmake --build build-release --target package_windows`; CPack emits `RocketVolley-0.8-windows-x64.zip` plus a SHA-256 checksum. Publish that exact asset on a GitHub Release first, with itch.io as an optional second storefront. GitHub supports stable `releases/latest` download links and keeps versioned assets attached to each release.

The `.github/workflows/release.yml` workflow now performs that release automatically on a version tag such as `v0.8`, or from **Actions > Windows Release > Run workflow**. It rejects tags that do not match the CMake project version, builds the x64 Release executable, runs the hidden smoke test, creates and verifies the ZIP checksum, keeps a 14-day workflow artifact, and publishes or refreshes the permanent GitHub Release assets. For manual publishing, enter `v0.8` for the current source version; future versions should update `project(... VERSION ...)` first.

## Can this become online multiplayer?

Yes, but networking should not be added by sending the current Jolt bodies directly between peers. Keep Jolt authoritative on one host, send timestamped player inputs to that host, and broadcast compact car/ball snapshots. Run gameplay physics at the existing 120 Hz, accept inputs at 60 Hz, send snapshots around 20-30 Hz, interpolate remote cars, and use prediction plus reconciliation for the local car. The ball and score always follow the host. This keeps a 2v2 match playable while avoiding four competing physics truths.

Version 0.8 establishes the first multiplayer boundary: local co-op routes Player 2 through a versioned `PlayerInputPacket`, while the authoritative game can emit validated `WorldSnapshotPacket` state for the ball, four cars, score, arena, clock, and team-touch possession. Both packet types use explicit little-endian serialization, protocol magic/version checks, finite-value validation, and corrupt-packet rejection. The next transport milestone is LAN/direct-IP host/client delivery with snapshot interpolation using Valve's open-source GameNetworkingSockets, which supplies reliable and unreliable encrypted messages but deliberately does not serialize game state. For a Steam release, the same protocol can move to Steam Networking Sockets plus Steam Lobbies/relays so player IPs stay hidden. Host migration, reconnects, version compatibility, input prediction/reconciliation, replay capture, and anti-cheat boundaries follow the first host-authoritative transport prototype.

## Web build

A browser version is technically feasible because raylib and Jolt both support WebAssembly, but it should be a later accessibility build rather than the primary release. The game loop, file access, audio startup, packaging, and networking transport all need Emscripten-specific work; browser clients also cannot use the native UDP transport directly and would need WebRTC or a WebSocket gateway. Keep native Windows as the quality bar, then prove a single-player WebAssembly build before promising browser cross-play.

## Release ladder

1. Portable Windows ZIP: current target, local 2v2 against AI, checksummed release asset.
2. Signed installer only if SmartScreen friction and automatic updates justify it.
3. LAN/direct-IP host-authoritative prototype behind a developer flag.
4. Steam lobbies/relay integration, versioned protocol, reconnects, and 2v2 online beta.
5. Optional WebAssembly demo; cross-play only after a browser-compatible gateway exists.

Research basis: [CMake CPack](https://cmake.org/cmake/help/latest/manual/cpack.1.html), [GitHub release links](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases), [raylib](https://github.com/raysan5/raylib), [Jolt supported platforms](https://github.com/jrouwe/JoltPhysics), [Valve GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets), and [Steam Networking](https://partner.steamgames.com/doc/features/multiplayer/networking).
