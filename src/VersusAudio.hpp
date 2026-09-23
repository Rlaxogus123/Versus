#pragma once

namespace versus::audio {

enum class Cue {
    UiOpen,
    ReadyLock,
    WinnerReveal,
    ExecutionImpact,
    ResultReveal,
};

void play(Cue cue);
void playBuiltin(char const* filename);

}
