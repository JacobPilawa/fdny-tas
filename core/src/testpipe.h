// Debug-only harness channel (see testpipe.cpp). Absent from Release: wrap
// Register calls in #ifdef FDCORE_DEBUG.
#pragma once

#ifdef FDCORE_DEBUG
namespace fdcore::testpipe {
void Start();
// A feature's own request: "<name>[ <args>]" -> fn(args or "", resp, cap).
// The core answers ping / stats / hook / snapshot itself.
using Handler = void (*)(const char* args, char* resp, int cap);
void Register(const char* name, Handler fn);
}
#endif
