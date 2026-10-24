"""RE helper (demos spike): disassemble the exe. python -I re_dis.py <hexaddr>:<count> ..."""
import sys
import pefile, capstone
pe = pefile.PE(r"C:\fdny-decomp\FDNY SP 1.1\FDNYFirefighter.exe")
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
for spec in sys.argv[1:]:
    a, n = spec.split(":")
    a = int(a, 16); n = int(n)
    print(f"===== 0x{a:08x}")
    for k, i in enumerate(md.disasm(img[a - base:a - base + 0x600], a)):
        if k >= n:
            break
        print(f"  {i.address:08x}  {i.mnemonic} {i.op_str}")
        if i.mnemonic == "ret" and k > 3 and n < 0:
            break
