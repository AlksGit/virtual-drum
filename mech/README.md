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

Longueur 406 mm (7A), Ø 15 mm, talon ovale 22,5 × 14,5 mm autour de la carte. Trois pièces :

| Pièce | Taille | Rôle |
|---|---|---|
| `handle` | 213 mm | talon : logement de la XIAO puis de la LiPo, prise USB-C au bout, trous reset et LED dessus |
| `shaft` | 216 mm | fût et olive ; tenon Ø 9 × 25 mm qui entre dans le talon |
| `cap` | 22 × 15 mm | embout du talon : bloque la carte, laisse passer la prise USB-C |

Repère (celui de `docs/PROTOCOL.md`) : X le long de la baguette vers la pointe, Z côté composants
de la carte (LED, reset).

### Impression (réglages pour Bambu Lab P2S, non encore essayés, plateau 256 mm, buse 0,4)
Les STL sortent déjà dans le bon sens : baguette couchée le long de X, dessus de la carte (Z)
vers le haut. Ne pas les retourner : le méplat du tenon et le plafond du logement de la carte
sont alors des ponts courts, sans support.

Commencer par les pièces de test (`test_butt` + `cap`, `test_socket` + `test_spigot`, ~15 g en
tout) pour régler les jeux avant les grandes pièces.

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
  (`CLEARANCE`) et les picots de l'embout (`PEG_R`, trous `PEG_HOLE_R`), puis régénérer.
- `cap` : face extérieure sur le plateau, picots vers le haut, sans support.
- Masse attendue : ~45 g pour la baguette, carte comprise, batterie en plus (non pesée ; une 7A
  en bois pèse ~50 g).

### Montage
1. Essai à blanc du tenon dans le talon : il doit entrer sans forcer (jeu 0,15 mm). Le méplat du
   tenon fixe l'orientation et laisse sortir la colle en trop.
2. Coller le tenon (cyanoacrylate ou époxy).
3. Batterie (LiPo LP321323, 3,2 × 13 × 23 mm) : la souder sur les pads **BAT+ / BAT−** sous la
   carte. **Vérifier la polarité au multimètre avant** : la carte n'est pas protégée contre
   l'inversion. Coller un double face fin (1 mm au plus) sous la batterie et la glisser par le
   bout du talon, fils vers l'embout, jusqu'au fond de son logement (juste après la carte, côté
   pointe). Les fils passent dans la rainure au fond du logement de la carte.
4. Coller un adhésif mousse double face de **1 mm** sous la carte (côté sans composants), en deux
   bandes de part et d'autre des soudures de la batterie (la rainure centrale fait 6 mm), puis la
   glisser dans le talon, prise USB-C vers le bout : elle bute au fond du logement.
5. Enfoncer l'embout (picots en force). Il bloque la carte dans son logement. Il n'y a plus à le
   retirer pour flasher (`tools/flash_stick.py`). Pour sortir la carte : tournevis plat dans
   l'encoche de l'embout, sur le flanc, et faire levier doucement.
6. Charge : brancher l'USB-C de la baguette, la XIAO charge à 50 mA (`docs/HARDWARE.md`). Pour
   jouer en USB, attacher le câble avec un collier dans la gorge, à 76 mm du bout.

Sur le dessus du talon, près de l'embout : un trou Ø 2,5 mm au-dessus du bouton reset (trombone,
double appui pour le mode UF2) et, de l'autre côté de la prise, une fenêtre sur les LED.

### Réglages courants (`stick/stick.py`)
- `CLEARANCE`, `PEG_R` / `PEG_HOLE_R`, `JOINT_CLEARANCE` : jeux, selon l'imprimante.
- `BOTTOM_GAP` : épaisseur de l'adhésif sous la carte.
- `BATTERY_L`, `BATTERY_W`, `BATTERY_T` : cotes de la batterie. Celles de la fiche 80 mAh ;
  **mesurer les cellules 120 mAh au pied à coulisse avant d'imprimer**. `battery_wall_check()`
  vérifie qu'il reste 1,5 mm de paroi autour du logement (affiché, doit valoir 0).
- `SPLIT_X` : position de la coupe (les deux pièces restent sous 220 mm).
