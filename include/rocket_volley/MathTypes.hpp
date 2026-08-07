#pragma once

namespace rv {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Rotation {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

struct Transform {
    Vec3 position{};
    Rotation rotation{};
};

} // namespace rv
