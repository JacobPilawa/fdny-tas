import struct
import os

SCR = r"C:\Users\ADMINI~1\AppData\Local\Temp\1\claude\C--fdny-decomp\67f4d207-76bc-4e8a-afd3-3dc7655005a2\scratchpad"
GEN = r"C:\fdny-decomp\FDNY SP 1.1\Genesis.dll"
EXE = r"C:\fdny-decomp\FDNY SP 1.1\FDNYFirefighter.exe"


class PE:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        d = self.d
        lfanew = struct.unpack_from("<I", d, 0x3C)[0]
        assert d[lfanew:lfanew + 4] == b"PE\0\0", "not PE"
        _, nsec, _, _, _, optsize, _ = struct.unpack_from("<HHIIIHH", d, lfanew + 4)
        opt = lfanew + 24
        magic = struct.unpack_from("<H", d, opt)[0]
        self.is64 = magic == 0x20B
        dd_off = opt + (112 if self.is64 else 96)
        self.dd = []
        for i in range(16):
            self.dd.append(struct.unpack_from("<II", d, dd_off + 8 * i))
        sec_off = opt + optsize
        self.sections = []
        for i in range(nsec):
            name, vsize, va, rawsz, rawptr = struct.unpack_from("<8sIIII", d, sec_off + 40 * i)
            self.sections.append((va, max(vsize, rawsz), rawptr, name.rstrip(b"\0").decode("ascii", "replace")))

    def off(self, rva):
        for va, size, rawptr, _ in self.sections:
            if va <= rva < va + size:
                return rawptr + (rva - va)
        raise ValueError("rva %x not in any section" % rva)

    def cstr(self, rva):
        o = self.off(rva)
        end = self.d.index(b"\0", o)
        return self.d[o:end].decode("latin-1")


def exports(pe):
    rva, size = pe.dd[0]
    out = []  # (name, ordinal, rva_hex, note)
    if not rva:
        return out, None
    o = pe.off(rva)
    (_, _, _, _, name_rva, base, nfuncs, nnames, aof, aon, aono) = struct.unpack_from(
        "<IIHHIIIIIII", pe.d, o)
    dll_name = pe.cstr(name_rva)
    funcs = [struct.unpack_from("<I", pe.d, pe.off(aof) + 4 * i)[0] for i in range(nfuncs)]
    names = {}
    for j in range(nnames):
        nrva = struct.unpack_from("<I", pe.d, pe.off(aon) + 4 * j)[0]
        ordidx = struct.unpack_from("<H", pe.d, pe.off(aono) + 2 * j)[0]
        names[ordidx] = pe.cstr(nrva)
    for i, frva in enumerate(funcs):
        if frva == 0:
            continue
        ordinal = base + i
        name = names.get(i, "(by-ordinal-only)")
        note = ""
        if rva <= frva < rva + size:
            note = "FORWARD -> " + pe.cstr(frva)
        out.append((name, ordinal, frva, note))
    return out, dll_name


def imports(pe):
    out = []  # (dll, func, delay_flag)
    # regular imports
    rva, size = pe.dd[1]
    if rva:
        o = pe.off(rva)
        while True:
            oft, ts, fc, name, ft = struct.unpack_from("<IIIII", pe.d, o)
            if oft == 0 and ft == 0 and name == 0:
                break
            dll = pe.cstr(name)
            trva = oft if oft else ft
            t = pe.off(trva)
            ent = 8 if pe.is64 else 4
            i = 0
            while True:
                v = struct.unpack_from("<Q" if pe.is64 else "<I", pe.d, t + ent * i)[0]
                if v == 0:
                    break
                if v & (1 << (63 if pe.is64 else 31)):
                    fn = "#%d" % (v & 0xFFFF)
                else:
                    fn = pe.cstr((v & 0x7FFFFFFF) + 2)
                out.append((dll, fn, ""))
                i += 1
            o += 20
    # delay-load imports (data dir 13)
    rva, size = pe.dd[13]
    if rva:
        o = pe.off(rva)
        while True:
            attrs, dlln, _, iat, int_, _, _, _ = struct.unpack_from("<IIIIIIII", pe.d, o)
            if dlln == 0:
                break
            if attrs & 1:
                dll = pe.cstr(dlln)
                intrva = int_
            else:
                dll = pe.cstr(dlln - pe.dd and dlln)  # VA form (rare); treat as RVA below
            t = pe.off(intrva)
            ent = 8 if pe.is64 else 4
            i = 0
            while True:
                v = struct.unpack_from("<Q" if pe.is64 else "<I", pe.d, t + ent * i)[0]
                if v == 0:
                    break
                if v & (1 << (63 if pe.is64 else 31)):
                    fn = "#%d" % (v & 0xFFFF)
                else:
                    fn = pe.cstr((v & 0x7FFFFFFF) + 2)
                out.append((dll, fn, " (delay)"))
                i += 1
            o += 32
    return out


def main():
    gen = PE(GEN)
    exe = PE(EXE)

    gexp, gdllname = exports(gen)
    gexp_sorted = sorted(gexp, key=lambda x: x[1])
    with open(os.path.join(SCR, "genesis_exports.txt"), "w", encoding="utf-8") as f:
        f.write("# Genesis.dll export table (internal name: %s)\n" % gdllname)
        f.write("# total exported entries with RVA: %d\n" % len(gexp))
        f.write("# name\tordinal\tRVA\tnote\n")
        for name, ordn, rva, note in gexp_sorted:
            f.write("%s\t%d\t0x%08X\t%s\n" % (name, ordn, rva, note))

    imps = imports(exe)
    with open(os.path.join(SCR, "exe_imports.txt"), "w", encoding="utf-8") as f:
        f.write("# FDNYFirefighter.exe import table (regular + delay)\n")
        f.write("# total imported functions: %d\n" % len(imps))
        for dll, fn, flag in imps:
            f.write("%s!%s%s\n" % (dll, fn, flag))

    print("GENESIS sections:", [(s[3], hex(s[0])) for s in gen.sections])
    print("GENESIS export internal name:", gdllname, "count:", len(gexp))
    print("---- Genesis exports (ordinal name RVA) ----")
    for name, ordn, rva, note in gexp_sorted:
        print("%d %s 0x%08X %s" % (ordn, name, rva, note))
    print("---- EXE imports ----")
    dlls = []
    for dll, fn, flag in imps:
        if dll not in dlls:
            dlls.append(dll)
    print("DLLs:", dlls)
    print("total imports:", len(imps))
    for dll, fn, flag in imps:
        print("%s!%s%s" % (dll, fn, flag))


if __name__ == "__main__":
    main()
