# Hardware

## Cartes
| Rôle | Carte | Réf. Seeed | Notes |
|---|---|---|---|
| Baguette ×2 | XIAO nRF52840 **Sense** | 102010469 | nRF52840 + IMU LSM6DS3TR-C + micro PDM, 21 × 17,5 mm |
| Récepteur ×1 | XIAO nRF52840 (non-Sense) | — | USB natif, servira aussi d'entrée ADC pour les pédales |

Batteries : LiPo 3,7 V ~100–150 mAh (en stock au bureau, à vérifier : capacité, connecteur, protection).

## IMU : LSM6DS3TR-C
- Accéléro : jusqu'à ±16 g. Gyro : jusqu'à **±2000 dps** (risque de saturation sur coups secs).
- ODR jusqu'à 6,66 kHz, FIFO intégrée.
- Config de départ : accéléro ±16 g, gyro ±2000 dps, ODR 1,66 kHz (ou 3,33 kHz si le lien suit).
- **Premier test à réception : mesurer la saturation du gyro sur des coups réels.**

## Points à vérifier AVANT d'écrire le code (ne pas deviner)
Sources : devicetree Zephyr de la board `xiao_ble` (`boards/seeed/xiao_ble/` dans Zephyr),
schéma et wiki Seeed « XIAO nRF52840 (Sense) », datasheet LSM6DS3TR-C.
- [ ] Nom exact de la board pour la version de NCS installée (`west boards | grep xiao`).
- [ ] Bus et adresse I²C de l'IMU, et GPIO d'**alimentation de l'IMU** (l'IMU de la Sense est
      alimenté par une broche du nRF qu'il faut activer) — lire le devicetree, ne pas supposer.
- [ ] Broche d'interruption de l'IMU (INT1) : est-elle câblée au nRF ? Sinon, polling FIFO sur timer.
- [ ] Mesure tension batterie : diviseur + broche d'activation. **Lire l'avertissement du wiki Seeed
      sur cette broche avant de la piloter** (mauvaise config = risque pour l'entrée ADC).
- [ ] Courant de charge (chargeur BQ25101, sélection par une broche) : adapter à la capacité de la LiPo.
- [ ] Layout flash avec le bootloader UF2 d'origine (offset de partition applicatif) — garder le bootloader.

## Flash / debug
- UF2 : double-tap sur reset → disque USB → copier `zephyr.uf2`.
- Pas de sonde SWD requise. Si besoin de debug pas-à-pas : pads SWD au dos + J-Link / nRF DK.

## Montage mécanique (plus tard)
- Carte côté **manche/talon**, pas en bout (équilibre ; le gyro mesure la même rotation partout sur un corps rigide).
- Baguette imprimée avec logement Ø intérieur ~22 mm, accès USB-C pour la charge.

## Options futures
- Magnétomètre I²C (LIS3MDL ou MMC5603) pour corriger lentement le lacet.
- IMU externe meilleur (LSM6DSV16X, ±4000 dps) en breakout sur une XIAO non-Sense si le gyro sature.
- Pédales : piézo / FSR (grosse caisse), Hall / FSR (charleston) sur l'ADC du récepteur.
- LED IR + caméra global shutter pour une position absolue.

## Toolchain (NCS v3.4.1)
Même procédure sur le PC et le Mac :
```
nrfutil install toolchain-manager            # si la commande n'est pas encore installée
nrfutil toolchain-manager install --ncs-version v3.4.1
nrfutil toolchain-manager launch --ncs-version v3.4.1 -- west init -m https://github.com/nrfconnect/sdk-nrf --mr v3.4.1 <dossier>/v3.4.1
cd <dossier>/v3.4.1
nrfutil toolchain-manager launch --ncs-version v3.4.1 -- west update --narrow -o=--depth=1
```
Sur le PC : `C:
cs3.4.1`. Sur Mac, `~/ncs/v3.4.1` par exemple.
