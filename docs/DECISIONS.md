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
