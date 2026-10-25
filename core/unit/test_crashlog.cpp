// crashlog: an access violation is reported with its module+offset, the registers and a
// stack walk, and the program carries on (the handler only logs). The fault is one Windows
// catches itself (IsBadReadPtr), so the test needs no game and survives it.
#include <windows.h>

#include "../src/crashlog.h"
#include "unit.h"

void TestCrashlogReportsFault() {
    unit::ClearLog();
    fdcore::crashlog::Install();
    const BOOL bad = IsBadReadPtr(reinterpret_cast<const void*>(0x10), 4);
    CHECK(bad);  // caught: we're still here
    CHECK(unit::LogContains("crashlog: exception c0000005 at"));
    CHECK(unit::LogContains("; data 00000010"));
    CHECK(unit::LogContains("crashlog: eax="));
    CHECK(unit::LogContains("(fdcore_unit.exe+"));  // the stack walk reaches our own frames
}
