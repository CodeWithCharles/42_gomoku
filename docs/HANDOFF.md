# Handoff — reprise de session

Document de passation, pas de référence : il décrit l'état du dépôt au
**12 octobre 2026** et ce qu'il reste à faire. Les jalons J0-J7 et leurs
critères de sortie restent dans [WORKFLOW.md](WORKFLOW.md) ; ce fichier dit
seulement où on en est dedans. À supprimer une fois absorbé.

## Où on en est

**J0 à J3 sont atteints, J4 l'est presque, J6 est bien entamé.**

- `make` produit `Gomoku`, ne relink pas, compile sans avertissement
- `make test` : 92 tests, 0 échec, en release **et** en `make debug`
- **J3** : évaluation par table de motifs (65 536 entrées, construite au
  premier appel, classement par niveaux : quatres, puis trois au sens du
  free three du sujet, puis deux), règles de trait sur les quatres et la
  capture gagnante, captures et menaces de capture comptées ; `moveScore` lit
  la même table (attaque + défense + capture immédiate)
- **J4** : `movegen` partant des pierres, table de transposition (Zobrist +
  paires capturées, bornes, scores de mat recalés), faisceau adaptatif (20
  candidats à la racine, −3 par niveau, 5 au minimum, coups forcés seuls quand
  un cinq menace), PVS, bitboards par ligne (`Board::window` sans boucle)
- **temps** : `budgetMs` est une moyenne gérée par `TimeBank` (PROTOCOL.md,
  « Budget de temps »)
- **serveur** : un thread de travail possède la partie, `stop` agit
  immédiatement, l'IA contre IA est jouable et interruptible (ADR-007)

`make bench` (positions de début de partie, les plus dures) :

```
  opening            depth  8     344260 nodes    450 ms     765022 n/s  DEPTH TOO LOW
  quiet midgame      depth  9     290561 nodes    450 ms     645691 n/s  DEPTH TOO LOW
  direct threat      depth  3        234 nodes      0 ms          0 n/s  DEPTH TOO LOW
  captures in play   depth 10     279373 nodes    450 ms     620829 n/s  OK
```

`make bench-games` (8 parties moteur contre moteur, ce que le sujet juge) :
**137 ms en moyenne**, pire moyenne courante 450 ms, coup le plus lent 714 ms,
**96 % des coups non forcés à la profondeur 10**, 25 % de coups où un gain ou
une perte est prouvé (la recherche s'arrête alors tôt, « direct threat » en
est un). Ce qui reste sous 10 : les 3-4 premiers coups de l'IA.

Mesurées puis **écartées** (gain nul ou négatif, détails dans l'historique de
session) : coups killer (le bonus change les coups gardés par le faisceau),
bonus de trait dans l'éval (aucun nœud gagné), réductions de fin de liste
(8-8 en match, pas de gain en partie réelle).

## À faire tout de suite

Côté interface et coque, tout est listé et priorisé dans
[FRONT.md](FRONT.md) : chronomètre en moyenne, lancement d'une partie IA
contre IA, Stop actif quand l'IA a le trait, `ui/dist` à reconstruire,
`ui/*.tsbuildinfo` commités par erreur, bascule de `make app` sur
`./Gomoku`.

## Documentation

Remise à jour le 12 octobre : README, ARCHITECTURE (ADR-002, 003, 004, 007 et
chemin critique mesuré), INTEGRATION (réécrit : il décrit le branchement réel
sur civetweb au lieu d'un serveur écrit à la main), PROTOCOL, ELECTRON,
WORKFLOW, RULES ; création de [FRONT.md](FRONT.md).

## Ce qui reste

### Recherche (J4, module 2)

Le sujet exige 10 niveaux ; on les atteint sur 96 % des coups non forcés, pas
encore sur les premiers coups. Pistes non essayées : réutiliser la table de
transposition d'un coup à l'autre en vieillissant les entrées, une éval
incrémentale (6 % du temps seulement, faible priorité), un livre
d'ouverture minimal.

Le propriétaire du module 2 doit pouvoir expliquer le faisceau : c'est lui
qui donne la profondeur, et il change ce que l'IA regarde.

### Durcissement (J6, tous)

Fait : `make fuzz` (JSON malformé, types inattendus, nombres hors bornes,
trames brutes, message sans fin, rafales pendant une partie IA contre IA,
déconnexions en pleine recherche, huit clients), vert sous ASan/UBSan et sous
`-fsanitize=float-cast-overflow` ; ThreadSanitizer sans course dans notre code ;
arrêt propre par SIGTERM en pleine partie.

Reste : comportement réel en manque de mémoire (les allocations du chemin
réseau sont protégées et la table de transposition s'en passe, mais rien n'a
été mesuré sous `ulimit -v`), et la fenêtre Electron fermée en plein calcul.

### Interface (module 4, front)

Voir [FRONT.md](FRONT.md).

## Petits points connus, non urgents

- `Server::start()` ne sait pas distinguer « port occupé » de « `mg_start` a
  refusé les options » : le message `no free port from N` est trompeur. Le
  diagnostic exploitable passe par `mg_start2()` et son buffer d'erreur —
  civetweb ne route pas les erreurs de parsing d'options vers `log_message`.
- `Server::broadcast()` est défini et jamais utilisé : `Session::pushState()`
  n'écrit qu'au client qui a envoyé la commande. Conforme au protocole (« un
  seul client »), mais deux onglets ouverts se désynchronisent silencieusement.
- l'arrêt prend 1 à 2 s quand des clients WebSocket sont attachés (poignée de
  main de fermeture dans `mg_stop`), immédiat sans client.
- `ThreadSanitizer` signale une course **dans** `mg_stop` (civetweb lit son
  drapeau d'arrêt sans atomique). Bénin, hors de notre code.
- `suggest` sur un plateau vide cherche jusqu'à la profondeur 10 un unique coup
  possible (le centre) : ~176 000 nœuds pour rien, sans conséquence.

## Commandes

```bash
cd backend && make          # compile Gomoku (binaire a la racine du depot)
cd backend && make test     # 92 tests, doit rester vert
cd backend && make bench    # profondeur atteinte et noeuds/s, positions fixes
make bench-games            # parties moteur contre moteur, ~40 s
make fuzz                   # fuzz du protocole (node), apres make debug pour ASan
cd backend && make debug    # reconfigure en ASan + UBSan, puis compile
cd ui && npm run build      # reconstruit ui/dist
./Gomoku --no-browser --port 8642
```

`make test` et `make bench` sont les deux garde-fous : à lancer après toute
modification du moteur.

## Pièges déjà payés, à ne pas repayer

- `backend/Makefile` : les trois cibles de link ont besoin de `$(OBJS_C)`, sinon
  `civetweb.c` n'est jamais compilé et le link échoue sur les `mg_*`.
- `Score` doit rester **signé**. En `uint32_t`, `-kScoreInf` devient
  4 292 967 296, `alpha < 0` est faux, et l'alpha-bêta ne garde plus jamais un
  bon coup. Silencieux, aucun crash.
- `pkill -f <motif>` attrape son propre shell : les mesures de processus
  orphelins faites comme ça sont fausses. Utiliser `pgrep -x Gomoku`.
- civetweb lit les trames d'une connexion **une par une** : tant qu'un
  gestionnaire tourne, la trame suivante de la même socket n'est pas lue. Ne
  jamais faire de calcul long dans un gestionnaire (ADR-007).
- `Engine::search` remet son drapeau d'arrêt à `false` en démarrant : un arrêt
  demandé juste avant est perdu. `Session::think` le réarme depuis le callback
  de progression.
- `json::value<int>` convertit `1e30` ou `2.5e9` en `int` directement :
  comportement indéfini, que UBSan de GCC ne signale **pas** par défaut
  (`float-cast-overflow` n'est pas dans `-fsanitize=undefined`). Lire les
  nombres par `intField` dans `session.cpp`.
- la table de motifs ne peut pas être `constexpr` : il faudrait entre 134 M et
  268 M d'opérations constexpr, GCC en autorise 33 M.
