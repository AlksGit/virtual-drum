# Hardware

## Cartes
| Rôle | Carte | Réf. Seeed | Notes |
|---|---|---|---|
| Baguette ×2 | XIAO nRF52840 **Sense** | 102010469 | nRF52840 + IMU LSM6DS3TR-C + micro PDM, 21 × 17,5 mm |
| Récepteur ×1 | **nRF52840 Dongle** (Nordic PCA10059) | — | Matériel reçu (2026-11-02). Même puce, USB natif |
| Récepteur (alternative) | XIAO nRF52840 (non-Sense) | — | Toujours supporté par le firmware (UF2) |

Batteries : 2 × LiPo **LP321323** 3,7 V, **120 mAh**, avec circuit de protection (PCM) et fils.
Fiche du vendeur pour la version 80 mAh (li-polymer-battery.com, même référence) : 3,2 × 13 × 23 mm,
charge à 4,2 V ± 50 mV et **40 mA au plus**, décharge continue 80 mA au plus. Pas de fiche trouvée
pour la version 120 mAh (charge max estimée à ~60 mA si elle suit le même 0,5 C, non vérifié) ;
cotes à mesurer. **Ne pas les charger avec un SparkFun LiPo Charger Basic** (500 mA fixes) :
charger par la XIAO (BQ25101, 50 mA par défaut, P0.13 laissée en haute impédance).

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

## Premier démarrage (2026-11-02) — constaté sur les cartes
- **IMU pas prête au boot** : la première lecture de WHO_AM_I finissait en timeout I²C
  (-ETIMEDOUT), puis l'IMU répondait (WHO_AM_I = 0x6A à l'adresse 0x6A). Cause :
  `regulator_fixed_init` n'applique pas le `startup-delay-us` (3 ms) du régulateur
  `lsm6ds3tr-c-en` quand il est activé au boot. L'exemple Zephyr `samples/sensor/lsm6dsl`
  échoue de la même façon. Correctif : le firmware interroge WHO_AM_I jusqu'à 100 ms.
- **Écritures I²C** : `i2c_burst_write` exige un tampon de concaténation TWIM que le devicetree
  `xiao_ble` ne déclare pas → écriture en un seul message (adresse + données).
- **Horodatage** : `k_cycle_get_64()` renvoie 0 sur nRF52 (pas de compteur 64 bits) ; on utilise
  `k_uptime_ticks()` (32768 Hz).
- **Résultats** : chaque baguette envoie ~1650–1680 échantillons/s au dongle, accéléro à plat
  ≈ 1,02 g, RSSI ≈ −68 dBm à 1 m. Mesuré sur la baguette G : ~1680 échantillons/s (0,8 % au-dessus
  de 1666) — à confirmer par la mesure d'ODR sur 10 min ci-dessous.
- LED baguette : sans récepteur, la rouge reste allumée (chaque paquet échoue) ; ce n'est pas
  un échec de démarrage. Avec récepteur : bleue qui clignote.

## À vérifier au premier démarrage sur carte (firmware baguette)
Points relevés par la revue experte (TASK-029), non vérifiables sans matériel :
- lecture en rafale de FIFO_DATA_OUT : comparer une lecture groupée et une lecture mot par mot
  (retour d'adresse de 0x3F à 0x3E) ;
- ODR réel de l'IMU contre l'horloge de la baguette sur 10 min : l'horodatage suit la dérive
  par une correction proportionnelle (1/8 par lecture) ; au-delà de ~1 % d'écart, il faudra
  estimer la vraie période ;
- INT1 (seuil de FIFO) : confirmer qu'elle reste haute tant que la FIFO est au-dessus du seuil ;
- STATUS v4 : compteurs de retransmissions, d'échecs et de débordements de FIFO à relever pendant
  les mesures de TASK-012.

## Flash / debug
- **Baguettes, sans toucher au bouton** (firmware 3+, depuis le 2026-11-03) : baguette allumée et
  branchée en USB à ce PC, récepteur branché :
  `python tools/flash_stick.py --stick left --uf2 build/h0/stick/zephyr/zephyr.uf2` (`right`,
  `build/h1` pour la D). Le récepteur relaie une commande radio, la baguette redémarre dans
  son bootloader UF2 et le script copie le fichier. Vérifié sur les deux baguettes.
- **Veille automatique** (firmware 6, 2026-11-06, vérifiée sur les deux baguettes) : sans
  interrupteur, la baguette reste alimentée dès que la batterie est soudée. Après 3 min immobile
  (`CONFIG_VDRUM_SLEEP_AFTER_S` ; mouvement = 3 échantillons de suite à plus de 20 °/s du biais
  ou 0,15 g de 1 g : le test de la calibration du biais, 6 °/s et 0,05 g, ne tenait pas 3 min
  sur la table, bruit du gyro de G, accéléro de D à 1,03 g), elle passe
  en System OFF : radio et LED coupées, l'IMU seul en veille (accéléro 26 Hz, ±2 g) envoie une
  impulsion sur INT1 (P0.11) au premier mouvement (pente > 62,5 mg), ce qui réveille le
  nRF52840 par un reset : démarrage normal (~1 s), visée à recentrer (touche C). Le GPIO garde
  sa configuration en System OFF : l'IMU reste alimenté par P1.08. Sources : exemple Zephyr
  `samples/boards/nordic/system_off` (réveil par niveau sur GPIO), pilote ST
  (`WAKE_UP_THS` 1 LSB = FS / 64), forum Nordic DevZone pour la rétention du GPIO. Désactivée
  dans le firmware enregistreur (mesures de 10 min au repos). Essai du 2026-11-06 : les deux
  baguettes cessent d'émettre ~3 min après leur démarrage, posées ; secouées, elles émettent de
  nouveau (LED bleue). Consommation en veille : à mesurer. Pour flasher une baguette en veille, la bouger d'abord. Un interrupteur sur BAT+
  est prévu plus tard.
- **Deux firmwares de baguette** (firmware 4+, 2026-11-04) : *jeu* (HIT + STATUS seulement),
  `west build -b xiao_ble/nrf52840/sense firmware/stick -d build/p0 -- -DCONFIG_VDRUM_STICK_ID=0
  -DCONFIG_VDRUM_STREAM_RAW=n` (`build/p1`, id 1 pour la D) ; *enregistreur* (RAW en plus, pour
  `tools/logger.py`), sans l'option `STREAM_RAW` (`build/h0`, `build/h1`). On passe de l'un à
  l'autre par `flash_stick.py`. Le chemin du repo contient un espace, que le build sysbuild
  refuse : lancer `west` depuis un lecteur `subst V: <repo>`.
- UF2 (XIAO), à la main si le firmware ne répond plus : double-tap sur reset → disque `XIAO-SENSE` → copier `zephyr.uf2`. La carte
  redémarre seule ; le bootloader n'est jamais écrasé. Les baguettes n'ont pas d'USB dans leur
  firmware : une fois flashées, elles n'apparaissent plus sur le PC (normal).
- Dongle nRF52840 (récepteur) : board `nrf52840dongle/nrf52840` (application à 0x1000, après le
  MBR, compatible avec le bootloader Nordic d'usine). Appuyer sur le bouton **RESET** de la
  tranche (poussé latéralement, pas SW1) → LED rouge qui pulse → port série « nRF52 SDFU USB »
  (VID 0x1915, PID 0x521F). Puis :
  ```
  nrfutil install nrf5sdk-tools
  nrfutil nrf5sdk-tools pkg generate --hw-version 52 --sd-req=0x00 --application build/.../zephyr/zephyr.hex --application-version 1 receiver.zip
  nrfutil nrf5sdk-tools dfu usb-serial -pkg receiver.zip -p COMx
  ```
  Source : `zephyr/boards/nordic/nrf52840dongle/doc/index.rst`. LED : rouge = erreur,
  vert = activité (overlay `firmware/receiver/boards/nrf52840dongle_nrf52840.overlay`).
- Câbles USB-C : certains ne font que charger (carte sans LED, rien côté PC). Garder un câble
  données identifié.
- Pas de sonde SWD requise. Si besoin de debug pas-à-pas : pads SWD au dos + J-Link / nRF DK.

## Montage mécanique
- Carte côté **manche/talon**, pas en bout (équilibre ; le gyro mesure la même rotation partout sur un corps rigide).
- **Baguette imprimée** (2026-11-03) : `mech/stick/stick.py`, mode d'emploi dans `mech/README.md`.
  Deux pièces collées (talon 215 mm + fût 216 mm, pour tenir sur le plateau) et un couvercle
  vissé sous le talon (2026-11-06, auparavant un embout à picots).
  Carte dans le talon, X le long de la baguette vers la pointe, composants vers le haut (Z),
  prise USB-C au bout. Cotes de la carte lues dans le modèle 3D du fabricant
  (`XIAO-nRF52840 v15.step`) : PCB 20,95 × 17,78 mm, prise USB-C dépassant de 1,53 mm,
  blindage RF à 3,0 mm au-dessus du dessous du PCB, prise à 4,2 mm. Le modèle ne contient pas
  l'IMU : le couvercle laisse un creux de 2,2 mm sous la carte (hors bords du PCB), et 1 mm de
  mousse au-dessus du blindage.
- **Axes de l'IMU = axes de la baguette** (mesuré le 2026-11-03, test de gravité sur les deux
  baguettes montées) : x de l'IMU vers la pointe, z vers la fenêtre des LED, y à gauche quand la
  pointe s'éloigne. Aucune rotation à appliquer dans le firmware ni les outils.

  | Position (immobile 5 s) | Accéléro G (x, y, z) en g | Accéléro D |
  |---|---|---|
  | à plat, LED en haut | −0,010 / +0,005 / +1,021 | −0,006 / +0,055 / +1,029 |
  | debout, pointe en haut | +0,992 / −0,013 / +0,024 | +0,992 / −0,008 / −0,044 |
  | à plat, LED à droite | −0,023 / +0,999 / +0,034 | −0,022 / +0,992 / +0,131 |

  Les petits écarts viennent surtout du placement à la main ; à plat sur la table, la carte de
  D est inclinée de ~3° autour de la baguette dans son logement (adhésif), sans conséquence.
- **LiPo** (2026-11-06) : à plat dans le talon, juste après la carte côté pointe, dans la
  continuité de son logement ; soudée sur les pads BAT+ / BAT− (polarité à vérifier, la carte
  n'est pas protégée contre l'inversion). Montage : `mech/README.md`.

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
Sur le PC : `C:/ncs/v3.4.1`. Sur Mac, `~/ncs/v3.4.1` par exemple.
