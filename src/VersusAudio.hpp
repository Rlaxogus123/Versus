#pragma once

namespace versus::audio {

enum class Cue {
    ReadyLock,
    Finish,
    Throw,
    Blast,
};

void play(Cue cue);
void playBuiltin(char const* filename);
void startResultMusic();
void stopResultMusic();

}
