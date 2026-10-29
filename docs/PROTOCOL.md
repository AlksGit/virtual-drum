# Protocol — version 0 (brouillon, phase 0)

Source de vérité : `firmware/common/protocol.h`. Toute modif ici ⇄ là ⇄ parseur Python
(`tools/`), et incrément de `PROTO_VERSION`.

Tous les champs en **little-endian**, structures **packées**.

## Radio ESB (baguette → récepteur)
Payload max ESB : 32 octets (statique) ; on vise des paquets ≤ 32 octets.

En-tête commun (4 octets) :
| Champ | Type | Description |
|---|---|---|
| `type` | u8 | 0x01 RAW, 0x02 HIT, 0x03 ORIENT, 0x04 STATUS |
| `stick_id` | u8 | 0 = gauche, 1 = droite |
| `seq` | u16 | compteur par baguette (détection de pertes) |

### 0x01 RAW (phase 0)
Plusieurs échantillons par paquet pour tenir le débit à 1 kHz+.
| Champ | Type |
|---|---|
| `t0_us` | u32 — timestamp du 1er échantillon (horloge baguette) |
| `n` | u8 — nombre d'échantillons (ex. 1–2) |
| `samples[n]` | {ax, ay, az, gx, gy, gz} en i16 brut capteur (12 octets) |

→ 4 + 4 + 1 + 2×12 = 33 octets : **trop gros pour 2 échantillons**. À trancher en phase 0 :
payload dynamique (ESB DPL, jusqu'à 252 octets sur nRF52840 selon config) ou 1 échantillon/paquet.
Décision à consigner dans DECISIONS.md.

### 0x02 HIT
| Champ | Type |
|---|---|
| `t_us` | u32 — instant du coup |
| `velocity` | u8 — 1..127 |
| `quat` | i16×4 — orientation au coup (Q14) |
| `flags` | u8 — saturation détectée, etc. |

### 0x03 ORIENT (~100 Hz)
`t_us` u32, `quat` i16×4 (Q14).

### 0x04 STATUS (~1 Hz)
`vbat_mv` u16, `gyro_bias` i16×3, `fw_version` u8.

## USB CDC (récepteur → PC)
Trame : `0xA5 0x5A | len u8 | rx_time_us u32 | rssi i8 | payload ESB (len octets) | crc8`.
Le parseur resynchronise sur `0xA5 0x5A` en cas d'erreur.
