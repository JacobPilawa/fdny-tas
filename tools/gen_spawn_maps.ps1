# gen_spawn_maps.ps1 -- one top-down spawn-point map per level, for handing a
# route-planning report to a speedrunner.
#
# Per .bsp: one panel per floor (storeys found by clustering spawn heights),
# each showing the whole level's floor faces + walls at that height and the
# RCS_VictimRescue spots on that floor -- spot 0 (editor origin) filled "A0",
# spots 1..N (RCS_VictimAltSpot, one picked at random each load) ringed "A1"..
# Positions only: no legend, captions or scale (subway-map strictness).
#
# Output: tools\spawn_maps\<map>.svg + .png, index.html, spawns.json (coords + floor).

param(
    [string]$MapsDir = "C:\fdny-decomp\FDNY SP 1.1\Data\Maps",
    [string]$OutDir  = "C:\fdny-decomp\tools\spawn_maps"
)

$PALETTE = @('#e6194b', '#3cb44b', '#4363d8', '#f58231', '#911eb4', '#00b8c4',
    '#f032e6', '#9A6324', '#808000', '#000075', '#e08214', '#4d9221',
    '#c51b7d', '#01665e', '#8073ac')

$KEY_RE = [regex]'^[A-Za-z_%][\w%]*$'

# Line-weight hierarchy, used identically on every map. Wall tier is set by the
# face's vertical span (world units); thresholds come from the per-map height
# histogram, which clusters at <32 (trim), 32-96 (counters/furniture), >=96 (walls).
# Trim isn't drawn: at a glance it's noise.
$WALL_TIERS = @(   # drawn in this order: light under heavy
    @{ name = 'detail';    minH = 0;  draw = $false },
    @{ name = 'fixture';   minH = 32; draw = $true; stroke = '#7d7d7d'; width = 2.5 },
    @{ name = 'structure'; minH = 96; draw = $true; stroke = '#141414'; width = 5.5 })
$FRAME_STROKE = '#bdbdbd'; $FRAME_W = 2     # plot border, recedes
$BG = '#ffffff'                              # outside any floor = void / out of bounds

# Floors (walkable up-facing faces) filled so the level's shape reads instantly.
# Shade by storey: band = floor((y - lowest floor) / $STOREY); upper storeys are
# drawn over lower ones, i.e. what you'd actually see from above.
$FLOOR_MIN_NY = 0.7                     # |normal.y| >= this = walkable surface (<= ~45 deg)
$FLOOR_FILL = '#e9e2d2'

# Floors: spot heights sorted, a gap > $STOREY_GAP starts the next storey; a
# storey's floor sits ~$SPAWN_ABOVE_FLOOR below its lowest spot (measured: the
# floor face under each spawn in bank/boat_a is 48-122 below it). One panel per
# storey, showing only that storey's floor faces, walls and spots.
$STOREY_GAP = 120
$SPAWN_ABOVE_FLOOR = 72

# The whole level is drawn (it's the landmark); marks are sized for that canvas
# fit to a screen. No legend, captions or scale -- positions only.
$PLOT_LONG = 1500           # px on a panel's long side (marks below are sized against this)
$TITLE_F = 48               # panel title: MAP · FLOOR n
$R_ORIGIN = 42; $F_ORIGIN = 34          # spot 0 (default spawn): filled, "A0"
$R_ALT    = 34; $F_ALT    = 27          # spots 1..n (alternates): ring, "A1"
$ALT_RING_W = 8; $MARK_W = 4; $HALO_W = 8
$LINK_W = 7                             # line joining a victim's spots on one floor
$TICK_W = 4                             # nudged marker -> its true position

function Parse-Vec3([string]$s) {
    if (-not $s) { return $null }
    $p = ($s -replace ',', ' ') -split '\s+' | Where-Object { $_ -ne '' }
    if ($p.Count -lt 3) { return $null }
    try { return @([double]$p[0], [double]$p[1], [double]$p[2]) } catch { return $null }
}

function Get-Entities([string]$path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $enc = [System.Text.Encoding]::GetEncoding(28591)
    $s = $enc.GetString($bytes)
    $lo = $s.IndexOf('classname'); if ($lo -lt 0) { return @() }
    $lo = [Math]::Max(0, $lo - 64)
    $hi = $s.LastIndexOf('%name%'); if ($hi -lt 0) { return @() }
    $hi = [Math]::Min($bytes.Length, $hi + 128)

    $tok = New-Object System.Collections.ArrayList
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
                [void]$tok.Add($enc.GetString($bytes, $p + 4, $ln - 1).Trim())
                $p += 4 + $ln
                continue
            }
        }
        $p++
    }

    $ents = New-Object System.Collections.ArrayList
    $cur = New-Object System.Collections.ArrayList
    foreach ($t in $tok) {
        [void]$cur.Add($t)
        if ($cur.Count -ge 2 -and $cur[$cur.Count - 2] -eq '%name%') {
            $d = @{ '_id' = $cur[$cur.Count - 1] }
            if ($cur.Count -gt 2) {
                $kvs = $cur.GetRange(0, $cur.Count - 2)
                for ($j = 0; $j -lt $kvs.Count - 1; $j++) {
                    $k = $kvs[$j]
                    if ($KEY_RE.IsMatch($k) -and -not $d.ContainsKey($k)) { $d[$k] = $kvs[$j + 1]; $j++ }
                }
            }
            [void]$ents.Add($d)
            $cur.Clear()
        }
    }
    return $ents
}

function Get-Pos($d) {
    foreach ($k in 'location', 'origin', 'ZZ_location') {
        if ($d.ContainsKey($k)) { $v = Parse-Vec3 $d[$k]; if ($v) { return $v } }
    }
    return $null
}

# Pull the level's wall geometry from the GBSP lumps: VERTS (type 14, vec3),
# VERT_INDEX (13, int32), FACES (11, 36 bytes -> [0]=firstVert [1]=numVerts).
# Walls: faces whose normal is near-horizontal, projected to one X/Z segment,
# de-duped -> walls = @( @(x1,z1,x2,z2,height) ... ).
# Floors: walkable upward surfaces as X/Z polygons -> floors = @( @{y; pts} ... ).
function Get-Geometry([string]$path) {
    $b = [System.IO.File]::ReadAllBytes($path)
    $off = @{}
    $p = 0
    for ($i = 0; $i -lt 60 -and $p + 12 -le $b.Length; $i++) {
        $t = [BitConverter]::ToInt32($b, $p); $s = [BitConverter]::ToInt32($b, $p + 4); $e = [BitConverter]::ToInt32($b, $p + 8)
        if ($t -eq 65535) { break }
        if ($s -lt 0 -or $e -lt 0 -or [int64]$s * $e -gt $b.Length) { break }
        if ($t -in 11, 13, 14) { $off[$t] = @(($p + 12), $e) }
        $p += 12 + [int64]$s * $e
    }
    if (-not ($off.ContainsKey(11) -and $off.ContainsKey(13) -and $off.ContainsKey(14))) { return @{ walls = @(); floors = @() } }

    $nV = $off[14][1]; $vxyz = New-Object 'single[]' ($nV * 3)
    [Buffer]::BlockCopy($b, $off[14][0], $vxyz, 0, $nV * 12)
    $nI = $off[13][1]; $vidx = New-Object 'int32[]' $nI
    [Buffer]::BlockCopy($b, $off[13][0], $vidx, 0, $nI * 4)
    $nF = $off[11][1]; $fbase = $off[11][0]

    $segs = New-Object System.Collections.Generic.List[double[]]
    $floors = New-Object System.Collections.Generic.List[hashtable]
    $seen =New-Object 'System.Collections.Generic.Dictionary[string,int]'
    for ($fi = 0; $fi -lt $nF; $fi++) {
        $fo = $fbase + $fi * 36
        $first = [BitConverter]::ToInt32($b, $fo)
        $nv = [BitConverter]::ToInt32($b, $fo + 4)
        if ($nv -lt 3 -or $nv -gt 64 -or $first -lt 0 -or $first + $nv -gt $nI) { continue }
        # Newell normal
        $nx = 0.0; $ny = 0.0; $nz = 0.0
        for ($k = 0; $k -lt $nv; $k++) {
            $ai = $vidx[$first + $k] * 3; $ci = $vidx[$first + (($k + 1) % $nv)] * 3
            $ax = $vxyz[$ai]; $ay = $vxyz[$ai + 1]; $az = $vxyz[$ai + 2]
            $cx = $vxyz[$ci]; $cy = $vxyz[$ci + 1]; $cz = $vxyz[$ci + 2]
            $nx += ($ay - $cy) * ($az + $cz)
            $ny += ($az - $cz) * ($ax + $cx)
            $nz += ($ax - $cx) * ($ay + $cy)
        }
        $ln = [Math]::Sqrt($nx * $nx + $ny * $ny + $nz * $nz)
        if ($ln -le 0) { continue }
        # GBSP winds faces so a walkable floor's Newell normal points DOWN (-Y);
        # ceilings point up. Verified: the face directly under every spawn in
        # bank + boat_a has ny<0, the one above has ny>0.
        if ($ny / $ln -le -$FLOOR_MIN_NY) {
            $pts = New-Object 'double[]' ($nv * 2); $ys = 0.0
            for ($k = 0; $k -lt $nv; $k++) { $q = $vidx[$first + $k] * 3; $pts[2 * $k] = $vxyz[$q]; $pts[2 * $k + 1] = $vxyz[$q + 2]; $ys += $vxyz[$q + 1] }
            $floors.Add(@{ y = $ys / $nv; pts = $pts })
            continue
        }
        if ([Math]::Abs($ny / $ln) -ge 0.30) { continue }   # ceiling / steep slope -> skip
        # one clean segment per wall face: the two verts farthest apart in X/Z
        $bx1 = 0.0; $bz1 = 0.0; $bx2 = 0.0; $bz2 = 0.0; $bd = -1.0
        for ($a = 0; $a -lt $nv; $a++) {
            $pa = $vidx[$first + $a] * 3; $pax = $vxyz[$pa]; $paz = $vxyz[$pa + 2]
            for ($c = $a + 1; $c -lt $nv; $c++) {
                $pc = $vidx[$first + $c] * 3
                $dx = $vxyz[$pc] - $pax; $dz = $vxyz[$pc + 2] - $paz
                $dd = $dx * $dx + $dz * $dz
                if ($dd -gt $bd) { $bd = $dd; $bx1 = $pax; $bz1 = $paz; $bx2 = $vxyz[$pc]; $bz2 = $vxyz[$pc + 2] }
            }
        }
        if ($bd -lt 64) { continue }   # < 8 units -> trim / rivet detail, skip
        $rx1 = [Math]::Round($bx1 / 3) * 3; $rz1 = [Math]::Round($bz1 / 3) * 3
        $rx2 = [Math]::Round($bx2 / 3) * 3; $rz2 = [Math]::Round($bz2 / 3) * 3
        $key = if ($rx1 -lt $rx2 -or ($rx1 -eq $rx2 -and $rz1 -le $rz2)) { "$rx1,$rz1,$rx2,$rz2" } else { "$rx2,$rz2,$rx1,$rz1" }
        # vertical span of the face -> drives line weight (tall = structural wall)
        $y0 = [double]::MaxValue; $y1 = [double]::MinValue
        for ($k = 0; $k -lt $nv; $k++) { $yy = $vxyz[$vidx[$first + $k] * 3 + 1]; if ($yy -lt $y0) { $y0 = $yy }; if ($yy -gt $y1) { $y1 = $yy } }
        $hgt = $y1 - $y0
        $key += "," + [Math]::Floor($y0 / 64)      # same X/Z edge on another storey stays separate
        if ($seen.ContainsKey($key)) {
            # merge faces on a shared edge: union their vertical extent
            $prev = $segs[$seen[$key]]
            if ($y0 -lt $prev[5]) { $prev[5] = $y0 }; if ($y1 -gt $prev[6]) { $prev[6] = $y1 }
            $prev[4] = $prev[6] - $prev[5]
        } else {
            $seen[$key] = $segs.Count; $segs.Add(@($bx1, $bz1, $bx2, $bz2, $hgt, $y0, $y1))
        }
    }
    return @{ walls = $segs; floors = $floors }
}

function N($v) { return ("{0:0.##}" -f [double]$v) }
function Esc([string]$t) { return ($t -replace '&', '&amp;' -replace '<', '&lt;' -replace '>', '&gt;') }

# --------------------------------------------------------------------------- #
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$jsonAll = [ordered]@{}
$svgList = New-Object System.Collections.ArrayList

foreach ($f in (Get-ChildItem $MapsDir -Filter *.bsp | Sort-Object Name)) {
    $map = [System.IO.Path]::GetFileNameWithoutExtension($f.Name).ToLower()
    $ents = Get-Entities $f.FullName

    $victims = @($ents | Where-Object { $_['classname'] -eq 'RCS_VictimRescue' })
    if ($victims.Count -eq 0) { Write-Host ("{0,-12} -- no victims, skipped" -f $map); continue }

    $altByV = @{}
    foreach ($e in $ents) {
        if ($e['classname'] -ne 'RCS_VictimAltSpot') { continue }
        $vid = $e['A_Victim']; $loc = Get-Pos $e
        if ($vid -and $loc) {
            if (-not $altByV.ContainsKey($vid)) { $altByV[$vid] = New-Object System.Collections.ArrayList }
            [void]$altByV[$vid].Add($loc)
        }
    }

    # victim records: name, type, ordered spot list
    $vrecs = New-Object System.Collections.ArrayList
    foreach ($v in $victims) {
        $o = Get-Pos $v
        $spots = New-Object System.Collections.ArrayList
        if ($o) { [void]$spots.Add($o) }
        if ($altByV.ContainsKey($v['_id'])) { foreach ($a in $altByV[$v['_id']]) { [void]$spots.Add($a) } }
        $nm = if ($v['A_Name']) { $v['A_Name'] } else { '<null>' }
        [void]$vrecs.Add(@{ name = $nm; type = $v['D_Type']; spots = $spots })
    }

    # level geometry (top-down)
    $geo = Get-Geometry $f.FullName
    $walls = $geo.walls; $floors = $geo.floors

    # ---- storeys, from spawn heights: sorted, a gap > $STOREY_GAP starts a new floor ----
    $vy = @(); foreach ($vr in $vrecs) { foreach ($sp in $vr.spots) { $vy += $sp[1] } }
    $ys = @($vy | Sort-Object)
    $clusters = New-Object System.Collections.ArrayList; $lo = $ys[0]; $prev = $ys[0]
    foreach ($y in $ys) { if ($y - $prev -gt $STOREY_GAP) { [void]$clusters.Add(@($lo, $prev)); $lo = $y }; $prev = $y }
    [void]$clusters.Add(@($lo, $prev))
    $nS = $clusters.Count
    function Get-Storey($y) { for ($q = 0; $q -lt $nS; $q++) { if ($y -le $clusters[$q][1]) { return $q } }; return $nS - 1 }
    # each storey's floor level, and the height slab whose floor faces belong to it
    $level = @(); foreach ($c in $clusters) { $level += ($c[0] - $SPAWN_ABOVE_FLOOR) }
    $slab = @(); for ($q = 0; $q -lt $nS; $q++) {
        $sLo = $level[$q] - 48
        $sHi = if ($q + 1 -lt $nS) { $level[$q + 1] - 16 } else { $level[$q] + 96 }
        $slab += , @($sLo, $sHi)
    }

    # ---- per-storey geometry: floor faces in the slab, walls standing on that floor ----
    $pFloors = @(); $pWalls = @()
    for ($q = 0; $q -lt $nS; $q++) {
        $fl = New-Object System.Collections.ArrayList; $wl = New-Object System.Collections.ArrayList
        foreach ($x in $floors) { if ($x.y -ge $slab[$q][0] -and $x.y -lt $slab[$q][1]) { [void]$fl.Add($x) } }
        $wLo = $level[$q] + 8; $wHi = $level[$q] + 96
        foreach ($s in $walls) { if ($s[6] -gt $wLo -and $s[5] -lt $wHi) { [void]$wl.Add($s) } }
        $pFloors += , $fl; $pWalls += , $wl
    }

    # ---- one frame shared by every panel: all drawn geometry (trimmed) + all spots ----
    function Pctl($list, $p) {
        $s = [double[]]$list.ToArray(); [Array]::Sort($s); if ($s.Count -eq 0) { return $null }
        return $s[[Math]::Max(0, [Math]::Min($s.Count - 1, [int][Math]::Round(($s.Count - 1) * $p)))]
    }
    $gx = New-Object System.Collections.Generic.List[double]; $gz = New-Object System.Collections.Generic.List[double]
    for ($q = 0; $q -lt $nS; $q++) {
        foreach ($s in $pWalls[$q]) { $gx.Add($s[0]); $gx.Add($s[2]); $gz.Add($s[1]); $gz.Add($s[3]) }
        foreach ($x in $pFloors[$q]) { for ($k = 0; $k -lt $x.pts.Count; $k += 2) { $gx.Add($x.pts[$k]); $gz.Add($x.pts[$k + 1]) } }
    }
    $vx = @(); $vz = @(); foreach ($vr in $vrecs) { foreach ($sp in $vr.spots) { $vx += $sp[0]; $vz += $sp[2] } }
    $xmin = ($vx | Measure-Object -Minimum).Minimum; $xmax = ($vx | Measure-Object -Maximum).Maximum
    $zmin = ($vz | Measure-Object -Minimum).Minimum; $zmax = ($vz | Measure-Object -Maximum).Maximum
    if ($gx.Count -gt 0) {
        $xmin = [Math]::Min($xmin, (Pctl $gx 0.002)); $xmax = [Math]::Max($xmax, (Pctl $gx 0.998))
        $zmin = [Math]::Min($zmin, (Pctl $gz 0.002)); $zmax = [Math]::Max($zmax, (Pctl $gz 0.998))
    }
    $padx = ($xmax - $xmin) * 0.02 + 48; $padz = ($zmax - $zmin) * 0.02 + 48
    $xmin -= $padx; $xmax += $padx; $zmin -= $padz; $zmax += $padz
    $xr = $xmax - $xmin; $zr = $zmax - $zmin
    $ppu = $PLOT_LONG / [Math]::Max($xr, $zr)
    $plotW = $xr * $ppu; $plotH = $zr * $ppu

    # panels in a grid, floor 1 first: up to 4 across for a tall map, 2 for a wide one
    $cols = if ($zr -ge $xr) { [Math]::Min($nS, 4) } else { [Math]::Min($nS, 2) }
    $rows = [int][Math]::Ceiling($nS / $cols)
    $M = 40; $HEAD = 90; $GAP = 70
    $cellW = $plotW; $cellH = $HEAD + $plotH
    $svgW = 2 * $M + $cols * $cellW + ($cols - 1) * $GAP
    $svgH = 2 * $M + $rows * $cellH + ($rows - 1) * $GAP

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("<svg xmlns='http://www.w3.org/2000/svg' width='$([int]$svgW)' height='$([int]$svgH)' font-family='Segoe UI, Arial, sans-serif'>")
    [void]$sb.AppendLine("<rect width='100%' height='100%' fill='$BG'/>")

    $jv = New-Object System.Collections.ArrayList
    for ($q = 0; $q -lt $nS; $q++) {
        $ox = $M + ($q % $cols) * ($cellW + $GAP)
        $oy = $M + [Math]::Floor($q / $cols) * ($cellH + $GAP) + $HEAD
        function PX($x) { return ($ox + ($x - $xmin) / $xr * $plotW) }
        # Genesis3D is right-handed, Y up: looking DOWN (-Y) with X right puts +Z
        # toward the bottom of the screen. (Z-up = viewed from below, mirrored.)
        function PY($z) { return ($oy + ($z - $zmin) / $zr * $plotH) }

        $title = $map.ToUpper(); if ($nS -gt 1) { $title += " &#183; FLOOR $($q + 1)" }
        [void]$sb.AppendLine("<text x='$(N $ox)' y='$(N ($oy - 26))' font-size='$TITLE_F' font-weight='800' fill='#111'>$title</text>")
        [void]$sb.AppendLine("<clipPath id='$map-p$q'><rect x='$(N $ox)' y='$(N $oy)' width='$(N $plotW)' height='$(N $plotH)'/></clipPath>")

        # floors (one flat tint), then walls by tier
        [void]$sb.AppendLine("<g clip-path='url(#$map-p$q)' fill='$FLOOR_FILL' stroke='$FLOOR_FILL' stroke-width='1' stroke-linejoin='round'>")
        foreach ($x in $pFloors[$q]) {
            $d = New-Object System.Text.StringBuilder
            for ($k = 0; $k -lt $x.pts.Count; $k += 2) { [void]$d.Append($(if ($k -eq 0) { 'M' } else { 'L' })).Append((N (PX $x.pts[$k]))).Append(' ').Append((N (PY $x.pts[$k + 1]))).Append(' ') }
            [void]$sb.AppendLine("<path d='$($d.ToString())Z'/>")
        }
        [void]$sb.AppendLine("</g>")
        for ($t = 0; $t -lt $WALL_TIERS.Count; $t++) {
            $tier = $WALL_TIERS[$t]; if (-not $tier.draw) { continue }
            $maxH = if ($t + 1 -lt $WALL_TIERS.Count) { $WALL_TIERS[$t + 1].minH } else { [double]::MaxValue }
            [void]$sb.AppendLine("<g clip-path='url(#$map-p$q)' stroke='$($tier.stroke)' stroke-width='$($tier.width)' stroke-linecap='round'>")
            foreach ($s in $pWalls[$q]) {
                if ($s[4] -lt $tier.minH -or $s[4] -ge $maxH) { continue }
                if (($s[0] -lt $xmin -and $s[2] -lt $xmin) -or ($s[0] -gt $xmax -and $s[2] -gt $xmax) -or
                    ($s[1] -lt $zmin -and $s[3] -lt $zmin) -or ($s[1] -gt $zmax -and $s[3] -gt $zmax)) { continue }
                [void]$sb.AppendLine("<line x1='$(N (PX $s[0]))' y1='$(N (PY $s[1]))' x2='$(N (PX $s[2]))' y2='$(N (PY $s[3]))'/>")
            }
            [void]$sb.AppendLine("</g>")
        }
        [void]$sb.AppendLine("<rect x='$(N $ox)' y='$(N $oy)' width='$(N $plotW)' height='$(N $plotH)' fill='none' stroke='$FRAME_STROKE' stroke-width='$FRAME_W'/>")

        # this storey's spots; nudge markers apart so none hides another
        $here = New-Object System.Collections.ArrayList
        for ($i = 0; $i -lt $vrecs.Count; $i++) {
            for ($k = 0; $k -lt $vrecs[$i].spots.Count; $k++) {
                if ((Get-Storey $vrecs[$i].spots[$k][1]) -eq $q) { [void]$here.Add(@($i, $k)) }
            }
        }
        $pos = @{}; $placed = New-Object System.Collections.ArrayList
        foreach ($h in $here) {
            $i = $h[0]; $k = $h[1]; $sp = $vrecs[$i].spots[$k]
            $r = if ($k -eq 0) { $R_ORIGIN } else { $R_ALT }
            $tx = PX $sp[0]; $ty = PY $sp[2]; $best = $null
            for ($ring = 0; $ring -le 8 -and $null -eq $best; $ring++) {
                $nA = if ($ring -eq 0) { 1 } else { 12 }
                for ($a = 0; $a -lt $nA; $a++) {
                    $th = $a * [Math]::PI / 6
                    $cx = $tx + $ring * $r * 0.7 * [Math]::Cos($th); $cy = $ty + $ring * $r * 0.7 * [Math]::Sin($th)
                    $ok = $true
                    foreach ($p in $placed) { $dx = $p[0] - $cx; $dy = $p[1] - $cy; if ([Math]::Sqrt($dx * $dx + $dy * $dy) -lt $p[2] + $r + 3) { $ok = $false; break } }
                    if ($ok) { $best = @($cx, $cy); break }
                }
            }
            if ($null -eq $best) { $best = @($tx, $ty) }
            [void]$placed.Add(@($best[0], $best[1], $r))
            $pos["$i,$k"] = @($best[0], $best[1], $r, $tx, $ty)
        }

        # true-spot ticks for nudged markers, then links (same-floor spots of one victim)
        foreach ($h in $here) {
            $p = $pos["$($h[0]),$($h[1])"]; $dx = $p[0] - $p[3]; $dy = $p[1] - $p[4]
            if ([Math]::Sqrt($dx * $dx + $dy * $dy) -gt 2) {
                [void]$sb.AppendLine("<line x1='$(N $p[3])' y1='$(N $p[4])' x2='$(N $p[0])' y2='$(N $p[1])' stroke='#111' stroke-width='$TICK_W'/>")
                [void]$sb.AppendLine("<circle cx='$(N $p[3])' cy='$(N $p[4])' r='$(N (2 * $TICK_W))' fill='#111'/>")
            }
        }
        for ($i = 0; $i -lt $vrecs.Count; $i++) {
            $col = $PALETTE[$i % $PALETTE.Count]
            # (unary comma: keep each @(x,y,...) whole instead of letting the pipeline flatten it)
            $mine = @($here | Where-Object { $_[0] -eq $i } | ForEach-Object { , $pos["$($_[0]),$($_[1])"] })
            for ($n = 1; $n -lt $mine.Count; $n++) {      # (not $m: PowerShell names are case-insensitive, $m IS $M)
                $a = $mine[0]; $p = $mine[$n]
                [void]$sb.AppendLine("<line x1='$(N $a[0])' y1='$(N $a[1])' x2='$(N $p[0])' y2='$(N $p[1])' stroke='#fff' stroke-width='$($LINK_W + $HALO_W)' stroke-linecap='round'/>")
                [void]$sb.AppendLine("<line x1='$(N $a[0])' y1='$(N $a[1])' x2='$(N $p[0])' y2='$(N $p[1])' stroke='$col' stroke-width='$LINK_W' stroke-linecap='round'/>")
            }
        }
        # markers: spot 0 filled, alternates ringed; label = letter + spot index
        foreach ($h in $here) {
            $i = $h[0]; $k = $h[1]; $p = $pos["$i,$k"]; $sp = $vrecs[$i].spots[$k]
            $col = $PALETTE[$i % $PALETTE.Count]; $L = [char](65 + ($i % 26))
            $tip = Esc ("$L$k  $($vrecs[$i].name) ($($vrecs[$i].type)): ($(N $sp[0]), $(N $sp[1]), $(N $sp[2]))")
            [void]$sb.AppendLine("<circle cx='$(N $p[0])' cy='$(N $p[1])' r='$(N ($p[2] + $HALO_W / 2))' fill='#fff'/>")
            if ($k -eq 0) {
                [void]$sb.AppendLine("<circle cx='$(N $p[0])' cy='$(N $p[1])' r='$(N $p[2])' fill='$col' stroke='#111' stroke-width='$MARK_W'><title>$tip</title></circle>")
                [void]$sb.AppendLine("<text x='$(N $p[0])' y='$(N ($p[1] + $F_ORIGIN * 0.36))' font-size='$F_ORIGIN' font-weight='800' text-anchor='middle' fill='#fff' stroke='#111' stroke-width='2' paint-order='stroke'>$L$k</text>")
            } else {
                [void]$sb.AppendLine("<circle cx='$(N $p[0])' cy='$(N $p[1])' r='$(N $p[2])' fill='#fff' stroke='$col' stroke-width='$ALT_RING_W'><title>$tip</title></circle>")
                [void]$sb.AppendLine("<text x='$(N $p[0])' y='$(N ($p[1] + $F_ALT * 0.36))' font-size='$F_ALT' font-weight='800' text-anchor='middle' fill='#111'>$L$k</text>")
            }
        }
    }

    # json (coordinates live here, not on the map)
    for ($i = 0; $i -lt $vrecs.Count; $i++) {
        $vr = $vrecs[$i]; $L = [char](65 + ($i % 26))
        $jspots = New-Object System.Collections.ArrayList
        for ($k = 0; $k -lt $vr.spots.Count; $k++) {
            [void]$jspots.Add([ordered]@{ spot = $k; label = "$L$k"; floor = (Get-Storey $vr.spots[$k][1]) + 1; x = [Math]::Round($vr.spots[$k][0], 2); y = [Math]::Round($vr.spots[$k][1], 2); z = [Math]::Round($vr.spots[$k][2], 2) })
        }
        [void]$jv.Add([ordered]@{ label = "$L"; index = $i; name = $vr.name; type = $vr.type; n_spots = $vr.spots.Count; spots = $jspots })
    }

    [void]$sb.AppendLine("</svg>")
    $outSvg = Join-Path $OutDir "$map.svg"
    [System.IO.File]::WriteAllText($outSvg, $sb.ToString(), [System.Text.UTF8Encoding]::new($false))
    [void]$svgList.Add(@{ map = $map; n = $vrecs.Count; svg = $sb.ToString() })

    $jsonAll[$map] = [ordered]@{
        bounds  = [ordered]@{ x = @([Math]::Round($xmin, 1), [Math]::Round($xmax, 1)); z = @([Math]::Round($zmin, 1), [Math]::Round($zmax, 1)) }
        floors  = $nS
        victims = $jv
    }
    Write-Host ("{0,-12} {1,2} victims, {2} floor(s) -> {3}" -f $map, $vrecs.Count, $nS, (Split-Path $outSvg -Leaf))
}

[System.IO.File]::WriteAllText((Join-Path $OutDir "spawns.json"), ($jsonAll | ConvertTo-Json -Depth 9), [System.Text.UTF8Encoding]::new($false))
Write-Host "`nwrote $(Join-Path $OutDir 'spawns.json')"

# contact-sheet report (all maps inline, for handing off)
$h = New-Object System.Text.StringBuilder
[void]$h.AppendLine("<!doctype html><html><head><meta charset='utf-8'><title>FDNY victim spawn maps</title>")
[void]$h.AppendLine("<style>body{font:14px/1.5 'Segoe UI',Arial,sans-serif;margin:24px;color:#111;background:#fafafa}h1{margin:0 0 4px}p.sub{color:#666;margin:0 0 18px}nav{margin:0 0 24px}nav a{display:inline-block;margin:0 10px 6px 0;padding:3px 9px;background:#eee;border-radius:4px;text-decoration:none;color:#333}section{background:#fff;border:1px solid #e2e2e2;border-radius:8px;padding:14px;margin:0 0 22px}svg{max-width:100%;height:auto;border:1px solid #eee}</style></head><body>")
[void]$h.AppendLine("<h1>F.D.N.Y. Firefighter &#8212; victim spawn maps</h1>")
[void]$h.Append("<nav>")
foreach ($it in $svgList) { [void]$h.Append("<a href='#$($it.map)'>$($it.map) ($($it.n))</a>") }
[void]$h.AppendLine("</nav>")
foreach ($it in $svgList) {
    [void]$h.AppendLine("<section id='$($it.map)'>")
    [void]$h.AppendLine(($it.svg -replace "<\?xml[^>]*\?>", ""))
    [void]$h.AppendLine("</section>")
}
[void]$h.AppendLine("</body></html>")
[System.IO.File]::WriteAllText((Join-Path $OutDir "index.html"), $h.ToString(), [System.Text.UTF8Encoding]::new($false))
Write-Host "wrote $(Join-Path $OutDir 'index.html')"

# rasterise each SVG to PNG via headless Edge/Chrome, if one is installed
$browser = @(
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($browser) {
    foreach ($svg in (Get-ChildItem $OutDir -Filter *.svg)) {
        [xml]$x = Get-Content $svg.FullName -Raw
        $w = [int]$x.svg.width; $ht = [int]$x.svg.height
        $png = [System.IO.Path]::ChangeExtension($svg.FullName, ".png")
        & $browser --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1.5 `
            "--screenshot=$png" "--window-size=$w,$ht" ("file:///" + ($svg.FullName -replace '\\', '/')) 2>&1 | Out-Null
    }
    Start-Sleep 1
    Write-Host "wrote $((Get-ChildItem $OutDir -Filter *.png).Count) PNGs"
} else {
    Write-Host "no Edge/Chrome found -- SVGs only (open in a browser)"
}
