#pragma once

#include <memory>

namespace rv {

class Game {
public:
    explicit Game(bool smokeTest = false, bool headlessTest = false);
    ~Game();

    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;

    int run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rv
