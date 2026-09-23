#include "VersusAudio.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <array>
#include <filesystem>
#include <string_view>

using namespace geode::prelude;

namespace versus::audio {
namespace {

struct CueSound {
    std::string_view custom;
    char const* fallback;
};

constexpr std::array<CueSound, 5> kSounds {{
    {"ui-open.ogg", "playSound_01.ogg"},
    {"ready-lock.ogg", "reward01.ogg"},
    {"winner-reveal.ogg", "highscoreGet02.ogg"},
    {"execution-impact.ogg", "magicExplosion.ogg"},
    {"result-reveal.ogg", "reward01.ogg"},
}};

std::filesystem::path customPath(std::string_view filename) {
    auto* mod = Mod::get();
    if (!mod) return {};
    auto const root = mod->getResourcesDir();
    std::array const candidates {
        root / "sounds" / filename,
        root / filename,
    };
    std::error_code error;
    for (auto const& path : candidates) {
        error.clear();
        if (std::filesystem::is_regular_file(path, error) && !error) return path;
    }
    return {};
}

}

void playBuiltin(char const* filename) {
    if (!filename || !*filename) return;
    if (auto* engine = FMODAudioEngine::sharedEngine()) {
        engine->playEffect(filename);
    }
}

void play(Cue cue) {
    auto const index = static_cast<size_t>(cue);
    if (index >= kSounds.size()) return;

    auto const& sound = kSounds[index];
    auto const path = customPath(sound.custom);
    if (!path.empty()) {
        if (auto* engine = FMODAudioEngine::sharedEngine()) {
            engine->playEffect(gd::string(path.string()));
            return;
        }
    }
    playBuiltin(sound.fallback);
}

}
