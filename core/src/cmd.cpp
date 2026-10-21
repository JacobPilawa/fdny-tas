#include "cmd.h"

#include <windows.h>

#include "console.h"
#include "hooks.h"
#include "log.h"

namespace fdcore::cmd {
namespace {

struct Command {
    const char* name;
    const char* help;
    CommandFn fn;
};

constexpr int kMaxCommands = 128;
constexpr int kMaxCvars = 256;
Command g_commands[kMaxCommands];
int g_nCommands = 0;
Cvar* g_cvars[kMaxCvars];
int g_nCvars = 0;

constexpr int kQueue = 64;
char g_queue[kQueue][256];
int g_qHead = 0, g_qTail = 0;
CRITICAL_SECTION g_qLock;
bool g_qInit = false;

bool IEq(const char* a, const char* b) { return lstrcmpiA(a, b) == 0; }

// Splits a line into whitespace-separated tokens; "quoted strings" stay whole.
int Tokenize(const char* line, char* buf, int cap, const char** argv) {
    int argc = 0, o = 0;
    const char* p = line;
    while (*p && argc < kMaxArgs) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        argv[argc++] = buf + o;
        if (*p == '"') {
            ++p;
            while (*p && *p != '"' && o < cap - 1) buf[o++] = *p++;
            if (*p == '"') ++p;
        } else {
            while (*p && *p != ' ' && *p != '\t' && o < cap - 1) buf[o++] = *p++;
        }
        if (o < cap) buf[o++] = 0;
    }
    return argc;
}

Command* FindCommand(const char* name) {
    for (int i = 0; i < g_nCommands; ++i)
        if (IEq(g_commands[i].name, name)) return &g_commands[i];
    return nullptr;
}

// --- builtins --------------------------------------------------------------
void CmdEcho(int argc, const char* const* argv) {
    char out[512];
    int o = 0;
    for (int i = 1; i < argc; ++i) {
        for (const char* s = argv[i]; *s && o < static_cast<int>(sizeof(out)) - 2; ++s) out[o++] = *s;
        if (i + 1 < argc && o < static_cast<int>(sizeof(out)) - 2) out[o++] = ' ';
    }
    out[o] = 0;
    console::Print("%s", out);
}

void CmdCvarlist(int, const char* const*) {
    for (int i = 0; i < g_nCvars; ++i) {
        const Cvar* v = g_cvars[i];
        console::Print("%s \"%s\"%s - %s", v->name, v->value, v->readOnly ? " (read-only)" : "", v->help);
    }
    console::Print("%d variable(s)", g_nCvars);
}

void CmdCmdlist(int, const char* const*) {
    for (int i = 0; i < g_nCommands; ++i) console::Print("%s - %s", g_commands[i].name, g_commands[i].help);
    console::Print("%d command(s)", g_nCommands);
}

// exec <file>: runs FDNYCore config files (<file> or <file>.cfg next to the
// game exe). If no such file exists the line is the game's own `exec` verb
// (Data\Scripts / Data\Maps), so it is handed back to the game untouched.
bool ExecFile(const char* name) {
    char path[MAX_PATH];
    wsprintfA(path, "%s%s", console::GameDir(), name);
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        wsprintfA(path, "%s%s.cfg", console::GameDir(), name);
        f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    if (f == INVALID_HANDLE_VALUE) return false;
    static char buf[32 * 1024];
    DWORD got = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(f);
    buf[got] = 0;
    console::Print("exec %s", path);
    char* p = buf;
    int lines = 0;
    while (*p) {
        char* e = p;
        while (*e && *e != '\n' && *e != '\r') ++e;
        char save = *e;
        *e = 0;
        const char* s = p;
        while (*s == ' ' || *s == '\t') ++s;
        if (*s && !(s[0] == '/' && s[1] == '/') && s[0] != '#') {
            if (!Dispatch(s)) QueueGameLine(s);
            ++lines;
        }
        *e = save;
        p = e;
        while (*p == '\n' || *p == '\r') ++p;
    }
    log::Info("exec: %s (%d line(s))", path, lines);
    return true;
}

Cvar g_fdVersion = {"fd_version", "", "FDNYCore version", true, nullptr, {0}};

void OnHooksFrame(Cvar& v) {
    bool on = v.value[0] != '0';
    if (!hooks::SetEnabled("frame", on)) console::Print("fd_hooks_frame: frame hooks unavailable");
}
Cvar g_fdHooksFrame = {"fd_hooks_frame", "1", "frame hooks on/off (debugging; disables per-frame features)",
                       false, OnHooksFrame, {0}};

} // namespace

void RegisterCommand(const char* name, const char* help, CommandFn fn) {
    if (g_nCommands < kMaxCommands) g_commands[g_nCommands++] = {name, help, fn};
}

void RegisterCvar(Cvar* v) {
    if (g_nCvars >= kMaxCvars) return;
    lstrcpynA(v->value, v->defaultValue, sizeof(v->value));
    g_cvars[g_nCvars++] = v;
}

Cvar* FindCvar(const char* name) {
    for (int i = 0; i < g_nCvars; ++i)
        if (IEq(g_cvars[i]->name, name)) return g_cvars[i];
    return nullptr;
}

bool SetCvar(Cvar* v, const char* value) {
    if (v->readOnly) return false;
    lstrcpynA(v->value, value, sizeof(v->value));
    if (v->onChange) v->onChange(*v);
    return true;
}

bool Dispatch(const char* line) {
    char buf[512];
    const char* argv[kMaxArgs];
    int argc = Tokenize(line, buf, sizeof(buf), argv);
    if (argc == 0) return false;
    if (IEq(argv[0], "exec")) {
        if (argc < 2) {
            console::Print("usage: exec <file>  (FDNYCore .cfg next to the game exe)");
            return true;
        }
        return ExecFile(argv[1]);  // false -> the game's own exec verb
    }
    if (Command* c = FindCommand(argv[0])) {
        log::Info("cmd: %s", line);
        c->fn(argc, argv);
        return true;
    }
    if (Cvar* v = FindCvar(argv[0])) {
        log::Info("cmd: %s", line);
        if (argc == 1) {
            console::Print("%s is \"%s\" (default \"%s\") - %s", v->name, v->value, v->defaultValue, v->help);
        } else if (!SetCvar(v, argv[1])) {
            console::Print("%s is read-only", v->name);
        }
        return true;
    }
    if (argv[0][0] == 'f' && argv[0][1] == 'd' && argv[0][2] == '_') {
        // Our namespace: never let a mistyped fd_ command reach the game.
        console::Print("unknown command: %s", argv[0]);
        return true;
    }
    return false;
}

void RegisterBuiltins() {
    if (!g_qInit) {
        InitializeCriticalSection(&g_qLock);
        g_qInit = true;
    }
    RegisterCommand("echo", "echo <text>: print text to the console", CmdEcho);
    RegisterCommand("cvarlist", "list FDNYCore variables", CmdCvarlist);
    RegisterCommand("cmdlist", "list FDNYCore commands", CmdCmdlist);
    RegisterCvar(&g_fdVersion);
    lstrcpynA(g_fdVersion.value, console::CoreVersion(), sizeof(g_fdVersion.value));
    RegisterCvar(&g_fdHooksFrame);
}

void QueueGameLine(const char* line) {
    EnterCriticalSection(&g_qLock);
    int next = (g_qTail + 1) % kQueue;
    if (next != g_qHead) {
        lstrcpynA(g_queue[g_qTail], line, sizeof(g_queue[0]));
        g_qTail = next;
    } else {
        log::Warn("cmd: game-line queue full; dropped: %s", line);
    }
    LeaveCriticalSection(&g_qLock);
}

bool PopGameLine(char* out, int cap) {
    EnterCriticalSection(&g_qLock);
    bool have = g_qHead != g_qTail;
    if (have) {
        lstrcpynA(out, g_queue[g_qHead], cap);
        g_qHead = (g_qHead + 1) % kQueue;
    }
    LeaveCriticalSection(&g_qLock);
    return have;
}

} // namespace fdcore::cmd
