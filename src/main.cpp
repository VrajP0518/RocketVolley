#include "rocket_volley/Game.hpp"
#include "rocket_volley/GameplayLogic.hpp"
#include "rocket_volley/MultiplayerProtocol.hpp"

#include <exception>
#include <iostream>
#include <string_view>

namespace {

int runGame(bool smokeTest) {
    try {
        rv::Game game(smokeTest);
        return game.run();
    } catch (const std::exception &error) {
        std::cerr << "Rocket Volley failed: " << error.what() << '\n';
        return 1;
    }
}

int runProtocolTest() {
    return rv::net::protocolSelfTest() && rv::gameplayLogicSelfTest() ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    const std::string_view command = argc > 1 ? argv[1] : "";
    if (command == "--protocol-test") return runProtocolTest();
    return runGame(command == "--smoke-test");
}

#ifdef _WIN32
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR commandLine, int) {
    const std::string_view arguments = commandLine != nullptr ? commandLine : "";
    if (arguments.find("--protocol-test") != std::string_view::npos) return runProtocolTest();
    return runGame(arguments.find("--smoke-test") != std::string_view::npos);
}
#endif
