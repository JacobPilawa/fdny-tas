// FDNYCore log: one file (FDNYCore.log next to the .asi), timestamped lines.
// Levels: INFO / WARN / ERROR. The harness fails a test on any ERROR line.
#pragma once

namespace fdcore::log {

bool Open(const char* path);        // truncates
bool OpenAppend(const char* path);  // keeps what's there (the inert second copy's line)
void Close();
void Info(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Error(const char* fmt, ...);
// One INFO line of any length, unformatted: "<tag><text>".
void Raw(const char* tag, const char* text);

} // namespace fdcore::log
