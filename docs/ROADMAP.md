# Roadmap

## Phase 0 — Enregistreur de données brutes ← EN COURS
Objectif : enregistrer des sessions de jeu réelles pour dimensionner la suite.
- [x] `git init` + `.gitattributes` / `.gitignore` en place, premier commit.
- [x] Setup NCS + `west`, **noter la version exacte dans CLAUDE.md** (v3.4.1), build d'un blinky sur
      les deux types de XIAO. — [ ] flash UF2 (hardware).
- [x] `stick` : lecture IMU (driver ST), ODR 1,66 kHz, FIFO. — [ ] validé sur cible.
- [x] `stick` → `receiver` : ESB, paquets RAW de 8 échantillons, compteur de pertes. — [ ] validé sur cible.
- [x] `receiver` : relais USB CDC avec timestamp de réception. — [ ] validé sur cible.
- [x] `tools/logger.py` : lecture série → CSV par session (une colonne par axe + stick_id + seq).
      Port via `--port`, sinon auto-détection VID/PID ; doit tourner tel quel sous Windows et macOS.
- [ ] Valider la bascule : clone sur le Mac, même NCS, rebuild propre, flash, logger OK.
- [x] `tools/plot_session.py` : courbes accéléro/gyro, marquage des échantillons saturés.
- [ ] **Mesures** : débit réel, pertes de paquets, saturation gyro sur coups secs, dérive du lacet
      (baguette posée 10 min, puis 10 min de jeu).

Critère de sortie : sessions enregistrées des deux baguettes à ≥ 1 kHz sans pertes significatives.

### En parallèle, sans hardware (démarré 2026-10)
Le hardware n'étant pas encore là, l'app host avance sur fausses données (cf. DECISIONS.md) :
squelette JUCE, parseur du protocole, générateur de fausses données, vue 3D (scène, éclairage,
bloom), sampler basique. Ces briques seront reprises telles quelles en phase 3.
Le firmware est écrit et compilé (CI), mais marqué « non validé sur cible » jusqu'aux tests réels.

## Phase 1 — Détection hors ligne (Python)
- Annoter des sessions (frappes réelles sur un pad pour la vérité terrain, ou métronome).
- Détecteur + estimation de vélocité ; mesurer faux positifs/négatifs et latence de détection.
- Fusion (Mahony/Madgwick) + stratégies anti-dérive, évaluées sur les enregistrements.

## Phase 2 — Portage embarqué
- Détection + fusion en C dans `stick`, paquets HIT/ORIENT.
- `receiver` en USB MIDI (note + vélocité) → jouable dans un DAW.
- Mesure de latence bout en bout (ex. micro sur un pad + sortie audio, à l'oscillo ou par enregistrement).

## Phase 3 — App JUCE
(base déjà posée en phase 0 sur fausses données.)
- Sampler multi-couches, mapping zones (tangage/lacet), calibration, vue 3D, enregistreur raw.

## Phase 4 — Pédales, mécanique, options
- Pédales sur l'ADC du récepteur ; baguettes imprimées (équilibre, logement, charge).
- Évaluer magnétomètre / IMU meilleur / suivi IR selon les mesures.
