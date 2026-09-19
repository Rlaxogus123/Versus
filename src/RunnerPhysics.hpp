#pragma once
#include <algorithm>
namespace versus {
struct RunnerJump {
    float height = 0.f, velocity = 0.f, heldFor = 0.f;
    bool held = false;
    void press() { if (held) return; held = true; if (height <= 0.f) { velocity = 265.f; heldFor = 0.f; } }
    void release() { held = false; if (velocity > 120.f) velocity = 120.f; }
    void step(float dt) {
        dt = std::clamp(dt, 0.f, 1.f / 60.f);
        if (height <= 0.f && velocity <= 0.f) return;
        heldFor += dt;
        float gravity = held && heldFor < .24f && velocity > 0.f ? 430.f : 1000.f;
        velocity -= gravity * dt; height += velocity * dt;
        if (height < 0.f) { height = 0.f; velocity = 0.f; }
    }
};
}
