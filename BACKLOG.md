# Backlog

Note : hardware pas encore disponible (2026-10-29). Le code peut avancer, mais toute
validation "sur cible" (flash, mesures) attend le matériel.

## TODO
- [ ] TASK-006 | stick : lecture IMU LSM6DS3TR-C en FIFO, ODR ≥ 1 kHz
  > Accéléro + gyro ≥ 1 kHz, ±16 g / ±2000 dps ; débit vérifié par log RTT/UART ;
  > pas d'allocation dynamique.
  > Hardware requis pour : validation du débit.
  > Blocked by: TASK-003 (build/flash), TASK-004 (pins et adresse I²C vérifiés)

- [ ] TASK-007 | Lien ESB stick → receiver, paquets RAW, compteur seq
  > Le récepteur reçoit les paquets RAW des deux baguettes (pipes distincts) ;
  > `seq` permet de compter les pertes.
  > Hardware requis pour : validation radio.
  > Blocked by: TASK-005 (format), TASK-006 (données à envoyer)

- [ ] TASK-011 | Validation bascule PC → Mac
  > Clone, même NCS, `west build -p` propre, flash, logger OK sur Mac.
  > Blocked by: TASK-007, TASK-008, TASK-009 (chaîne complète à valider)

- [ ] TASK-012 | Mesures phase 0 (débit, pertes, saturation gyro, dérive lacet)
  > Sessions des deux baguettes enregistrées ; résultats chiffrés dans docs/ ;
  > critère de sortie de phase 0 atteint ou écarts identifiés.
  > Hardware requis.
  > Blocked by: TASK-010 (analyse), TASK-007/008/009 (chaîne complète)

## IN PROGRESS
- [ ] TASK-008 | receiver : relais USB CDC avec timestamp de réception + crc8
  > Trame `0xA5 0x5A | len | rx_time_us | rssi | payload | crc8` conforme à
  > docs/PROTOCOL.md, visible sur un terminal série. Peut avancer avec paquets factices.
  > Hardware requis pour : validation USB.
  > Branch: feat/task-008-receiver-relay
  > Blocked by: TASK-005 (format), TASK-003 (flash du receiver)

## ON HOLD
- [ ] TASK-003 | Blinky sur les deux types de XIAO, flash UF2
  > `west build` passe pour stick (Sense) et receiver ; `CONFIG_BUILD_OUTPUT_UF2=y` ;
  > LED clignote après copie du .uf2 ; bootloader UF2 conservé (offset de partition
  > vérifié et noté dans docs/HARDWARE.md).
  > Hardware requis pour : flash + LED.
  > Fait : build des deux apps, UF2 vérifié (0x27000, famille nRF52840), CI firmware avec UF2 en artefacts.
  > Reason: reste le flash + LED, hardware requis (voir QUESTIONS.md).

## DONE
- [x] TASK-020 | host : bloom et animations au coup
  > Merged in: devape (2026-10-29). Bloom HDR (pyramide 6 niveaux), anneau lumineux sur les peaux,
  > balancement amorti des cymbales. Audio ouvert 1,5 s après le démarrage (la scène s'affiche
  > d'abord), option --no-audio. Particules non faites (optionnelles).
- [x] TASK-023 | host : modèles procéduraux détaillés du kit
  > Merged in: devape (2026-10-29). Cercles, coquilles, tirants, grosse caisse avec pédale et logo
  > néon, support de toms, tom basse sur pieds, tripodes, pieds à perche dégagés des pads,
  > charleston avec pédale, siège. Captures : docs/images/host-3d-view*.png.
- [x] TASK-004 | Vérification hardware IMU (devicetree, pins, I²C, power GPIO, INT1)
  > Merged in: devape (2026-10-29). IMU i2c0 @0x6A, INT1 P0.11, alim P1.08, batterie P0.31/P0.14,
  > charge P0.13, layout UF2 : tout sourcé dans HARDWARE.md. Restent : ratio du diviseur, taille FIFO.
- [x] TASK-002 | Setup toolchain NCS + west, version figée
  > Merged in: devape (2026-10-29). NCS v3.4.1 installé (nrfutil), noms de cartes vérifiés,
  > version notée dans CLAUDE.md et DECISIONS.md. Procédure Mac dans HARDWARE.md.
- [x] TASK-019 | host : rendu physique (PBR), ombres, environnement studio
  > Merged in: devape (2026-10-29). Ombres (projecteur), PBR GGX, laque à paillettes vernie,
  > chrome, bronze rainuré, environnement studio néon, HDR + ACES, 60 fps. Capture :
  > docs/images/host-3d-view.png. Review indépendante : bug NaN hors NVIDIA corrigé.
- [x] TASK-010 | tools/plot_session.py : courbes accel/gyro + marquage saturation
  > Merged in: devape (2026-10-29). Tracés bruts accel/gyro par baguette, saturation marquée,
  > pertes surlignées, stats par baguette. 5 tests.
- [x] TASK-018 | host : sampler basique
  > Merged in: devape (2026-10-29). 24 voix, sons synthétisés (pas de fichiers), courbe de
  > vélocité, panoramique, file sans verrou. 13 tests. Sur le PC : WASAPI basse latence mais
  > le pilote Realtek impose 480 échantillons (~20 ms) : ASIO nécessaire pour la cible.
- [x] TASK-009 | tools/logger.py : série → CSV par session
  > Merged in: devape (2026-10-29). Parseur Python identique au C++ (constantes vérifiées contre
  > protocol.h), CSV par session, rejeu de captures. 35 tests, CI Linux/Windows/macOS.
  > Lecture série réelle non validée (hardware).
- [x] TASK-016 | host : scène 3D de base (kit + baguettes + caméra)
  > Merged in: devape (2026-10-29). Scène OpenGL 4.1 à 60 fps vérifiée par captures (Windows),
  > flash au coup, caméra orbitale, tempo au clavier. 12 tests de géométrie. Build Mac OK en CI,
  > jamais lancé sur Mac.
- [x] TASK-005 | Décision : taille des paquets RAW (ESB DPL vs 1 échantillon/paquet)
  > Merged in: devape (2026-10-29). DPL, 8 échantillons / paquet (105 o), protocole v3.
  > Calcul de temps d'antenne dans DECISIONS.md ; risque de collisions à mesurer (TASK-012).
- [x] TASK-015 | host : générateur de fausses données réalistes
  > Merged in: devape (2026-10-29). Batteur simulé déterministe (groove, breaks, mouvement),
  > 11 tests, `vdrum_monitor --fake`. Protocole v2 (convention de repère des quaternions).
- [x] TASK-014 | host : parseur protocole + source de données abstraite
  > Merged in: devape (2026-10-29). Protocole v1 (crc8 spécifié, USB IDs), 17 tests, CI verte
  > Windows + macOS. Source série non validée sur cible (hardware). Source fausses données : TASK-015.
- [x] TASK-022 | CI GitHub Actions : build host Windows + macOS
  > Merged in: devape (2026-10-29). CI verte Windows + macOS.
- [x] TASK-017 | Mise à jour roadmap et décisions (app avancée en phase 0)
  > Merged in: devape (2026-10-29)
- [x] TASK-021 | host : README, avertissement chemins longs Windows
  > Merged in: devape (2026-10-29)
- [x] TASK-013 | host : squelette app JUCE (CMake, Windows + Mac)
  > Merged in: devape (2026-10-29), branch feat/task-013-host-skeleton
- [x] TASK-001 | Init repo (.gitignore, .gitattributes, premier commit)
  > Merged in: main (commit d16e7ae)

## CANCELLED
