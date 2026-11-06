# Backlog

Note : hardware reçu le 2026-11-02 (2 XIAO nRF52840 Sense + dongle nRF52840 Nordic comme
récepteur). Chaîne complète validée sur cible ; mesures en cours (docs/MEASUREMENTS.md).

## TODO
- [ ] TASK-035 | Radio : pertes en jeu, même avec une seule baguette
  > 2026-11-03 : G seule en jeu, 52 à 93 % des paquets reçus (100 % posée), trous jusqu'à 3 s,
  > RSSI −66 à −73 dBm. Émission passée à +8 dBm des deux côtés (firmware 3, flashé, non mesuré).
  > Fait = même test (G seule, 2 min : posée, jeu, reposée) comparé à session_20261103_153935 ;
  > si insuffisant : plus de retransmissions à une baguette, 1 Mbit/s, position du dongle.
  > Puis : dérive du lacet en jeu (TASK-012), créneaux (TASK-033). Embouts v2 à réimprimer.
- [ ] TASK-011 | Validation bascule PC → Mac
  > Clone, même NCS, `west build -p` propre, flash, logger OK sur Mac.
  > Blocked by: TASK-007, TASK-008, TASK-009 (chaîne complète à valider)

- [ ] TASK-033 | Radio : créneaux synchronisés entre les deux baguettes (mode RAW)
  > Repoussée (2026-11-04) : ne sert qu'à enregistrer le flux RAW des deux baguettes à la fois ;
  > en jeu (HIT seulement) la radio est presque vide. Enregistrer une baguette à la fois.
  > Cause des pertes identifiée (TASK-031) : collisions entre baguettes. Le récepteur mesure la
  > phase d'arrivée de chaque baguette et renvoie à la baguette 1 une correction dans le payload
  > d'ACK ; la baguette 1 cale l'envoi de ses paquets RAW à une demi-période de la baguette 0.
  > Fait = retransmissions < 10 % et pertes < 0,1 % à deux baguettes au repos (10 min).
  > Protocole : payload d'ACK à définir (PROTOCOL.md + version).
- [ ] TASK-032 | Gyro Z de la baguette G : raie à 769 Hz (40 % du bruit)
  > Échanger la place des baguettes ; si la raie suit la carte, la caractériser.
  > 2026-11-03 : raie toujours là (758–768 Hz, 28 à 87 % du bruit du gyro Z) avec la carte
  > montée dans la baguette imprimée et posée ailleurs : elle suit probablement la carte.
- [ ] TASK-012 | Mesures phase 0 (débit, pertes, saturation gyro, dérive lacet)
  > Sessions des deux baguettes enregistrées ; résultats chiffrés dans docs/ ;
  > critère de sortie de phase 0 atteint ou écarts identifiés.
  > Fait (2026-11-02) : 10 min au repos (pertes, retransmissions, ODR, bruit, dérive du lacet),
  > outil `tools/analyze_rest.py`.
  > Fait (2026-11-03) : coups secs, pas de saturation du gyro (1409 °/s max), accéléro à 13,6 g.
  > Reste : dérive du lacet pendant le jeu, une baguette à la fois (pertes radio en jeu à deux).

## IN PROGRESS
## ON HOLD

## DONE
- [x] TASK-041 | Baguette : veille automatique, réveil au mouvement
  > Firmware 6 : après 3 min immobile (test propre à la veille, 20 °/s / 0,15 g), System OFF,
  > réveil par l'IMU (wake-up sur INT1). Vérifié sur les deux baguettes : endormies ~3 min
  > après le démarrage, réveillées en les secouant. Enregistreur : jamais de veille.
  > Plus tard : interrupteur sur BAT+ (demandé par l'utilisateur), consommation en veille.
  > Merged in: devape (2026-11-06)
- [x] TASK-039 | Orientation à bord : viser tous les pads (hors grosse caisse)
  > Fusion Mahony (tools/detection/fusion.py → firmware/stick/src/fusion.c), biais du gyro
  > recalibré à chaque pause, ORIENT à 104 Hz, quaternion dans HIT, biais dans STATUS
  > (firmware 5, flashé). App : recentrage par baguette (touche C / bouton), décalages
  > d'azimut et de hauteur. Vérifié hors ligne (poses : élévation à 2,6° près ; 10 min au repos :
  > pas de dérive du lacet) et sur cible au repos (ORIENT reçus, biais = mesure du jour).
  > **Pas encore essayé en jeu** (2026-11-05) : justesse des pads, dérive en jeu.
  > Merged in: devape (2026-11-04)
- [x] TASK-038 | Première chaîne jouable : détection à bord → HIT → son dans l'app
  > Portage de tools/detection/detector.py dans la baguette (firmware/stick/src/hit.c), paquets
  > HIT, firmware de jeu sans RAW (`CONFIG_VDRUM_STREAM_RAW=n`). Pas encore de fusion :
  > tous les coups visent la caisse claire. Essayé sur les deux baguettes (2026-11-04) :
  > « ça a l'air pas trop mal ». Seuils à régler sur des coups enregistrés.
  > Merged in: devape (2026-11-04)
- [x] TASK-036 | Flash des baguettes sans double appui sur reset
  > Merged in: devape (2026-11-03). Commande radio ENTER_BOOTLOADER (protocole v5, payload d'ACK),
  > `tools/flash_stick.py`. Vérifié sur les deux baguettes. Émission à +8 dBm (TASK-035, à mesurer).
- [x] TASK-034 | Baguettes imprimées : modèle, impression et montage
  > Merged in: devape (2026-11-03). `mech/stick/stick.py` (CadQuery), deux pièces collées +
  > embout, pièces de test des jeux. Deux baguettes imprimées (Bambu P2S), montées, émettent.
  > Axes de l'IMU = axes de la baguette (test de gravité, docs/HARDWARE.md).
- [x] TASK-031 | Radio : cause des retransmissions ESB (37–69 %)
  > Fait (2026-11-02) : collisions entre les deux baguettes (2–4 % chacune seule). Écartés par
  > mesure : priorité USB du récepteur, montée radio rapide, plus de retransmissions / file ESB
  > plus grande (engorgement). docs/MEASUREMENTS.md. Suite : TASK-033.
- [x] TASK-030 | Premier démarrage sur cible : corrections firmware, récepteur sur dongle
  > Merged in: devape (2026-11-02). IMU interrogée trop tôt au boot (délai du régulateur ignoré
  > par Zephyr), écritures I²C en un message, horodatages à 0 (`k_cycle_get_64`), récepteur sur
  > dongle nRF52840 (overlay LED, CI), paquets périmés en début de session, outil
  > `tools/analyze_rest.py`, docs/MEASUREMENTS.md.
- [x] TASK-003 | Blinky sur les deux types de XIAO, flash UF2
  > Validé sur cible (2026-11-02) : flash UF2 des deux baguettes, LED, bootloader conservé.
  > Récepteur sur dongle Nordic (DFU nrfutil) au lieu d'une XIAO.
- [x] TASK-029 | Corrections pré-hardware issues de la revue experte (firmware, host, tools)
  > Merged in: devape (2026-10-29). Radio : l'init ESB échouait (délai < 435 µs), corrigé ; IMU qui
  > pouvait rester bloquée, horodatage revu (relu par un 2e agent) ; protocole v4 (compteurs dans
  > STATUS) ; pertes correctes dans plot_session ; voix en fondu. Points à vérifier sur carte dans
  > docs/HARDWARE.md.
- [x] TASK-025 | tools : téléchargement de kits de samples libres
  > Merged in: devape (2026-10-29). `tools/fetch_kits.py` : Swirly et Big Rusty (Karoryfer, CC0),
  > 6 couches de vélocité par pad, rien de commité. Sampler multi-couches. Chargement vérifié dans l'app.
- [x] TASK-028 | host : latence audio sous Windows (mode exclusif, ASIO optionnel)
  > Merged in: devape (2026-10-29). Liste « Audio » dans l'app (partagé, basse latence, exclusif,
  > ASIO si compilé avec le SDK Steinberg). Mesuré sur le PC : 480 éch. (~20 ms) en partagé, 144 éch.
  > (~9 ms) en exclusif.
- [x] TASK-026 | tools : prototype de détection des coups (Python, IMU simulée)
  > Merged in: devape (2026-10-29). Simulation réaliste : rappel ~99 %, précision ~98,5 %,
  > corrélation de vélocité 0,995, déclenchement ~8 ms avant l'arrêt. Seuil 260 dps (compromis
  > notes fantômes / gestes parasites, à régler sur données réelles). CLI aussi sur CSV réels.
- [x] TASK-027 | host : sortie MIDI vers un DAW
  > Merged in: devape (2026-10-29). Notes GM canal 10, vélocité 1-127, note off à 30 ms, sortie
  > choisie dans l'app et mémorisée, port virtuel sur Mac. Vérifié avec le synthé GS de Windows.
- [x] TASK-024 | host : sélecteur de kits de sons
  > Merged in: devape (2026-10-29). 4 kits synthétisés + kits de samples utilisateur, liste
  > déroulante + touche K, choix mémorisé, changement à chaud sûr. Vérifié dans l'app (kit de
  > samples de test détecté, rééchantillonné ; changements pendant le jeu). 3 tests de plus.
- [x] TASK-007 | Lien ESB stick → receiver, paquets RAW, compteur seq
  > Merged in: devape (2026-10-29), branche feat/task-007-esb-raw-v2. ESB PTX, RAW de 8 échantillons,
  > STATUS 1 Hz, paquet raté jeté puis reprise. UF2 gauche/droite en CI. **Non validé sur cible.**
- [x] TASK-008 | receiver : relais USB CDC avec timestamp de réception + crc8
  > Merged in: devape (2026-10-29). ESB PRX 2 canaux → trames USB CDC (crc8, horodatage, RSSI dBm),
  > un seul port, tampon qui jette des trames entières. Review indépendante traitée.
  > **Non validé sur cible** ; débit USB à mesurer avec les deux baguettes (QUESTIONS.md).
- [x] TASK-006 | stick : lecture IMU LSM6DS3TR-C en FIFO, ODR ≥ 1 kHz
  > Merged in: devape (2026-10-29). Code + build OK (CI), **non validé sur cible** : débit réel,
  > ordre gyro/accel détecté, lecture FIFO multi-octets à confirmer (QUESTIONS.md).
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
