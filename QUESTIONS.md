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

## Créé pendant ton absence
(tâches ajoutées au backlog)
- TASK-022 : CI GitHub Actions (build host Windows + macOS).

## À tester par toi
(son, ressenti, vérifs visuelles fines)

## À tester avec le hardware
(étapes de validation sur cible)
- **Lien USB (TASK-014)** : brancher le récepteur, lancer `vdrum_monitor --list` puis `vdrum_monitor` :
  le port doit être détecté (VID/PID 2FE3:0100), les débits par baguette affichés, 0 crc_errors.
  Débrancher pendant la lecture : le monitor doit afficher « Source stopped » sans planter.
  À faire sur PC **et** sur Mac (le code de lecture macOS n'a jamais tourné sur un vrai port).
