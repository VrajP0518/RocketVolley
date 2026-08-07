#pragma once

#include <memory>

namespace rv {

class Game {
public:
    Game();
    ~Game();

    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;

    int run(bool smokeTest = false);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rv
