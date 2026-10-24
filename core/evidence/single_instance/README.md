# Single-instance guard (D43)

Releases are named `FDNYCore-<version>.asi`, so an update dropped beside an older copy (or
beside `FDNYCore.asi`) would load two cores, each hooking the same functions.

## Test

`tests/core_single_instance.yaml`, 2026-10-24, Debug, PASS (12/12):

- installed: `FDNYCore-test.asi` and `FDNYCoreZ.asi` (both the Debug build); `FDNYCore.asi` removed for the run;
- `FDNYCore-test.asi` loads first (ASI loader order) and starts; `FDNYCoreZ.asi` logs one line and stays inert:

```
[INFO] FDNYCore loaded from C:\fdny-decomp\FDNY SP 1.1\FDNYCore-test.asi
[WARN] FDNYCore 0.18.1 at C:\fdny-decomp\FDNY SP 1.1\FDNYCoreZ.asi stays inert: another copy is already loaded (C:\fdny-decomp\FDNY SP 1.1\FDNYCore-test.asi); keep one FDNYCore*.asi in the game folder
[INFO] profile: ready (fd_profile, fd_watermark); other asi: alttabfix FDNYCoreZ FDNYFirefighterAmericanHeroFOVFix
```

- "FDNYCore loaded from", "FDNYCore ready" and "modules: " each appear once (one core started);
- the watermark omits the running core (`FDNYCore-test`) though it isn't named `FDNYCore.asi`, and lists the inert copy;
- no crash, 0 ERROR lines.

Files: `FDNYCore.log`, `result.json`, `watermark_two_cores.png`.

Also rerun after the change: `core_loads`, `addr_bad_signature` (Debug address override, now found by
module handle), `recorder`, `draw_layers`: all PASS.
