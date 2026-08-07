#include "rocket_volley/Game.hpp"

#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
    try {
        const bool smokeTest = argc > 1 && std::string_view(argv[1]) == "--smoke-test";
        rv::Game game(smokeTest);
        return game.run();
    } catch (const std::exception &error) {
        std::cerr << "Rocket Volley failed: " << error.what() << '\n';
        return 1;
    }
}
