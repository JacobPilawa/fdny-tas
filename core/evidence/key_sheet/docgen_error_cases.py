import os, shutil, sys, tempfile
sys.path.insert(0, r"C:\fdny-decomp\core\tools")
import docgen
real = docgen.read_source()
print("clean source reads:", len(real["cvars"]), "cvars", len(real["commands"]), "commands")
cases = [
    ("tp.cpp", 'cmd::RegisterCommand("fd_tp_load", "teleport to the fd_tp_save location (same map only)", CmdLoad);',
     'cmd::RegisterCommand("fd_tp_load", kHelp, CmdLoad);'),
    ("tp.cpp", 'binds::BindDefault("l", "fd_tp_load");', 'binds::BindDefault("l", "fd_tp_load", 1);'),
    ("hud.cpp", "cmd::RegisterCvar(&g_fdHudPlayer);", "cmd::RegisterCvar(&g_fdNope);"),
]
for name, old, new in cases:
    tmp = tempfile.mkdtemp()
    src = os.path.join(tmp, "src")
    shutil.copytree(r"C:\fdny-decomp\core\src", src)
    p = os.path.join(src, "features", name)
    t = open(p, encoding="utf-8").read()
    assert old in t, old
    open(p, "w", encoding="utf-8").write(t.replace(old, new))
    docgen.SRC, docgen.FEAT = src, os.path.join(src, "features")
    try:
        docgen.read_source()
        print("NOT CAUGHT:", new)
    except docgen.ReadError as e:
        print("caught:", str(e).split("src/")[-1] if "src/" in str(e) else e)
    shutil.rmtree(tmp)
