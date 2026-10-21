// FDNYCore log: one file (FDNYCore.log next to the .asi), timestamped lines.
// Levels: INFO / WARN / ERROR. The harness fails a test on any ERROR line.
#pragma once

namespace fdcore::log {

bool Open(const char* path);
void Close();
void Info(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Error(const char* fmt, ...);

} // namespace fdcore::log
