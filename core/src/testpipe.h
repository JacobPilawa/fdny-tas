// Debug-only harness channel (see testpipe.cpp). Absent from Release.
#pragma once

#ifdef FDCORE_DEBUG
namespace fdcore::testpipe {
void Start();
}
#endif
