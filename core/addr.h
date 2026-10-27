// Address table: the only place game addresses live (generated from
// core/addresses.json). Selects the version by exe + Genesis SHA-256, checks
// every entry's code signature in memory, and resolves pointer chains with
// guarded reads. Modules use Ids, never raw addresses.
#pragma once

#include <stdint.h>

#include "gen/addresses_gen.h"

namespace fdcore::addr {

// Selects the table version and verifies signatures. Never fails hard: an
// unknown version or a bad signature only marks entries unusable.
void Init(const char* exeSha256, const char* genesisSha256, uintptr_t exeBase,
          uintptr_t genesisBase);

#ifdef FDCORE_UNIT
void ResetForUnitTest();  // tests/unit runs Init several times in one process
#endif

bool VersionKnown();
const char* VersionName();
bool Usable(Id id);          // version known and this entry's signature verified
const char* Name(Id id);

// Final address of an entry (null pointer anywhere in the chain -> false).
bool Resolve(Id id, uintptr_t* out);
// For work that must happen in DllMain, before Init (hashing) has run: the
// address of an exe code entry if its signature matches the running exe in
// any table version, else 0. The signature check alone guards the patch.
uintptr_t EarlyCodeAddress(Id id);

// Load base of a module (0 = exe, 1 = Genesis.dll).
uintptr_t ModuleBase(int module);

// Offset of a struct-field entry (module "field" in addresses.json).
uint32_t Field(Id id);

// Address of a verified code entry (a hooked export); 0 if unusable.
uintptr_t ExportAddress(Id id);
bool ReadI32(Id id, int32_t* out);
bool ReadF32(Id id, float* out);
// Reads up to cap-1 bytes of a NUL-terminated string.
bool ReadStr(Id id, char* out, int cap);

// Guarded memory read: false if any byte isn't committed+readable.
bool SafeRead(uintptr_t addr, void* out, uint32_t size);
bool SafeWrite(uintptr_t addr, const void* in, uint32_t size);
// Guarded write of a table entry's value (data entries only).
bool WriteU8(Id id, uint8_t v);
bool WriteI32(Id id, int32_t v);

} // namespace fdcore::addr
