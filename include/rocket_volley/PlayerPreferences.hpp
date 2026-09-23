#pragma once

#include <algorithm>
#include <ostream>
#include <string_view>

namespace rv {

// Optional version-3 settings keys. Old profiles keep the original mix/visuals.
struct PlayerPreferences {
    int musicVolume = 100;
    int effectsVolume = 100;
    int cameraShake = 100;
    bool pointReplays = true;

    bool readSetting(std::string_view name, int value) {
        if (name == "point_replays") {
            pointReplays = value > 0;
            return true;
        }
        int *setting = name == "music_volume" ? &musicVolume
            : name == "effects_volume" ? &effectsVolume
            : name == "camera_shake" ? &cameraShake : nullptr;
        if (!setting) return false;
        *setting = std::clamp(value, 0, 100);
        return true;
    }

    void writeSettings(std::ostream &output) const {
        output << "music_volume=" << std::clamp(musicVolume, 0, 100) << '\n'
            << "effects_volume=" << std::clamp(effectsVolume, 0, 100) << '\n'
            << "camera_shake=" << std::clamp(cameraShake, 0, 100) << '\n'
            << "point_replays=" << (pointReplays ? 1 : 0) << '\n';
    }

    float musicGain() const { return static_cast<float>(std::clamp(musicVolume, 0, 100)) / 100.0F; }
    float effectsGain() const { return static_cast<float>(std::clamp(effectsVolume, 0, 100)) / 100.0F; }
    float shakeGain() const { return static_cast<float>(std::clamp(cameraShake, 0, 100)) / 100.0F; }
};

} // namespace rv
