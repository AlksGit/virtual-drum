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

- **Design de la batterie** (suite à ton message) : TASK-019 recentrée sur un rendu physique
  complet (PBR, ombres, reflets, HDR) et **nouvelle TASK-023** pour des modèles détaillés
  générés en code, au lieu de télécharger des modèles glTF. — Défaut pris : procédural. —
  Raison : contrôle total du rendu, aucune licence à gérer dans un repo public, pas de
  dépendance. Si tu préfères de vrais modèles (Blender ou téléchargés), dis-le.
- **Latence audio sur le PC (TASK-018)** : le pilote Realtek n'accepte pas moins de 480
  échantillons (~20 ms) en mode partagé, même en mode basse latence. — Défaut : on garde ça
  pour l'instant (le Mac avec CoreAudio devrait faire 64). — Pour la cible < 10 ms sous
  Windows : ASIO (ASIO4ALL + SDK Steinberg à compiler dans JUCE, à acter), ou une option
  « mode exclusif » (coupe le son des autres applis). Ton choix ?

- **Diviseur de tension batterie** : son ratio n'est ni dans le devicetree ni sur le wiki Seeed.
  — Défaut : la mesure de batterie reste non implémentée. — Il faut le schéma Seeed de la XIAO
  nRF52840 (PDF) ; si tu l'as, dépose-le dans `docs/` et je l'exploite.
- **Datasheet LSM6DS3TR-C** : le site ST bloque le téléchargement automatique ; les sensibilités
  et WHO_AM_I viennent du pilote officiel ST livré avec NCS. — Rien de bloqué ; la taille de FIFO
  reste à confirmer (PDF à déposer dans `docs/` si tu veux que je la vérifie).

- **Espace dans le chemin du repo** (`Personal work`) : Zephyr refuse les chemins avec espaces.
  — Défaut : build via `subst V:` (documenté dans `firmware/README.md`). — Plus simple à terme :
  déplacer le repo dans un chemin sans espace (ex. `C:/dev/virtual-drum`) ; ton choix.

## Créé pendant ton absence
(tâches ajoutées au backlog)
- TASK-023 : modèles procéduraux détaillés du kit.
- TASK-022 : CI GitHub Actions (build host Windows + macOS).

## À tester par toi
(son, ressenti, vérifs visuelles fines)
- **Vue 3D (TASK-016)** : lancer `Virtual Drum.exe` (ou `--fake 120`). Vérifié par captures sur le PC
  (60 fps, flashs, baguettes qui bougent, flèches pour le tempo). À juger par toi : le rendu
  général, la sensation de la caméra (glisser / molette / double-clic), et **le lancement sur
  le Mac** (le build Mac passe en CI mais l'app n'y a jamais tourné).
- **Rendu 3D (TASK-019)** : capture dans `docs/images/host-3d-view.png`. Direction artistique à
  valider : fûts laqués bleu à paillettes, cymbales bronze, studio sombre, liserés néon cyan /
  magenta, tapis à anneau cyan. Les couleurs sont des constantes faciles à changer.
- **Son (TASK-018)** : écouter l'app avec le batteur simulé. Les sons sont synthétisés en code
  (caisse claire, toms, charleston, crash, ride) : à juger à l'oreille, avec la latence perçue.
  M coupe le son.

## À tester avec le hardware
(étapes de validation sur cible)
- **Blinky (TASK-003)** : télécharger les UF2 depuis l'onglet Actions de GitHub (workflow
  « firmware », artefacts `stick-uf2` / `receiver-uf2`) ou builder en local, double-tap reset,
  copier `zephyr.uf2` : LED bleue qui clignote sur une baguette, verte sur le récepteur.
- **Lien USB (TASK-014)** : brancher le récepteur, lancer `vdrum_monitor --list` puis `vdrum_monitor` :
  le port doit être détecté (VID/PID 2FE3:0100), les débits par baguette affichés, 0 crc_errors.
  Débrancher pendant la lecture : le monitor doit afficher « Source stopped » sans planter.
  À faire sur PC **et** sur Mac (le code de lecture macOS n'a jamais tourné sur un vrai port).
- **IMU (TASK-006)** : flasher la baguette ; LED bleue qui clignote = échantillons reçus, rouge =
  pertes ou erreur I2C. À confirmer avec le lien radio (TASK-007) et `vdrum_monitor` : ~1666
  échantillons/s, ordre gyro/accéléro correct (accéléro ≈ 1 g au repos), pas de valeurs aberrantes
  (sinon la lecture FIFO multi-octets ne reboucle pas : passer à des lectures de 2 octets).
- **Récepteur (TASK-008)** : un seul port série doit apparaître (VID/PID 2FE3:0100, « Virtual Drum
  Receiver ») ; LED verte qui clignote lentement sans baguette, plus vite avec trafic, rouge si des
  trames sont jetées. Mesurer avec les deux baguettes en RAW que la LED rouge reste éteinte
  (sinon le débit USB, ~47 ko/s, est trop juste : piste `CONFIG_USBD_CDC_ACM_BUF_POOL`).
- **Logger Python (TASK-009)** : `python tools/logger.py --duration 30 --save-raw` avec le récepteur
  branché : CSV créés dans `recordings/`, ~1666 échantillons/s par baguette, pertes et crc affichés.
