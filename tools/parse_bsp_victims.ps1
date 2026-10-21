# parse_bsp_victims.ps1 -- extract victim spawn layouts from FDNY .bsp files.
#
# Genesis3D entity lump = flat stream of [u32 len][bytes][NUL] tokens grouped
# into entities, each terminated by a `%name%` -> <instance-id> pair. The u32
# length prefixes and the BSP geometry are non-printable, so a printable-run
# scan cleanly recovers the token stream.
#
# Per RCS_VictimRescue we emit: name (A_Name), type (D_Type), heading
# (G_Heading), id (trailing %name%), and spots = [ location, *ZZ_location of
# each RCS_VictimAltSpot whose A_Victim == id, in file order ]. Index 0 is the
# editor origin; 1..N are the alt-spot markers in file order -- the ordering
# the runtime tool's `spot` index assumes. Cross-checked against a capture log.
#
# Output: tools\victims_reference.json (REFERENCE ONLY -- the runtime config
# stays index-only; fdny_spawns.py reads live coords from game memory).

param(
    [string]$MapsDir = "C:\fdny-decomp\FDNY SP 1.1\Data\Maps",
    [string]$LogPath = "",
    [string]$OutPath = "C:\fdny-decomp\tools\victims_reference.json"
)

$KNOWN_KEYS = @{}
foreach ($k in @('A_Name','classname','D_Type','G_Heading','J_Radius','location',
    'spawn_message','%name%','A_Victim','B_Heading','ZZ_location','ZZ_spawn_message',
    'F_FireType','G_Reach','I_Ambush')) { $KNOWN_KEYS[$k] = $true }
for ($i = 1; $i -le 10; $i++) { $KNOWN_KEYS[("S_Script{0:00}" -f $i)] = $true }

function Parse-Vec3([string]$s) {
    if (-not $s) { return $null }
    $p = ($s -replace ',', ' ') -split '\s+' | Where-Object { $_ -ne '' }
    if ($p.Count -lt 3) { return $null }
    try { return @([double]$p[0], [double]$p[1], [double]$p[2]) } catch { return $null }
}

function Parse-Map([string]$path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $s = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)   # latin-1

    # The entity lump is one contiguous region of [u32 len][bytes][NUL] tokens.
    # Bound it, then parse the real framing (so single-char values survive).
    $lo = $s.IndexOf('classname'); if ($lo -lt 0) { return @() }
    $lo = [Math]::Max(0, $lo - 64)
    $hi = $s.LastIndexOf('%name%'); if ($hi -lt 0) { return @() }
    $hi = [Math]::Min($bytes.Length, $hi + 128)

    $tokens = New-Object System.Collections.ArrayList
    $p = $lo
    while ($p + 5 -le $hi) {
        $ln = [System.BitConverter]::ToUInt32($bytes, $p)
        if ($ln -ge 1 -and $ln -le 512 -and ($p + 4 + $ln) -le $hi -and $bytes[$p + 4 + $ln - 1] -eq 0) {
            $ok = $true
            for ($q = $p + 4; $q -lt $p + 4 + $ln - 1; $q++) {
                $b = $bytes[$q]
                if ($b -lt 9 -or ($b -gt 13 -and $b -lt 32) -or $b -gt 126) { $ok = $false; break }
            }
            if ($ok) {
                $str = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes, $p + 4, $ln - 1).Trim()
                [void]$tokens.Add($str)
                $p += 4 + $ln
                continue
            }
        }
        $p++
    }

    $ents = New-Object System.Collections.ArrayList
    $cur = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $tokens.Count; $i++) {
        [void]$cur.Add($tokens[$i])
        if ($cur.Count -ge 2 -and $cur[$cur.Count - 2] -eq '%name%') {
            $id = $cur[$cur.Count - 1]
            $kvs = $cur.GetRange(0, $cur.Count - 2)
            $d = @{ '_id' = $id }
            for ($j = 0; $j -lt $kvs.Count - 1; $j++) {
                $k = $kvs[$j]
                if ($KNOWN_KEYS.ContainsKey($k) -and -not $d.ContainsKey($k)) {
                    $d[$k] = $kvs[$j + 1]; $j++
                }
            }
            [void]$ents.Add($d)
            $cur.Clear()
        }
    }

    $altByVictim = @{}
    foreach ($e in $ents) {
        if ($e['classname'] -ne 'RCS_VictimAltSpot') { continue }
        $vid = $e['A_Victim']
        $rawloc = if ($e['ZZ_location']) { $e['ZZ_location'] } else { $e['location'] }
        $loc = Parse-Vec3 $rawloc
        if ($vid -and $loc) {
            if (-not $altByVictim.ContainsKey($vid)) { $altByVictim[$vid] = New-Object System.Collections.ArrayList }
            [void]$altByVictim[$vid].Add(@{ loc = $loc; heading = $e['B_Heading']; id = $e['_id'] })
        }
    }

    $rows = New-Object System.Collections.ArrayList
    foreach ($e in $ents) {
        if ($e['classname'] -ne 'RCS_VictimRescue') { continue }
        $origin = Parse-Vec3 $e['location']
        $alts = @()
        if ($altByVictim.ContainsKey($e['_id'])) { $alts = $altByVictim[$e['_id']] }
        $spots = New-Object System.Collections.ArrayList
        if ($origin) { [void]$spots.Add($origin) }
        foreach ($a in $alts) { [void]$spots.Add($a.loc) }
        $vname = if ($e['A_Name']) { $e['A_Name'] } else { '<null>' }
        [void]$rows.Add([ordered]@{
            name    = $vname
            type    = $e['D_Type']
            heading = $e['G_Heading']
            id      = $e['_id']
            count   = $spots.Count
            spots   = $spots.ToArray()
            alt_ids = @($alts | ForEach-Object { $_.id })
        })
    }
    return $rows.ToArray()
}

# --- optional capture-log cross-check -----------------------------------------
$logOrigins = @{}
if ($LogPath -and (Test-Path $LogPath)) {
    $curMap = $null
    foreach ($line in Get-Content $LogPath) {
        $m = [regex]::Match($line, "level .*map[= ]+'?(\w+)")
        if ($m.Success) { $curMap = $m.Groups[1].Value.ToLower(); if (-not $logOrigins.ContainsKey($curMap)) { $logOrigins[$curMap] = New-Object System.Collections.ArrayList } }
        $m = [regex]::Match($line, "origin=\(([-\d.]+), *([-\d.]+), *([-\d.]+)\)")
        if ($m.Success -and $curMap) { [void]$logOrigins[$curMap].Add(@([double]$m.Groups[1].Value, [double]$m.Groups[3].Value)) }
    }
}

# --- run ---------------------------------------------------------------------
$result = [ordered]@{}
foreach ($f in (Get-ChildItem $MapsDir -Filter *.bsp | Sort-Object Name)) {
    $mapname = [System.IO.Path]::GetFileNameWithoutExtension($f.Name).ToLower()
    $rows = @(Parse-Map $f.FullName)
    $result[$mapname] = $rows
    $multi = @($rows | Where-Object { $_.count -gt 1 }).Count
    $note = ""
    if ($logOrigins.ContainsKey($mapname) -and $logOrigins[$mapname].Count -gt 0) {
        $want = $logOrigins[$mapname]
        $hits = 0
        for ($i = 0; $i -lt [Math]::Min($want.Count, $rows.Count); $i++) {
            $sp = $rows[$i].spots
            if ($sp.Count -gt 0 -and [Math]::Abs($want[$i][0] - $sp[0][0]) -lt 1 -and [Math]::Abs($want[$i][1] - $sp[0][2]) -lt 1) { $hits++ }
        }
        $note = "  [log xcheck: $hits/$($want.Count) origins match]"
    }
    "{0,-14} {1,2} victims ({2} multi-spot){3}" -f $mapname, $rows.Count, $multi, $note
}

[System.IO.File]::WriteAllText($OutPath, ($result | ConvertTo-Json -Depth 8), [System.Text.UTF8Encoding]::new($false))
"wrote $OutPath"

# --- human-readable table --------------------------------------------------
$mdPath = [System.IO.Path]::ChangeExtension($OutPath, ".md")
$md = New-Object System.Collections.ArrayList
[void]$md.Add("# FDNY victim spawn reference")
[void]$md.Add("")
[void]$md.Add("Per map, victims in ``.bsp`` spawn order (= the order ``fdny_spawns.py`` sees them).")
[void]$md.Add("Spot 0 = editor origin; spots 1..N = ``RCS_VictimAltSpot`` markers in file order.")
[void]$md.Add("``SpawnAtSpot`` picks one index uniformly at load; set ``spot`` in the config to pin it.")
[void]$md.Add("Coordinates are authored values (pre ``SnapToFloor``, which only adjusts Y).")
[void]$md.Add("")
foreach ($mp in $result.Keys) {
    $rows = $result[$mp]
    if ($rows.Count -eq 0) { [void]$md.Add("## $mp"); [void]$md.Add(""); [void]$md.Add("_No RCS_VictimRescue entities (script-spawned or non-rescue level)._"); [void]$md.Add(""); continue }
    [void]$md.Add("## $mp")
    [void]$md.Add("")
    [void]$md.Add("| # | name | type | count | spot 0 (origin) | alt spots (1..N) |")
    [void]$md.Add("|--:|------|------|------:|-----------------|------------------|")
    for ($i = 0; $i -lt $rows.Count; $i++) {
        $r = $rows[$i]
        $o = if ($r.spots.Count -gt 0) { "(" + (($r.spots[0]) -join ', ') + ")" } else { "-" }
        if ($r.count -gt 1) {
            $parts = @()
            for ($k = 1; $k -lt $r.count; $k++) { $parts += ("$k`: (" + (($r.spots[$k]) -join ', ') + ")") }
            $alt = $parts -join " &nbsp; "
        } else { $alt = "-" }
        [void]$md.Add("| $i | $($r.name) | $($r.type) | $($r.count) | $o | $alt |")
    }
    [void]$md.Add("")
}
[System.IO.File]::WriteAllText($mdPath, ($md -join "`r`n"), [System.Text.UTF8Encoding]::new($false))
"wrote $mdPath"

# --- minimal config skeleton ------------------------------------------------
# spawns.json for fdny_spawns.py: one map -> a bare list of spot indices in
# .bsp spawn order (list position = the victim's identity). 0 = editor origin,
# 1..N = alt-spot markers. null = leave that victim on the RNG pick. Which
# index is which victim: see victims_reference.md. Skeleton is all-zeros
# (every victim pinned to its editor origin) -- edit to taste.
$cfgPath = [System.IO.Path]::Combine([System.IO.Path]::GetDirectoryName($OutPath), "spawns.generated.json")
$readme = "spawns.json for fdny_spawns.py: one map = a list of spot indices in .bsp spawn order. N = force that authored spawn point (0 is the editor origin, 1..N are the alt-spot markers). null = leave that victim on the RNG pick. Which index maps to which victim: see victims_reference.md. Skeleton is all 0 (every victim at its editor origin)."
# build by hand: PS5.1 ConvertTo-Json mangles single-element / empty arrays
$cfgSb = New-Object System.Text.StringBuilder
[void]$cfgSb.AppendLine("{")
[void]$cfgSb.AppendLine('  "_README": ' + ($readme | ConvertTo-Json) + ',')
$mkeys = @($result.Keys)
for ($mi = 0; $mi -lt $mkeys.Count; $mi++) {
    $ints = @($result[$mkeys[$mi]] | ForEach-Object { 0 })
    $sep = if ($mi -lt $mkeys.Count - 1) { ',' } else { '' }
    [void]$cfgSb.AppendLine('  "' + $mkeys[$mi] + '": [' + ($ints -join ', ') + ']' + $sep)
}
[void]$cfgSb.AppendLine("}")
[System.IO.File]::WriteAllText($cfgPath, $cfgSb.ToString(), [System.Text.UTF8Encoding]::new($false))
"wrote $cfgPath"
