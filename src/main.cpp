#include "rocket_volley/Game.hpp"

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

} // namespace

int main(int argc, char **argv) {
    return runGame(argc > 1 && std::string_view(argv[1]) == "--smoke-test");
}

#ifdef _WIN32
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR commandLine, int) {
    const std::string_view arguments = commandLine != nullptr ? commandLine : "";
    return runGame(arguments.find("--smoke-test") != std::string_view::npos);
}
#endif
