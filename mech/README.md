# Mécanique

Pièces imprimées, décrites par des scripts **CadQuery** (Python) : chaque cote est une constante
en tête de script, les pièces sont régénérées en une commande.

```
python -m pip install -r mech/requirements.txt    # dans le venv du projet
python mech/stick/stick.py                        # -> mech/stick/out/*.stl, *.step
```

`mech/vendor/` (non commité, licence non claire) contient le modèle 3D de la carte,
`XIAO-nRF52840 v15.step`. S'il est présent, le script place la carte dans la baguette et vérifie
qu'elle ne touche aucune pièce (volume d'interférence affiché, doit valoir 0).

## Baguette (`stick/`)

Longueur 406 mm (7A), Ø 15 mm, talon ovale 22,5 × 14,5 mm sur 48 mm autour de la carte et de la
batterie. Trois pièces :

| Pièce | Taille | Rôle |
|---|---|---|
| `handle` | 215 mm | talon : carte puis batterie, ouvertes par-dessous ; prise USB-C au bout, trous reset et LED dessus |
| `lid` | 68 mm | couvercle sous le talon, tenu par 2 vis : porte la carte par les bords, passage des fils |
| `shaft` | 216 mm | fût et olive ; tenon Ø 9 × 25 mm qui entre dans le talon |

Repère (celui de `docs/PROTOCOL.md`) : X le long de la baguette vers la pointe, Z côté composants
de la carte (LED, reset).

### Impression (réglages pour Bambu Lab P2S, non encore essayés, plateau 256 mm, buse 0,4)
Les STL sortent dans le repère de la baguette (couchée le long de X, dessus de la carte vers +Z).
- `handle` : **le retourner** dans le slicer (logements vers le haut) : rien à soutenir à
  l'intérieur, seul le dessus rond (alors en bas) prend des supports.
- `lid` : **face plate sur le plateau** (retourné aussi) ; les creux sous la carte et la
  batterie sont des ponts de 15 mm, sans support.
- `shaft` : tel quel.

Commencer par les pièces de test (`test_butt` + `lid`, `test_socket` + `test_spigot`) pour régler
les jeux avant les grandes pièces. `test_butt` est le bout du talon, jusqu'après le couvercle :
carte, batterie et vis s'y essaient pour de vrai.

Réglages Bambu Studio, noms de l'interface anglaise. Panneau **Process** à gauche, activer
**Advanced** (interrupteur en haut du panneau) pour voir tous les réglages :
| Onglet › section | Réglage | Valeur | Pourquoi |
|---|---|---|---|
| Quality › Layer height | Layer height | 0.16 mm | surface ronde plus lisse, ponts plus propres |
| Quality › Seam | Seam position | Aligned | couture sur le flanc (outil *Seam painting* si besoin) |
| Quality › Precision | X-Y hole compensation | 0 | à régler avec les pièces de test |
| Strength › Walls | Wall loops | 4 | la solidité d'un tube vient des parois |
| Strength › Top/bottom shells | Top / Bottom shell layers | 5 / 5 | |
| Strength › Sparse infill | Sparse infill density / pattern | 20 % / Gyroid | |
| Support | Enable support | oui | |
| Support | Type | tree(auto) | |
| Support | Threshold angle | 30° | seul le dessous rond a besoin d'appui |
| Support | On build plate only | oui | rien dans le logement ni dans l'emboîture |
| Support › Advanced | Top Z distance | 0.2 mm (PLA), 0.24 mm (PETG) | décollage facile |
| Support › Filament for Supports | Support/raft interface | PLA (si AMS et pièce en PETG) | se décolle seul du PETG |
| Others › Bed adhesion | Brim type | No-brim | la pièce repose sur les supports |

En haut de l'onglet **Prepare** : **Plate type** › *Textured PEI Plate*.

- Matière : **PETG** (PETG HF) de préférence, résiste mieux aux chocs. PLA possible.
- Pièces de test : le tenon doit entrer à la main sans forcer et sans jeu visible. Trop serré →
  augmenter `JOINT_CLEARANCE` de 0,05 ; trop lâche → la diminuer. Même chose pour la carte
  (`CLEARANCE`), puis régénérer. Avant-trous des vis : `PILOT_D` (2,7 mm) ; plus grand si la vis
  force trop, plus petit si elle ne mord pas.
- Le fût (`shaft`) ne change pas avec le couvercle (2026-11-06) : celui déjà imprimé reste bon.
- Masse attendue : ~45 g pour la baguette, carte comprise, batterie en plus (non pesée ; une 7A
  en bois pèse ~50 g).

### Montage
Tout se fait à découvert, talon retourné (logements vers le haut).
1. Essai à blanc du tenon dans le talon : il doit entrer sans forcer (jeu 0,15 mm). Le méplat du
   tenon fixe l'orientation et laisse sortir la colle en trop. Coller le tenon (cyanoacrylate ou
   époxy).
2. Batterie (LiPo LP321323) soudée sur les pads **BAT+ / BAT−** sous la carte, **polarité vérifiée
   au multimètre** (la carte n'est pas protégée contre l'inversion).
3. Coller un petit carré de mousse adhésive de **1 mm** sur le blindage métallique de la carte
   (le plus grand composant), pas sur les LED.
4. Poser la carte dans son logement, composants vers le fond, prise USB-C dans l'encoche du bout.
5. Coller un double face fin sur la batterie, la poser dans son logement (après la cloison,
   côté pointe) et appuyer : elle tient au fond. Les fils restent à plat sur le dessous de la
   carte et passent la cloison sur le côté (rainure du couvercle) ; le surplus se replie dans
   l'espace de 5 mm avant la batterie.
6. Poser le couvercle : il porte la carte par les bords de son PCB, les fils et les soudures
   logent dans son creux. Visser **2 vis autotaraudeuses Ø 3 mm, 10 mm, tête cylindrique (Ø 6 mm
   au plus)** : trous Ø 3 mm dans le couvercle, avant-trous Ø 2,7 mm dans le talon (6 mm de
   filet). Serrer sans forcer.
7. Charge : brancher l'USB-C de la baguette, la XIAO charge à 50 mA (`docs/HARDWARE.md`). Pour
   jouer en USB, attacher le câble avec un collier dans la gorge, à 94 mm du bout.

Sur le dessus du talon, près du bout : un trou Ø 2,5 mm au-dessus du bouton reset (trombone,
double appui pour le mode UF2) et, de l'autre côté de la prise, une fenêtre sur les LED.

### Réglages courants (`stick/stick.py`)
- `CLEARANCE`, `JOINT_CLEARANCE`, `PILOT_D` / `HOLE_D` : jeux, selon l'imprimante.
- `BATTERY_PLAY` : jeu de la batterie dans son logement, au total dans chaque dimension (1,5 mm).
- `TOP_FOAM` : mousse sur le blindage ; `UNDER_GAP` : creux du couvercle sous la carte (fils).
- `SCREW_LEN`, `HEAD_D`, `HEAD_H` : vis du couvercle ; le script affiche la longueur de filet
  dans le talon et l'épaisseur sous la tête.
- `BATTERY_L`, `BATTERY_W`, `BATTERY_T` : cotes de la batterie (fiche 80 mAh, jugées bonnes pour
  les 120 mAh). `battery_wall_check()` vérifie qu'il reste 1,5 mm de paroi autour du logement
  (affiché, doit valoir 0).
- `SPLIT_X` : position de la coupe (les deux pièces restent sous 220 mm).
