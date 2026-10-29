# Decisions

Format : date — décision — pourquoi — alternatives écartées.

- **2026-10** — Cartes du commerce uniquement, pas de PCB custom. — Contrainte du projet.
- **2026-10** — Baguettes : Seeed XIAO nRF52840 Sense. — Format 21 × 17,5 mm (rentre dans un manche),
  IMU 6 axes intégré, charge LiPo intégrée, nRF52840 avec ESB mature dans NCS.
  Écartés : Puck.js (pile CR2032, format galet, nRF52832) ; SensorTile.box PRO (meilleur IMU mais
  radio BLE seulement, boîtier trop gros, ~80 CHF) ; XIAO nRF54L15 Sense (SDK moins mûr, même IMU).
- **2026-10** — Radio : **ESB** (Nordic Enhanced ShockBurst) plutôt que BLE. — Latence ~1–2 ms et
  déterministe ; BLE limité par l'intervalle de connexion de l'OS (~7,5–15 ms + gigue).
- **2026-10** — Récepteur : une 3ᵉ XIAO nRF52840 au lieu du nRF52840 Dongle Nordic. — Même chip,
  USB natif, ADC dispo pour les pédales.
- **2026-10** — Sortie : récepteur en USB composite (CDC pour données/config, MIDI plus tard).
  App host native **JUCE** (pas d'app web). — Latence audio, compatibilité DAW.
- **2026-10** — Détection de coup et fusion **à bord** ; on n'envoie que des événements + orientation
  (le flux brut seulement en phase 0 pour l'enregistrement).
- **2026-10** — Pas de magnétomètre au départ ; dérive du lacet gérée par recalibration du biais à
  l'immobilité + recentrage logiciel. Magnétomètre externe = option à évaluer sur données.
- **2026-10** — Algorithmes prototypés en Python sur enregistrements réels avant portage C.
- **2026-10** — Dev sur deux machines (PC puis Mac). — Repo git comme seul lien, version NCS figée
  et identique, LF partout, outils indépendants de l'OS (port série auto-détecté, `pathlib`).
- **2026-10** — Host : JUCE récupéré par CMake `FetchContent`, figé sur le tag **9.0.4** ; rendu 3D
  en OpenGL 4.1 Core. — Une seule ligne à changer pour monter de version, même version garantie sur
  PC et Mac, pas d'étape `git submodule`. 4.1 Core est le maximum supporté par macOS.
  Écartés : sous-module git (étape manuelle de plus, facile à désynchroniser) ; Projucer (projets
  générés par OS, moins adapté au multi-machine).
- **2026-10** — App host démarrée dès la phase 0, sur fausses données, avant l'arrivée du hardware.
  — Le matériel n'est pas encore livré ; la vue 3D, le parseur et le sampler peuvent avancer
  sans lui. On écrit directement l'app JUCE cible (pas de prototype jetable) ; les fausses données
  passent par le vrai format de trame et le vrai parseur.
- **2026-10** — Rendu 3D **tout natif** (OpenGL dans JUCE). — Une seule techno, pas de pont
  C++ ↔ JavaScript ; un rendu stylé (PBR, bloom, animations) reste atteignable en OpenGL 4.1.
  Risque connu : OpenGL est déprécié sur macOS (gelé en 4.1 mais fonctionnel) ; si Apple le
  retire, porter le rendu vers Metal ou passer par une couche de traduction.
  Écarté : UI hybride (WebView JUCE + three.js), plus rapide pour le visuel mais deux langages.
- **2026-10** — Paquets RAW : **payload ESB dynamique (DPL), 8 échantillons par paquet**
  (105 octets), soit un paquet toutes les 4,8 ms par baguette à 1,66 kHz. — Calcul de temps
  d'antenne (2 Mbit/s, préambule 2 o, adresse 5 o, PCF 10 bits, CRC 2 o, ACK vide, montée
  radio 40 µs, retournement ~150 µs), pour 2 baguettes :

  | Échantillons / paquet | Payload | Charge radio |
  |---|---|---|
  | 1 | 21 o | 119 % (impossible) |
  | 4 | 57 o | 42 % |
  | 8 | 105 o | 29 % |
  | 16 | 201 o | 22 % |

  Au-delà de 8, le gain est faible et chaque retransmission coûte plus cher. Le flux brut seul
  occupe ~16 % du débit (2 × 1666 × 12 o). Écarté : 1 échantillon par paquet statique de 32 o.
  **Risque à mesurer (TASK-012)** : les deux baguettes émettent sans coordination ; chacune occupe
  ~10 % de l'air, donc ~20 % de ses paquets risquent une collision et une retransmission. Mesures
  prévues : délais de retransmission ESB différents par baguette ; si les pertes restent
  significatives, synchronisation par créneaux via les payloads d'ACK du récepteur. Le flux RAW
  n'existe qu'en phase 0 ; en jeu (HIT + ORIENT 100 Hz) la charge est faible.
- **2026-10** — **NCS v3.4.1** (dernière stable), installé avec `nrfutil toolchain-manager`
  (toolchain dans `C:/ncs/toolchains`, SDK dans `C:/ncs/v3.4.1`, clone superficiel). — Version
  la plus récente, carte `xiao_ble` (variante `sense`) supportée avec le partitionnement UF2.
  Installée sans changer la toolchain par défaut ni l'enregistrement CMake global, pour ne pas
  perturber d'autres projets NCS (v2.3.0 / v2.5.0) présents sur le PC.
- **2026-10** — Samples acoustiques : kits **CC0** de Karoryfer (Swirly, Big Rusty) **téléchargés
  par un script** (`tools/fetch_kits.py`, commits figés), jamais commités ; **couches de vélocité**
  dans le sampler (`<pad>_v<N>`, normalisées ensemble pour garder la dynamique enregistrée).
  — Repo public : CC0 évite toute obligation d'attribution ou de partage ; le script garde le
  repo léger. Écartés : Salamander (CC-BY-SA, archive de 387 Mo), samples commités dans le repo.
