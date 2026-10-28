// Modal spectator camera service. Replay/viewer modules supply target poses; this
// service owns camera input/override and the paused simulation, without file knowledge.
#pragma once
#include "camera.h"
namespace fdcore::spectator {
constexpr int kMaxTargets = 8;
struct Target {
    float pos[3], forward[3];
    bool valid;
};
enum class Action { TogglePlay, Exit, SeekBack, SeekForward, Restart, ToggleXray };
using ActionHandler = void (*)(Action action);
struct State {
    bool active, free;
    int followed, targets;
    float distance, yaw, pitch, pos[3];
};
bool Init();
bool Prepare();     // preconfigure follow pose, input and hooks without affecting menu camera
void PrimeCamera(); // apply the prepared pose before the selected stage first renders
bool Start();       // a loaded level only; preserves a pause that was already active
void Stop();
bool Active();
void SetPending(bool pending);
bool Pending();
void SetTargets(const Target* targets, int count);
void SetActionHandler(ActionHandler handler);
void SetFree(bool free);
void Follow(int index);   // re-hook to an available target
void Zoom(float notches); // positive moves closer; wheel or equivalent UI control
void ReadState(State* state);
} // namespace fdcore::spectator

namespace fdcore::spectator::input {
struct Key {
    bool previous, pending;
    void Reset(bool down) {
        previous = down;
        pending = false;
    }
    void Message(bool repeat) {
        if (!repeat && !previous)
            pending = true;
    }
    void Release() { previous = false; }
    bool Poll(bool down, bool live) {
        const bool pressed = live && (pending || (down && !previous));
        pending = false;
        previous = down;
        return pressed;
    }
};
} // namespace fdcore::spectator::input
