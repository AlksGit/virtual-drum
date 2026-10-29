# Architecture

```
[Baguette G]  XIAO nRF52840 Sense ─┐
[Baguette D]  XIAO nRF52840 Sense ─┼── ESB 2.4 GHz ──> [Récepteur XIAO nRF52840] ──USB──> [PC]
[Pédales] (plus tard)             ─┘                    CDC série (+ MIDI)            ├─ tools/ (Python logger)
                                                                                       ├─ host/ (app JUCE)
                                                                                       └─ DAW (via MIDI)
```

## Baguette (`firmware/stick`)
1. IMU LSM6DS3TR-C en FIFO, ODR cible **≥ 1 kHz** (accéléro + gyro), interruption FIFO watermark.
2. Calibration du biais gyro : au démarrage + à chaque période d'immobilité détectée (> ~0,5 s).
3. Fusion d'orientation (Mahony ou Madgwick) → quaternion.
4. Détection de coup + estimation de vélocité (voir plus bas).
5. Émission ESB :
   - **événement coup** immédiatement (prioritaire) ;
   - **flux d'orientation** ~100 Hz pour l'affichage/calibration ;
   - **mode raw** (phase 0) : échantillons bruts IMU pour enregistrement.

## Récepteur (`firmware/receiver`)
- ESB PRX, reçoit les deux baguettes (pipes ou adresses distinctes).
- Horodate à la réception, relaie vers USB CDC (binaire, cf. PROTOCOL.md).
- Plus tard : périphérique USB MIDI (note on + vélocité), entrées ADC pour les pédales.

## Host
- Phase 0–2 : scripts Python (`tools/`) — logger, plots, prototypage des algos.
- Phase 3+ : app **JUCE** (C++) — sampler multi-couches par vélocité, vue 3D des zones,
  calibration, enregistreur raw. Audio : ASIO (Windows) / CoreAudio, buffer 64 @ 48 kHz.

## Budget de latence (cible)
| Étape | Cible |
|---|---|
| Détection à bord (fenêtre + traitement) | 1–3 ms |
| ESB + USB | 1–2 ms |
| App + buffer audio | 2–4 ms |
| **Total** | **5–9 ms** |

## Détection de coup (point de départ, à valider sur données)
- Un coup « dans le vide » = la pointe descend vite puis la baguette est freinée brusquement.
- Candidats : pic de vitesse angulaire autour de l'axe de frappe, suivi d'un passage à zéro / forte
  décélération (jerk). Seuils + période réfractaire pour éviter les doubles déclenchements.
- Vélocité : dérivée de la vitesse angulaire **avant** le pic (robuste même si le gyro sature au pic).

## Zone visée et dérive
- Tangage/roulis : corrigés par la gravité, pas de dérive → la **hauteur** sépare déjà caisse claire / toms / cymbales.
- Lacet : dérive (pas de référence absolue sur un IMU 6 axes). Stratégies, par ordre :
  1. recalibration du biais à l'immobilité ;
  2. pas de saturation gyro (FS ±2000 dps, surveiller les échantillons saturés) ;
  3. recentrage automatique lent sur la zone la plus frappée (caisse claire) ;
  4. geste de recalibrage manuel (double-tap / pose) ;
  5. option : magnétomètre I²C externe ; option long terme : LED IR + caméra.
