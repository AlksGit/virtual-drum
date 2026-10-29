# Virtual Drum — baguettes IMU

Batterie virtuelle : deux baguettes équipées d'un IMU détectent les coups (vélocité + zone visée),
envoient des événements par radio propriétaire basse latence à un récepteur USB, qui alimente une app
native de sampler et/ou un DAW via MIDI.

Objectif principal : **latence geste → son < 10 ms** et un jeu expressif (vélocité fiable).
Projet inspiré d'un prototype d'un collègue (Puck.js + Espruino + app web) dont les limites étaient
la latence (JS interprété + BLE + Web Audio) et la détection basique.

Langue : on discute en **français** ; code, identifiants et commentaires en **anglais**.

## Contexte détaillé (à lire selon la tâche)
- @docs/ARCHITECTURE.md — vue d'ensemble, budget de latence, rôle de chaque bloc
- @docs/HARDWARE.md — cartes, capteurs, points à vérifier (pins, charge, batterie)
- @docs/PROTOCOL.md — format des paquets radio et du lien USB
- @docs/DECISIONS.md — décisions prises et pourquoi (ne pas les remettre en cause sans raison)
- @docs/ROADMAP.md — phases ; **on est en phase 0**

## Structure du repo
```
firmware/
  stick/      # nRF Connect SDK app — baguette (XIAO nRF52840 Sense), ESB PTX
  receiver/   # nRF Connect SDK app — récepteur USB (XIAO nRF52840), ESB PRX, USB CDC (+ MIDI plus tard)
  common/     # headers partagés : format de paquets, constantes
tools/        # Python : logger série → CSV, analyse/plots, prototypage des algos
host/         # app native JUCE (C++) — phase 3+
docs/
```

## Toolchain & commandes
- Firmware : **nRF Connect SDK (Zephyr)**, build avec `west`.
  - Baguette : `west build -b xiao_ble/nrf52840/sense firmware/stick`
  - Récepteur : `west build -b xiao_ble/nrf52840 firmware/receiver`
  - Noms vérifiés pour NCS v3.4.1 (`west boards` → `xiao_ble`, variante `sense` dans
    `zephyr/boards/seeed/xiao_ble/board.yml`).
  - `west` se lance via la toolchain : `nrfutil toolchain-manager launch --ncs-version v3.4.1 -- west ...`
- Flash : les XIAO ont un bootloader **UF2** (double-tap reset → disque USB). Activer
  `CONFIG_BUILD_OUTPUT_UF2=y` et copier `build/.../zephyr.uf2`. Pas de sonde SWD nécessaire.
- Python : `tools/` avec un `requirements.txt` (pyserial, numpy, pandas, matplotlib).

## Multi-machine (PC ↔ Mac)
Le dev se fait sur un PC puis sur un Mac. Le repo git est le seul lien entre les deux.
- **Version NCS figée : `v3.4.1`** (toolchain + SDK, installés le 2026-10-29 sur le PC). Même version
  et même outil d'installation sur les deux machines : `nrfutil toolchain-manager` (voir
  `docs/HARDWARE.md`, section Toolchain). Ne jamais changer de version NCS sans l'acter dans
  `docs/DECISIONS.md`.
- `build/` n'est pas portable : après un changement de machine, rebuild propre (`west build -p`).
- Fins de ligne : LF partout (`.gitattributes`).
- Port série : `COMx` sous Windows, `/dev/cu.usbmodem*` sur Mac. **Aucun outil ne code un port en
  dur** : port en argument (`--port`), et par défaut auto-détection de la XIAO par VID/PID USB
  (via `serial.tools.list_ports`).
- Chemins Python : `pathlib`, jamais de séparateurs en dur.
- Venv Python recréé sur chaque machine (`python -m venv .venv` + `pip install -r tools/requirements.txt`),
  jamais commité.
- L'historique des sessions Claude Code ne suit pas d'une machine à l'autre : toute décision ou
  découverte importante (pin vérifié, mesure, choix) va dans `docs/` avant de changer de machine.
- Audio (phase 3) : CoreAudio sur Mac ; sous Windows il faut ASIO (driver carte son ou ASIO4ALL +
  SDK ASIO Steinberg pour compiler JUCE).

## Règles de travail
- **Ne jamais deviner un pin, une adresse I²C ou un registre.** Vérifier dans le devicetree de la board
  Zephyr, le schéma Seeed ou la datasheet, et citer la source dans un commentaire.
- Le firmware de la baguette est temps réel : pas d'allocation dynamique, pas de logs bloquants dans le
  chemin IMU → détection → radio. Logs via RTT/UART uniquement en debug.
- Toute nouvelle logique de détection se prototype d'abord **hors ligne en Python** sur des
  enregistrements réels (`tools/`), puis se porte en C.
- Le format de paquet vit dans `firmware/common/` et doit rester synchronisé avec le parseur Python
  et, plus tard, l'app host. Toute modif → mettre à jour `docs/PROTOCOL.md` et incrémenter la version.
- Mesurer avant d'optimiser : latence, débit, saturation capteur sont vérifiés par des mesures loggées.
- Quand une décision d'architecture change, l'ajouter dans `docs/DECISIONS.md`.

## Matériel disponible
- 2× Seeed XIAO nRF52840 **Sense** (baguettes, IMU LSM6DS3TR-C intégré)
- 1× Seeed XIAO nRF52840 (récepteur USB)
- LiPo 3,7 V petite capacité (à souder sur les pads BAT)
- Pas encore : magnétomètre externe, pads de pédales, caméra IR
