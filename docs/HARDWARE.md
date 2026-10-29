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
- [x] Nom exact de la board (NCS v3.4.1) : `xiao_ble/nrf52840` (récepteur) et
      `xiao_ble/nrf52840/sense` (baguettes). Source : `west boards`, `zephyr/boards/seeed/xiao_ble/board.yml`.
- [x] IMU : bus `i2c0` (TWIM), adresse **0x6A**, SDA **P0.07**, SCL **P0.27**, 400 kHz
      (`I2C_BITRATE_FAST`), pilote Zephyr `st,lsm6dsl`. **Alimentation de l'IMU : P1.08** (actif haut),
      régulateur `lsm6ds3tr-c-en` avec `regulator-boot-on` et 3 ms de démarrage : Zephyr l'active
      au boot. Source : `xiao_ble_nrf52840_sense.dts`, `xiao_ble-pinctrl.dtsi`.
      Débit I²C : 1666 Hz × 12 o ≈ 20 ko/s, soit environ la moitié des ~44 ko/s utiles à 400 kHz.
      Lire la FIFO par rafales (pas échantillon par échantillon). Le TWIM du nRF52840 est spécifié
      jusqu'à 400 kHz : pas de marge pour 3,33 kHz sur I²C.
- [x] INT1 : câblée au nRF sur **P0.11** (`irq-gpios`, actif haut). Interruption FIFO watermark possible.
      Source : `xiao_ble_nrf52840_sense.dts`.
- [x] Mesure batterie : entrée ADC **P0.31** (AIN7), chemin activé en mettant **P0.14 à l'état bas**.
      ⚠️ Wiki Seeed : « When P0.14 is set HIGH, the battery voltage reading path is disabled and
      P0.31 may reach the input voltage limit of 3.6V, posing a risk of damaging the P0.31 pin »,
      et « avoid setting P0.14 HIGH during battery charging ». → **Garder P0.14 à l'état bas en
      permanence.** Le ratio du diviseur n'est pas donné par le wiki ni le devicetree : **à lire sur
      le schéma Seeed avant d'écrire la conversion** (non deviné). Source : wiki Seeed XIAO BLE.
- [x] Charge : chargeur **BQ25101**, courant choisi par **P0.13** : entrée haute impédance sans pull
      → **50 mA**, sortie à l'état bas → **100 mA** (phase courant constant). Défaut sûr : 50 mA ;
      100 mA seulement si la LiPo accepte ≥ 1 C (capacité à confirmer). Source : wiki Seeed XIAO BLE.
- [x] Flash avec le bootloader UF2 (Adafruit nRF52, SoftDevice s140 v7 réservée) : application à
      **0x27000** (taille 0xC5000), settings à 0xEC000, UF2 à 0xF4000. `CONFIG_BUILD_OUTPUT_UF2=y`
      est déjà le défaut de la board. Source : `pm_static.yml`, `xiao_ble_defconfig`.

## LSM6DS3TR-C — valeurs vérifiées
Source : pilote ST livré avec NCS v3.4.1 (`modules/hal/st/sensor/stmemsc/lsm6ds3tr-c_STdC/driver`).
| Paramètre | Valeur |
|---|---|
| Sensibilité accéléro ±16 g | 0,488 mg/LSB (`lsm6ds3tr_c_from_fs16g_to_mg`) |
| Sensibilité accéléro ±8 g | 0,244 mg/LSB |
| Sensibilité gyro ±2000 dps | 70 mdps/LSB (`lsm6ds3tr_c_from_fs2000dps_to_mdps`) |
| WHO_AM_I | registre 0x0F, valeur 0x6A |
| ODR disponibles (extrait) | 1,66 kHz, 3,33 kHz, 6,66 kHz (accéléro et gyro) |
| Taille de la FIFO | à vérifier dans la datasheet (non confirmé ici) |

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
