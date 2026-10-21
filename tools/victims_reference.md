# FDNY victim spawn reference

Per map, victims in `.bsp` spawn order (= the order `fdny_spawns.py` sees them).
Spot 0 = editor origin; spots 1..N = `RCS_VictimAltSpot` markers in file order.
`SpawnAtSpot` picks one index uniformly at load; set `spot` in the config to pin it.
Coordinates are authored values (pre `SnapToFloor`, which only adjusts Y).

## bank

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | betty | girl | 2 | (1176, 0, -672) | 1: (1192, 1, -772) |
| 1 | <null> | woman | 1 | (-331, 0, -287) | - |
| 2 | <null> | girl | 2 | (735, 0, -2184) | 1: (-511, 172, -578) |
| 3 | <null> | boy | 2 | (-768, 0, -2160) | 1: (-2423, -6, -1914) |
| 4 | <null> | man | 2 | (1128, 284, 684) | 1: (1256, 312, -1312) |
| 5 | <null> | fatman | 1 | (-2213, 314, 699) | - |

## bar_r

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | Victim01 | fatman | 1 | (1980, 420, -80) | - |
| 1 | Victim02 | woman | 2 | (96, 40, -1700) | 1: (424, 92, -1720) |
| 2 | Victim03 | man | 1 | (2164, 304, -176) | - |
| 3 | Victim04 | man | 1 | (516, 304, -1756) | - |
| 4 | Victim05 | fatman | 3 | (2104, 40, -1368) | 1: (1736, 96, -720) &nbsp; 2: (2160, 140, -1064) |
| 5 | Victim06 | woman | 2 | (1792, 40, -324) | 1: (84, 92, -536) |

## boat_a

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | victimfork | boy | 2 | (698, -293, 2393) | 1: (-127, -765, 2628) |
| 1 | girl | girl | 2 | (-1551, -72, 456) | 1: (153, -44, 463) |
| 2 | victim3 | girl | 2 | (1359, 105, 6149) | 1: (1159, -63, 5502) |
| 3 | boy2 | boy | 2 | (308, -1018, 3784) | 1: (1274, -963, 4082) |
| 4 | fatty | fatman | 1 | (119, -115, 6541) | - |
| 5 | fatman | fatman | 2 | (551, -280, 1632) | 1: (529, -260, 1507) |
| 6 | fatman1 | fatman | 2 | (451, -52, 1344) | 1: (-579, -31, 784) |
| 7 | fat | fatman | 2 | (1056, -730, 4532) | 1: (1468, -994, 3346) |
| 8 | girl | girl | 1 | (-12, -1042, 4501) | - |

## boat_b

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | victim3 | girl | 2 | (-271, -788, 6405) | 1: (-1625, -943, 6563) |
| 1 | boy2 | boy | 2 | (106, -551, 3794) | 1: (414, -126, 3727) |
| 2 | fatty | fatman | 2 | (-1151, -578, 3462) | 1: (-1545, -126, 3207) |
| 3 | <null> | man | 2 | (-783, -291, 4517) | 1: (-1001, -325, 2889) |
| 4 | erg | woman | 2 | (-910, -843, 7760) | 1: (-254, -616, 7447) |

## city_h

_No RCS_VictimRescue entities (script-spawned or non-rescue level)._

## const_h

_No RCS_VictimRescue entities (script-spawned or non-rescue level)._

## dock_h

_No RCS_VictimRescue entities (script-spawned or non-rescue level)._

## gas_r

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | Victim1 | man | 1 | (1404, -172, 24) | - |
| 1 | Victim2 | woman | 1 | (1576, -80, 512) | - |
| 2 | Victim3 | man | 1 | (756, -64, 648) | - |
| 3 | Victim4 | man | 1 | (1269, -172, 1404) | - |
| 4 | Victim6 | woman | 1 | (1988, -172, 480) | - |
| 5 | Victim7 | woman | 1 | (1972, -172, -204) | - |
| 6 | Victim5 | man | 1 | (-404, -120, 1164) | - |

## mbase

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | victim1 | man | 1 | (348, -136, -3068) | - |
| 1 | victim3 | man | 1 | (1796, -540, -3284) | - |
| 2 | victim4 | man | 1 | (840, -260, -4812) | - |
| 3 | victim5 | man | 1 | (1852, -112, -5732) | - |
| 4 | victim10 | man | 1 | (292, -320, -1320) | - |

## oil_rig_a

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | <null> | woman | 2 | (-236, 590, -262) | 1: (-1076, 612, -260) |
| 1 | <null>17 | girl | 2 | (-228, 590, -508) | 1: (-600, 612, -852) |
| 2 | <null>18 | man | 3 | (-742, 608, -762) | 1: (-744, 612, -648) &nbsp; 2: (-746, 612, -874) |

## oil_rig_b

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | <null>2 | man | 1 | (1144, 496, 628) | - |
| 1 | <null>3 | dog | 2 | (876, 832, 416) | 1: (802, 776, 1046) |
| 2 | woman | woman | 1 | (1080, 288, 1032) | - |

## oil_rig_c

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | <null>4 | fatman | 3 | (857, 1345, -320) | 1: (837, 1072, -296) &nbsp; 2: (845, 1584, -344) |
| 1 | <null>5 | man | 3 | (1073, 1904, -364) | 1: (885, 2112, -384) &nbsp; 2: (885, 2392, -416) |
| 2 | fatman behind rubble | fatman | 1 | (1076, -256, 496) | - |
| 3 | man on ledge1 | man | 3 | (1048, 312, -120) | 1: (624, 312, -1040) &nbsp; 2: (456, 312, -104) |
| 4 | woman at console | woman | 1 | (926, 40, -864) | - |
| 5 | other man in A1 | man | 1 | (508, 60, -1418) | - |

## oil_rig_d

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | man | man | 3 | (1072, 864, -1464) | 1: (-130, 856, -1472) &nbsp; 2: (-880, 856, -1472) |
| 1 | man1 | man | 3 | (1072, 304, -1464) | 1: (338, 312, -1470) &nbsp; 2: (-584, 312, -1480) |
| 2 | woman | woman | 1 | (-128, 584, -1608) | - |

## oil_rig_h

_No RCS_VictimRescue entities (script-spawned or non-rescue level)._

## subway_a

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | Victim1 | fatman | 3 | (1180, -532, -1452) | 1: (1844, -532, -804) &nbsp; 2: (1766, -462, 153) |
| 1 | Victim2 | man | 1 | (-1777, 0, 1549) | - |
| 2 | Victim3 | woman | 1 | (2100, -516, -864) | - |
| 3 | Victim4 | woman | 1 | (2302, -441, 105) | - |
| 4 | Victim5 | man | 1 | (-852, 12, 408) | - |
| 5 | Victim7 | man | 1 | (2438, -412, -3175) | - |

## subway_b

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | Victim1 | fatman | 1 | (-2668, -117, 868) | - |
| 1 | Victim2 | fatman | 2 | (-1376, -100, 3392) | 1: (-1857, -53, 4118) |
| 2 | Victim4 | dog | 2 | (-68, 0, -3804) | 1: (584, -53, -5389) |
| 3 | Victim5 | boy1 | 2 | (541, 66, 4070) | 1: (178, -53, 3213) |
| 4 | Victim6 | woman | 2 | (38, 0, 1804) | 1: (583, -53, 1868) |
| 5 | Victim7 | woman | 1 | (-1, -53, 4082) | - |
| 6 | Victim9 | woman | 1 | (-2014, -82, -605) | - |
| 7 | Victim10 | man | 1 | (-742, -53, -5269) | - |
| 8 | Victim11 | woman | 1 | (-211, -53, -4266) | - |
| 9 | Victim12 | man | 1 | (542, 37, -4819) | - |

## training

| # | name | type | count | spot 0 (origin) | alt spots (1..N) |
|--:|------|------|------:|-----------------|------------------|
| 0 | victim1 | man | 3 | (1104, -60, -4976) | 1: (1300, -120, -4952) &nbsp; 2: (1472, -120, -4972) |
| 1 | victim2 | man | 3 | (1656, -120, -4968) | 1: (1820, -120, -4960) &nbsp; 2: (1972, -120, -4960) |
