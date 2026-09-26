#include "VersusAudio.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <random>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace versus::audio {
namespace {

constexpr int kResultMusicChannel = 2;
bool s_resultMusicPlaying = false;

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool matchesVariant(std::filesystem::path const& path, std::string_view base) {
    auto extension = lowercase(path.extension().string());
    if (extension != ".mp3" && extension != ".wav" && extension != ".ogg") return false;
    auto stem = lowercase(path.stem().string());
    if (!stem.starts_with(base)) return false;
    auto suffix = std::string_view(stem).substr(base.size());
    return suffix.empty() || std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

std::vector<std::filesystem::path> variants(std::string_view base) {
    std::vector<std::filesystem::path> result;
    auto* mod = Mod::get();
    if (!mod) return result;
    auto const root = mod->getResourcesDir();
    std::array const folders {root / "sounds", root / "resources" / "sounds", root};
    std::error_code error;
    for (auto const& folder : folders) {
        error.clear();
        if (!std::filesystem::is_directory(folder, error) || error) continue;
        for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
            if (!it->is_regular_file(error) || error || !matchesVariant(it->path(), base)) continue;
            auto canonical = std::filesystem::weakly_canonical(it->path(), error);
            if (error) { error.clear(); canonical = it->path(); }
            if (std::find(result.begin(), result.end(), canonical) == result.end()) result.push_back(std::move(canonical));
        }
        error.clear();
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::filesystem::path randomVariant(std::string_view base) {
    auto sounds = variants(base);
    if (sounds.empty()) return {};
    static std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<size_t> pick(0, sounds.size() - 1);
    return sounds[pick(generator)];
}

void playFile(std::string_view base, char const* fallback) {
    if (auto* engine = FMODAudioEngine::sharedEngine()) {
        auto path = randomVariant(base);
        if (!path.empty()) {
            engine->playEffect(gd::string(path.string()));
            return;
        }
    }
    playBuiltin(fallback);
}

}

void playBuiltin(char const* filename) {
    if (!filename || !*filename) return;
    if (auto* engine = FMODAudioEngine::sharedEngine()) engine->playEffect(filename);
}

void play(Cue cue) {
    switch (cue) {
        case Cue::ReadyLock: playBuiltin("reward01.ogg"); break;
        case Cue::Finish: playFile("finish", "highscoreGet02.ogg"); break;
        case Cue::Throw: playFile("throw", "playSound_01.ogg"); break;
        case Cue::Blast: playFile("blast", "magicExplosion.ogg"); break;
    }
}

void startResultMusic() {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;
    auto path = randomVariant("finbgm");
    if (path.empty()) return;
    engine->stopMusic(kResultMusicChannel);
    engine->playMusic(gd::string(path.string()), true, .18f, kResultMusicChannel);
    engine->resumeMusic(kResultMusicChannel);
    s_resultMusicPlaying = true;
}

void stopResultMusic() {
    if (!s_resultMusicPlaying) return;
    if (auto* engine = FMODAudioEngine::sharedEngine()) engine->stopMusic(kResultMusicChannel);
    s_resultMusicPlaying = false;
}

}
