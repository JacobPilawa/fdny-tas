// Command + variable registry behind the console. Lines typed into the game's
// console are offered here first; anything not registered goes to the game.
#pragma once

namespace fdcore::cmd {

constexpr int kMaxArgs = 16;

using CommandFn = void (*)(int argc, const char* const* argv);

struct Cvar {
    const char* name;
    const char* defaultValue;
    const char* help;
    bool readOnly;
    void (*onChange)(Cvar& v);  // may be null
    char value[128];
};

void RegisterCommand(const char* name, const char* help, CommandFn fn);
// The Cvar must outlive the core (static storage). Value starts at its default.
void RegisterCvar(Cvar* v);
Cvar* FindCvar(const char* name);
bool SetCvar(Cvar* v, const char* value);  // false if read-only

// Executes one line if it's ours. Returns false (untouched) if the first
// token isn't a registered command or variable, so the caller can hand the
// line to the game.
bool Dispatch(const char* line);

// Registers echo / cvarlist / cmdlist / exec and the core's own variables.
void RegisterBuiltins();

// Lines from `exec` that belong to the game are queued and handed to the
// game one per frame (its script engine stores one pending line at a time).
void QueueGameLine(const char* line);
bool PopGameLine(char* out, int cap);

} // namespace fdcore::cmd
