# Mesures (phase 0, TASK-012)

Mesures faites sur le matériel réel. Chaque résultat cite sa session (`recordings/`, non commité)
et l'outil qui le calcule, pour pouvoir le refaire.

## 2026-11-02 — deux baguettes au repos, 10 min

Session `session_20261102_133155` (`tools/logger.py --duration 600 --save-raw`), analysée par
`python tools/analyze_rest.py recordings/session_20261102_133155_imu.csv`.
Conditions :
- baguettes G (id 0) et D (id 1) posées à plat sur une table, alimentées en USB, sans batterie ;
- récepteur : dongle nRF52840 sur le PC, à ~1 m ;
- bureau avec Wi-Fi 2,4 GHz chargé sur les canaux 1, 5 et 11 (40 à 50 % d'occupation) ;
  ESB est sur le canal 80 (2480 MHz), au-dessus du Wi-Fi mais sur le canal d'annonce BLE 39 ;
- firmware : `fix/task-030-stick-boot` (commit 2c463f1).

### Lien radio

| | Baguette G | Baguette D |
|---|---|---|
| RSSI moyen (min) | −52 dBm (−60) | −65 dBm (−72) |
| Paquets reçus | 125 820 | 124 136 |
| Paquets perdus | **1** (0,001 %) | **765** (0,6 %), rafales de 2 au plus |
| Retransmissions ESB | 46 524 (**37 %** des paquets) | 58 313 (**47 %**) |
| Échecs après toutes les retransmissions | 239 | 1 123 |
| File radio pleine | 0 | 0 |
| Débordements de FIFO IMU | 0 | 0 |

- Critère de sortie de la phase 0 (« sans pertes significatives ») : atteint pour G, D à 0,6 %.
- **Le taux de retransmissions est élevé** (37 % sur G, 47 % sur D) : ce sont les
  **collisions entre les deux baguettes**, voir « Retransmissions : causes » ci-dessous.

### Horloges

| | Baguette G | Baguette D |
|---|---|---|
| ODR réel de l'IMU (horloge baguette) | 1678 Hz (**+0,70 %**) | 1665 Hz (−0,05 %) |
| Horloge baguette contre récepteur | +3,5 ppm | +11 ppm |
| Écart d'horodatage entre paquets consécutifs | ±25 µs (max 50 µs) | max 6 µs |

- L'ODR de l'IMU varie d'une carte à l'autre (oscillateur interne) : +0,7 % sur G.
- L'horodatage suit cette dérive par la correction proportionnelle (`firmware/stick/src/imu.c`) :
  sur G, il alterne entre 4800 µs (période nominale) et ~4748 µs par paquet. L'erreur reste
  < 0,1 échantillon ; estimer la vraie période la ramènerait à quelques µs (à faire si la
  détection en a besoin).
- Les horloges des cartes (quartz 32 kHz) s'accordent à ~10 ppm près.

### Capteurs au repos

| | Baguette G | Baguette D |
|---|---|---|
| Norme accéléromètre | 1,017 g | 1,028 g |
| Bruit accéléromètre (x, y, z) | 12,7 / 3,3 / 4,8 mg | 3,5 / 3,9 / 4,7 mg |
| Biais gyro (x, y, z) | 0,00 / −1,42 / 0,48 °/s | 0,47 / −2,19 / −0,78 °/s |
| Bruit gyro (x, y, z) | 0,40 / 0,45 / **1,76** °/s | 0,75 / 0,46 / 0,38 °/s |
| Variation du biais vertical sur 10 min | 0,431 → 0,411 °/s | −1,465 → −1,452 °/s |
| Échantillons saturés, répétés | 0, 0 | 0, 0 |

- **Gyro Z de G** : une raie à 769 Hz porte 40 % de la puissance de bruit (absente sur D).
  Vibration (ventilateur, table) ou parasite propre à la carte : à vérifier en échangeant la
  place des deux baguettes.
- Le biais dérive lentement et d'un seul côté (~0,02 °/s en 10 min, mise en température
  probable).

### Dérive du lacet au repos

Biais calibré sur les N premières secondes, puis vitesse autour de la verticale intégrée sur
le reste de la session (`analyze_rest.py --calibration N`) :

| Calibration | Baguette G | Baguette D |
|---|---|---|
| 10 s | +5,4° en 10 min | +7,8° |
| 60 s | −8,3° | +5,4° |

- Ordre de grandeur : **5 à 8° en 10 min au repos**, soit 0,5 à 0,9 °/min, dominé par la dérive
  lente du biais et non par le bruit (une calibration plus longue n'aide pas). La recalibration
  à chaque immobilité (ARCHITECTURE.md) est donc nécessaire ; reste à mesurer la dérive pendant
  le jeu (10 min de jeu, TASK-012).

### Défauts trouvés pendant ces mesures

- **Paquets périmés en début de session** : le récepteur continuait à empiler des paquets
  pendant que le port était fermé, et le pilote CDC en garde encore quelques-uns ; à
  l'ouverture suivante, ils arrivaient en premier (jusqu'à 2 min d'âge), vus comme des milliers
  de pertes. Corrigé (firmware récepteur compilé, pas encore flashé sur le dongle) : le
  récepteur n'empile plus rien port fermé, le logger ignore les 200
  premières ms, `analyze_rest.py` écarte ces paquets dans les anciennes sessions.

## 2026-11-02 — retransmissions : causes (TASK-031)

Mêmes conditions (baguettes au repos, USB), sessions de 1 à 2 min, `analyze_rest.py`.
Les taux varient d'une session à l'autre (positions retouchées entre deux flashs, phase
relative des baguettes) : seules les comparaisons faites d'affilée sont fiables.

| Session | Configuration | Retrans. G | Retrans. D | Pertes D |
|---|---|---|---|---|
| 144325 | référence (récepteur A) | 36 % | 49 % | 0,8 % |
| 150001 | radio du récepteur en priorité 0 | 42 % | 58 % | 1,7 % |
| 151911 | sans montée radio rapide (3 cartes) | 53 % | 68 % | 2,3 % |
| 152341 | référence, deux baguettes | 49 % | 69 % | 3,7 % |
| 152555 | **G seule** | **3,9 %** | — | — |
| 152757 | **D seule** | — | **2,4 %** | **0** |

- Seule, chaque baguette retransmet peu (2 à 4 %) et ne perd rien : le récepteur, la priorité
  de l'USB et la montée radio rapide ne sont pas en cause (les deux essais ne font pas mieux).
- À deux, les retransmissions des baguettes sont corrélées seconde par seconde (0,4 à 0,66) :
  **collisions**. Chaque baguette envoie un paquet toutes les 4,8 ms, un échange (paquet + ACK)
  occupe ~0,75 ms ; les deux rythmes sont presque égaux (écart d'ODR 0,7 %), donc les baguettes
  glissent lentement l'une contre l'autre et se chevauchent pendant de longues périodes. Les
  retransmissions ajoutent de la charge et d'autres collisions : bien plus que les ~20 %
  estimés dans DECISIONS.md.
- La montée radio rapide reste activée (sans elle : pire).
- Impact : pertes de 0,6 à 3,7 % sur D en mode RAW (phase 0). En jeu (HIT + ORIENT à 100 Hz),
  la charge sera bien plus faible.

Essais de réglage (baguettes seulement, mêmes conditions) :

| Session | Réglage | G : retrans. / pertes / file pleine | D : retrans. / pertes / file pleine | RSSI G / D |
|---|---|---|---|---|
| 154201 | 8 retransmissions | 75 % / 0,38 % / 7 | 53 % / 0,18 % / 46 | −69 / −55 dBm |
| 154929 | 8 retrans. + file ESB de 16 | 79 % / 0,01 % / 0 | 127 % / 2,7 % / 558 | −66 / −71 dBm |

- Plus de retransmissions = plus de temps d'antenne = plus de collisions : à signal faible
  (D à −71 dBm), le lien s'engorge (plus de tentatives que de paquets). Pas de gain net ;
  **réglage d'origine conservé** (3 retransmissions, file de 8).
- Le RSSI varie de −52 à −71 dBm entre sessions (cartes déplacées à chaque double-tap) : cet
  effet est du même ordre que les réglages testés. Pour départager des réglages fins, il
  faudra des sessions plus longues et des cartes fixées.
- Solution de fond (prévue dans DECISIONS.md) : **créneaux synchronisés** — le récepteur
  décale une baguette d'une demi-période via les payloads d'ACK, les paquets alternent.

## 2026-11-03 — coups secs, saturation (TASK-012)

Session `session_20261103_153541` (75 s, `--save-raw`). Les deux baguettes imprimées
(`mech/stick`), câble USB branché, coups secs dans le vide de plus en plus forts, des deux
mains, puis roulements. Récepteur : dongle sur le PC.

| | Baguette G | Baguette D | Pleine échelle |
|---|---|---|---|
| Gyro max par axe (x / y / z) | 953 / 1224 / 1110 °/s | 803 / 1355 / 527 °/s | ±2000 °/s |
| Norme du gyro max | 1409 °/s | 1357 °/s | |
| Accéléro max par axe (x / y / z) | 3,6 / 12,5 / 10,8 g | 4,2 / 11,4 / 13,6 g | ±16 g |
| Échantillons saturés | 0 | 0 | |
| Paquets reçus | ~50 % | ~85 % | |

- **Pas de saturation du gyro** : 68 % de la pleine échelle au plus, la marge suffit pour des
  coups dans le vide. À revérifier en frappant un vrai pad (arrêt brutal), phase 1.
- **L'accéléromètre approche la saturation** : 13,6 g sur 16 g. La carte est au talon, loin du
  poignet : l'accélération centripète y est forte. Sans conséquence pour la vélocité (estimée sur
  le gyro), à surveiller si la détection utilise l'accéléro.
- Réserve : G a perdu la moitié de ses paquets, des pics ont pu être manqués ; D (85 % reçus)
  donne les mêmes ordres de grandeur.
- **Le lien radio ne tient pas en jeu** : réception de G entre 13 et 40 % pendant de longues
  périodes, D entre 75 et 96 %, RSSI −65 à −79 dBm (−59 dBm une fois posées). Trous jusqu'à 1,2 s.
  Collisions entre baguettes (TASK-031) aggravées par la baisse du signal en mouvement (main,
  corps). Conséquence : l'orientation ne peut pas être intégrée à travers ces trous ; la mesure
  de dérive en jeu se fait **une baguette à la fois** (seule, une baguette ne perd presque rien,
  TASK-031) en attendant les créneaux synchronisés (TASK-033).

## 2026-11-04 — deux baguettes au repos à +8 dBm, puis premier jeu

Session `session_20261104_143641` (10 min, `analyze_rest.py`), firmware 3 (+8 dBm, RAW).
Baguettes dans leur talon, à plat, alimentées en USB ; places et Wi-Fi différents du 2026-11-02.

| | G 02/11 (0 dBm) | G 04/11 (+8 dBm) | D 02/11 | D 04/11 |
|---|---|---|---|---|
| Paquets perdus | 0,001 % | **3,2 %** | 0,6 % | **1,6 %** |
| Retransmissions | 37 % | 71 % | 47 % | 58 % |
| RSSI moyen | −52 dBm | −68 dBm | −65 dBm | −61 dBm |
| ODR (horloge baguette) | +0,70 % | +0,71 % | −0,05 % | −0,04 % |
| Dérive du lacet (calib. 10 s) | +5,4° | −6,4° | +7,8° | −3,5° |
| Bruit gyro z | 1,76 °/s | 1,9 °/s | 0,38 °/s | 0,4 °/s |

- Pertes régulières sur toute la session : collisions entre baguettes. +8 dBm n'y change rien
  (attendu) ; la hausse par rapport au 02/11 n'est pas attribuable à la puissance (place, Wi-Fi).
- ODR, dérive au repos et bruit de G (TASK-032) confirment le 02/11.
- Firmware 4 de jeu (HIT seulement) : 30 paquets en 15 s au repos, 0 perdu, 0 retransmission ;
  en jeu sur un coussin, 39 coups reçus en 45 s, 0 perdu. Pics de 260 à 660 °/s (contre
  1360 °/s dans le vide le 03/11) : vélocité calée sur 800 °/s.

## Reste à mesurer (TASK-012)
- Dérive du lacet pendant 10 min de jeu.
- Pertes avec les deux baguettes en mouvement, sur batterie, à distance de jeu.

## 2026-11-06 — dérive en jeu, sur batterie (firmware 6 puis 7)

Protocole : baguettes posées 10 s à un endroit repéré, ~1 min de jeu, reposées au même endroit
(`tools/logger.py`, HIT + ORIENT + STATUS). Sessions `session_20261106_161259` (firmware 6) et
`session_20261106_163641` (firmware 7). Pas de saturation du gyro (aucun HIT avec le drapeau).

| | Firmware 6 G | Firmware 6 D | Firmware 7 G | Firmware 7 D |
|---|---|---|---|---|
| Lacet, posée avant → après | 23,0 → 21,2° | 2,7 → 6,2° | 20,8 → 19,1° | 15,8 → 14,9° |
| Azimut des coups (médianes par 5 s) | 17 à 92° | −31 à 4° | 29 à 35° (46 une fois) | −8 à −2° (−17 une fois) |
| Élévation des coups | ~+35° | ~+35° | +17 → +8° | +17 → +11° |

- La dérive du gyro lui-même est faible : 1 à 3,5° en une minute de jeu, posée à la main au même
  endroit (la précision de la pose est du même ordre).
- Firmware 6 : l'accéléromètre corrigeait aussi pendant les coups (norme proche de 1 g par
  moments). La carte est au talon, derrière la main : l'accélération centripète, le long de la
  baguette, passait pour une inclinaison. Élévation fausse (+35° au bas des coups), azimut qui
  glisse avec : c'est la dérive ressentie en ~30 s.
- Firmware 7 : correction seulement quand la baguette tourne à moins de 30 °/s. Azimut des coups
  stable à quelques degrés près sur la minute. L'élévation baisse encore de ~6° en une minute :
  à suivre (élévation vraie au moment des coups pas connue).
