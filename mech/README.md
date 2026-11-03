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
| `handle` | 213 mm | talon : logement de la XIAO, prise USB-C au bout, trous reset et LED dessus |
| `shaft` | 216 mm | fût et olive ; tenon Ø 9 × 25 mm qui entre dans le talon |
| `cap` | 22 × 15 mm | embout du talon : bloque la carte, laisse passer la prise USB-C |

Repère (celui de `docs/PROTOCOL.md`) : X le long de la baguette vers la pointe, Z côté composants
de la carte (LED, reset).

### Impression
- Les deux grandes pièces **à plat** (couches dans la longueur : bien plus solide que debout),
  supports arborescents sous la partie ronde. 4 périmètres, remplissage 20 %. PETG de
  préférence (résiste mieux aux chocs), PLA possible.
- `cap` : face extérieure sur le plateau, picots vers le haut.
- Masse attendue : ~45 g pour la baguette, carte comprise (une 7A en bois pèse ~50 g).

### Montage
1. Essai à blanc du tenon dans le talon : il doit entrer sans forcer (jeu 0,15 mm). Le méplat du
   tenon fixe l'orientation et laisse sortir la colle en trop.
2. Coller le tenon (cyanoacrylate ou époxy).
3. Coller un adhésif mousse double face de **1 mm** sous la carte (côté sans composants), puis la
   glisser dans le talon, prise USB-C vers le bout : elle bute au fond du logement.
4. Enfoncer l'embout (picots en force). Il bloque la carte dans son logement.
5. Attacher le câble USB le long de la baguette avec un collier dans la gorge, à 70 mm du bout.

Sur le dessus du talon, près de l'embout : un trou Ø 2,5 mm au-dessus du bouton reset (trombone,
double appui pour le mode UF2) et, de l'autre côté de la prise, une fenêtre sur les LED.

### Réglages courants (`stick/stick.py`)
- `CLEARANCE`, `PEG_CLEARANCE`, `JOINT_CLEARANCE` : jeux, selon l'imprimante.
- `BOTTOM_GAP` : épaisseur de l'adhésif sous la carte.
- `SPLIT_X` : position de la coupe (les deux pièces restent sous 220 mm).
