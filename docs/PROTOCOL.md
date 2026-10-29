# Protocol — version 2 (brouillon, phase 0)

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
| `quat` | i16×4 — orientation au coup, ordre w, x, y, z, Q14 (1.0 = 16384) |
| `flags` | u8 — saturation détectée, etc. |

### Convention d'orientation (HIT et ORIENT)
Le quaternion fait passer du **repère baguette** au **repère monde** :
- repère baguette : X le long de la baguette, vers la pointe ;
- repère monde : X devant le joueur (vers le kit), Y à sa gauche, Z vers le haut.

Les zones de frappe se lisent sur la direction de la pointe : **azimut** (lacet, positif vers la
gauche) et **élévation** (tangage, positif vers le haut). Le kit par défaut est décrit dans
`host/src/core/DrumKit.h`. Le firmware (fusion Mahony/Madgwick, phase 2) devra produire ce repère.

### 0x03 ORIENT (~100 Hz)
`t_us` u32, `quat` i16×4 (w, x, y, z, Q14).

### 0x04 STATUS (~1 Hz)
`vbat_mv` u16, `gyro_bias` i16×3, `fw_version` u8.

## USB CDC (récepteur → PC)
Trame : `0xA5 0x5A | len u8 | rx_time_us u32 | rssi i8 | payload ESB (len octets) | crc8`.
- `len` = longueur du payload seul. Overhead total : 9 octets.
- `crc8` : **CRC-8/SMBUS** (poly 0x07, init 0x00, sans réflexion, sans xor final ;
  `crc8("123456789") = 0xF4`), calculé sur `len`, `rx_time_us`, `rssi` et le payload,
  c'est-à-dire tout ce qui est entre les octets de synchro et le crc.
- Le parseur resynchronise sur `0xA5 0x5A` en cas d'erreur (crc faux, type inconnu, longueur
  incohérente avec le type).

Identifiants USB du récepteur (phase 0) : VID `0x2FE3` / PID `0x0100` (valeurs de dev par défaut
de Zephyr), produit « Virtual Drum Receiver ». Les outils host détectent le port avec ces valeurs.

### Trame de référence
Vecteur de test partagé : tout parseur (C++, Python) doit la décoder à l'identique.
HIT, stick 1, seq 0x1234, `t_us` 0x01020304, vélocité 100, quat (16384, −1, 0, 258), flags 0x01,
`rx_time_us` 0xAABBCCDD, rssi −40 :
```
A5 5A 12 DD CC BB AA D8 02 01 34 12 04 03 02 01 64 00 40 FF FF 00 00 02 01 01 32
```

## Historique
- v2 (2026-10) : convention de repère des quaternions (baguette → monde, axes du monde).
- v1 (2026-10) : définition du crc8 et de sa couverture, ordre w, x, y, z des quaternions,
  identifiants USB. `protocol.h` compilable sous MSVC.
- v0 : brouillon initial.
