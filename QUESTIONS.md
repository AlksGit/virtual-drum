# Questions & things to check

Les agents ont travaillé en autonomie. Tout ce qui demande ton avis ou tes mains est regroupé
ici, pour y répondre d'un coup.

## Questions ouvertes
Format : question — choix par défaut pris — ce qui est bloqué / ce qui avance quand même.

- **LiPo** : capacité exacte, connecteur, protection intégrée ? — Défaut : rien de changé, le
  courant de charge garde la valeur sûre par défaut de la carte. — Bloque seulement le réglage du
  courant de charge, avec le hardware.

- **Config des agents en mode autonome** : le classifieur de sécurité de Claude Code a refusé que
  je modifie moi-même `.claude/commands/agents/*.md` pour autoriser merge/push automatiques.
  — Défaut : je n'ai pas touché ces fichiers ; j'ai enchaîné PM → Dev → Review dans la session sur
  ta consigne directe (merge `--no-ff` dans `devape` + push, jamais `main`). — À toi de décider si
  tu veux ajouter ces règles toi-même dans les configs pour les prochaines sessions.

- **Format RAW (TASK-005)** : décidé par défaut — payload ESB dynamique, 8 échantillons par paquet
  (105 o). Un échantillon par paquet de 32 o saturerait la radio (~119 % de temps d'antenne pour
  2 baguettes à 1,66 kHz). — Rien de bloqué. — Si tu préfères une autre ODR ou un autre lot,
  c'est une constante (`RAW_MAX_SAMPLES`) ; le calcul est dans DECISIONS.md.

## Créé pendant ton absence
(tâches ajoutées au backlog)
- TASK-022 : CI GitHub Actions (build host Windows + macOS).

## À tester par toi
(son, ressenti, vérifs visuelles fines)
- **Vue 3D (TASK-016)** : lancer `Virtual Drum.exe` (ou `--fake 120`). Vérifié par captures sur le PC
  (60 fps, flashs, baguettes qui bougent, flèches pour le tempo). À juger par toi : le rendu
  général, la sensation de la caméra (glisser / molette / double-clic), et **le lancement sur
  le Mac** (le build Mac passe en CI mais l'app n'y a jamais tourné).

## À tester avec le hardware
(étapes de validation sur cible)
- **Lien USB (TASK-014)** : brancher le récepteur, lancer `vdrum_monitor --list` puis `vdrum_monitor` :
  le port doit être détecté (VID/PID 2FE3:0100), les débits par baguette affichés, 0 crc_errors.
  Débrancher pendant la lecture : le monitor doit afficher « Source stopped » sans planter.
  À faire sur PC **et** sur Mac (le code de lecture macOS n'a jamais tourné sur un vrai port).
- **Logger Python (TASK-009)** : `python tools/logger.py --duration 30 --save-raw` avec le récepteur
  branché : CSV créés dans `recordings/`, ~1666 échantillons/s par baguette, pertes et crc affichés.
