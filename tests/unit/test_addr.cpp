// addr: version selection by hash, signature checks against memory, Genesis relocation,
// patch sites taken by another mod, pointer chains. The exe and Genesis.dll are fake
// images built from the table's own signatures, so this runs without the game (it
// replaces the logic half of addr_verified / addr_bad_signature / addr_unknown_exe; those
// keep proving the real exe verifies and the game runs on).
#include <string.h>
#include <windows.h>

#include "../../core/addr.h"
#include "unit.h"

using fdcore::addr::Id;
namespace addr = fdcore::addr;

namespace {

const addr::Version& V() { return addr::kVersions[0]; }
constexpr int kCount = static_cast<int>(Id::Count);

struct Image {
    uint8_t* base = nullptr;
    uint32_t size = 0;
};

uint32_t Extent(int module) {
    uint32_t hi = 0;
    for (int i = 0; i < kCount; ++i) {
        const addr::Entry& e = V().entries[i];
        if (e.module != module || !e.rva) continue;
        if (e.rva + 64 > hi) hi = e.rva + 64;
        if (e.sigRva + 64 > hi) hi = e.sigRva + 64;
    }
    return (hi + 0xFFFF) & ~0xFFFFu;
}

// Genesis' code holds absolute pointers into its preferred base 0x10000000; the loader
// adds (base - 0x10000000) to each when it relocates. Signature windows that look like
// one get the same treatment, as VerifySig expects.
void Relocate(uint8_t* sig, int len, uint32_t delta) {
    for (int j = 0; j + 4 <= len; ++j) {
        uint32_t v;
        memcpy(&v, sig + j, 4);
        if (v >= 0x10000000u && v < 0x10400000u) {
            v += delta;
            memcpy(sig + j, &v, 4);
            j += 3;
        }
    }
}

// Both images with every signature the table expects. genesisAt = 0: anywhere.
bool Build(Image& exe, Image& gen, uintptr_t genesisAt) {
    exe.size = Extent(0), gen.size = Extent(1);
    exe.base = static_cast<uint8_t*>(VirtualAlloc(nullptr, exe.size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    gen.base = static_cast<uint8_t*>(
        VirtualAlloc(reinterpret_cast<void*>(genesisAt), gen.size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!exe.base || !gen.base) return false;
    const uint32_t delta = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gen.base) - 0x10000000u);
    for (int i = 0; i < kCount; ++i) {
        const addr::Entry& e = V().entries[i];
        if (!e.rva || !e.sigLen || e.module > 1) continue;
        uint8_t sig[addr::kMaxSig];
        memcpy(sig, e.sig, e.sigLen);
        if (e.module == 1 && delta) Relocate(sig, e.sigLen, delta);
        memcpy((e.module ? gen.base : exe.base) + e.sigRva, sig, e.sigLen);
    }
    return true;
}

void Free(Image& a, Image& b) {
    if (a.base) VirtualFree(a.base, 0, MEM_RELEASE);
    if (b.base) VirtualFree(b.base, 0, MEM_RELEASE);
    a = Image{}, b = Image{};
}

void Init(const Image& exe, const Image& gen, const char* exeSha = nullptr) {
    addr::ResetForUnitTest();
    unit::ClearLog();
    addr::Init(exeSha ? exeSha : V().exeSha256, V().genesisSha256, reinterpret_cast<uintptr_t>(exe.base),
               reinterpret_cast<uintptr_t>(gen.base));
}

// Hooked exports are also resolved by name (GetProcAddress), which a fake image can't
// answer: those entries stay unusable here, and only those.
bool ExpectUsable(const addr::Entry& e) { return e.rva && !e.exportName; }

void CheckAllVerify(const Image& exe, const Image& gen) {
    Init(exe, gen);
    CHECK(addr::VersionKnown());
    CHECK(strcmp(addr::VersionName(), V().name) == 0);
    int wrong = 0, exports = 0;
    for (int i = 0; i < kCount; ++i) {
        const addr::Entry& e = V().entries[i];
        if (e.exportName) ++exports;
        if (addr::Usable(static_cast<Id>(i)) != ExpectUsable(e)) {
            if (!wrong) printf("    first wrong entry: %s\n", e.name);
            ++wrong;
        }
    }
    CHECK(wrong == 0);
    CHECK(unit::LogCount("signature mismatch") == exports);  // the exports, nothing else
}

} // namespace

void TestAddrAllVerify() {
    Image exe, gen;
    CHECK(Build(exe, gen, 0));
    CheckAllVerify(exe, gen);
    Free(exe, gen);
}

void TestAddrGenesisAtPreferredBase() {
    // At 0x10000000 nothing is relocated: the signatures must match byte for byte.
    Image exe, gen;
    if (!Build(exe, gen, 0x10000000)) {
        printf("    (0x10000000 not free in this process: skipped)\n");
        Free(exe, gen);
        return;
    }
    CheckAllVerify(exe, gen);
    Free(exe, gen);
}

void TestAddrUnknownVersion() {
    Image exe, gen;
    CHECK(Build(exe, gen, 0));
    Init(exe, gen, "0000000000000000000000000000000000000000000000000000000000000000");
    CHECK(!addr::VersionKnown());
    CHECK(strcmp(addr::VersionName(), "unknown") == 0);
    bool any = false;
    for (int i = 0; i < kCount; ++i) any = any || addr::Usable(static_cast<Id>(i));
    CHECK(!any);
    CHECK(unit::LogContains("no address-table version matches; all modules disabled"));
    Free(exe, gen);
}

void TestAddrBadSignature() {
    // One corrupted byte disables exactly the entries checked at that spot.
    Image exe, gen;
    CHECK(Build(exe, gen, 0));
    const addr::Entry& bad = V().entries[static_cast<int>(Id::game_map_name)];
    exe.base[bad.sigRva + 3] ^= 0xFF;
    Init(exe, gen);
    CHECK(addr::VersionKnown());
    CHECK(!addr::Usable(Id::game_map_name));
    int wrong = 0;
    for (int i = 0; i < kCount; ++i) {
        const addr::Entry& e = V().entries[i];
        const bool hit = e.module == bad.module && e.sigLen && e.sigRva <= bad.sigRva + 3 &&
                         bad.sigRva + 3 < e.sigRva + e.sigLen;
        if (addr::Usable(static_cast<Id>(i)) != (ExpectUsable(e) && !hit)) ++wrong;
    }
    CHECK(wrong == 0);
    CHECK(unit::LogContains("signature mismatch: game.map_name"));
    CHECK(unit::LogContains("signatures: "));
    Free(exe, gen);
}

void TestAddrPatchSiteDetoured() {
    // Another mod's jmp at a patch site: a signature mismatch like any other, never patched (D55).
    Image exe, gen;
    CHECK(Build(exe, gen, 0));
    const addr::Entry& p = V().entries[static_cast<int>(Id::patch_victim_spawn)];
    exe.base[p.sigRva] = 0xE9;
    Init(exe, gen);
    CHECK(!addr::Usable(Id::patch_victim_spawn));
    CHECK(unit::LogContains("signature mismatch: patch.victim_spawn"));
    Free(exe, gen);
}

void TestAddrResolveChain() {
    // game.map_name: [exe+rva] -> p; [p+0x688] -> q; [q+0x0] -> r; r+0xc.
    Image exe, gen;
    CHECK(Build(exe, gen, 0));
    const addr::Entry& e = V().entries[static_cast<int>(Id::game_map_name)];
    CHECK(e.derefRoot && e.nchain == 3);
    static uint8_t heap[0x1000];
    uint8_t* p = heap;          // the root object
    uint8_t* q = heap + 0x800;  // what p+0x688 points at
    uint8_t* r = heap + 0xC00;  // what q+0x0 points at
    uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
    memcpy(exe.base + e.rva, &v, 4);
    v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(q));
    memcpy(p + e.chain[0], &v, 4);
    v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r));
    memcpy(q + e.chain[1], &v, 4);
    strcpy(reinterpret_cast<char*>(r + e.chain[2]), "training");
    Init(exe, gen);
    uintptr_t a = 0;
    CHECK(addr::Resolve(Id::game_map_name, &a));
    CHECK(a == reinterpret_cast<uintptr_t>(r + e.chain[2]));
    char name[24];
    CHECK(addr::ReadStr(Id::game_map_name, name, sizeof(name)) && strcmp(name, "training") == 0);
    memset(q + e.chain[1], 0, 4);  // a null link anywhere: no address, no read
    CHECK(!addr::Resolve(Id::game_map_name, &a));
    CHECK(!addr::ReadStr(Id::game_map_name, name, sizeof(name)));
    Free(exe, gen);
}

void TestAddrFieldOffsets() {
    // Struct-field entries are offsets: verified without memory, read as numbers.
    Image exe, gen;
    CHECK(Build(exe, gen, 0));
    Init(exe, gen);
    CHECK(addr::Field(Id::victim_pos) == V().entries[static_cast<int>(Id::victim_pos)].rva);
    CHECK(addr::Usable(Id::victim_pos));
    Free(exe, gen);
}
