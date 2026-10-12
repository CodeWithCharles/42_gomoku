# Handoff — reprise de session

Document de passation, pas de référence : il décrit l'état du dépôt au
**12 octobre 2026** et ce qu'il reste à faire. Les jalons J0-J7 et leurs
critères de sortie restent dans [WORKFLOW.md](WORKFLOW.md) ; ce fichier dit
seulement où on en est dedans. À supprimer une fois absorbé.

## Où on en est

**J0, J1 et J2 sont atteints.** Le module 4 (plomberie et arbitrage) est fini.

- `make` produit `Gomoku`, ne relink pas, compile sans avertissement
- `make test` : 81 tests, 258 assertions, 0 échec — en release **et** en
  `make debug` (ASan + UBSan)
- HTTP + WebSocket opérationnels (civetweb vendoré), protocole complet, partie
  jouable de bout en bout dans le navigateur, captures et fin de partie
  comprises
- les 13 clés de `StateEvent` et les 10 de `Stats` correspondent exactement à
  `ui/src/protocol.ts`
- `suggest` renvoie bien un coup sans modifier le plateau, et le chronomètre
  exigé par le sujet affiche le temps réel de la recherche (`0.45 s` pour un
  budget de `0.50 s`)

**Le chemin critique restant, c'est l'IA.** `make bench`, budget 450 ms :

```
  opening            depth  5     223890 nodes    450 ms     497533 n/s  DEPTH TOO LOW
  quiet midgame      depth  6     189717 nodes    450 ms     421593 n/s  DEPTH TOO LOW
  direct threat      depth  5      15067 nodes     44 ms     342432 n/s  DEPTH TOO LOW
  captures in play   depth  6     189079 nodes    450 ms     420176 n/s  DEPTH TOO LOW
```

Profondeur 5-6, le sujet en exige 10. Ça ne se gagne pas en optimisant les
nœuds/s : à facteur de branchement constant il faudrait ~1000 × plus de nœuds.
C'est le facteur de branchement effectif qu'il faut casser.

## À faire tout de suite

- [ ] `cd ui && npm run build` — `ui/dist` est encore l'ancien bundle, celui qui
      vide la page à la fin de partie. Les corrections de source sont faites
      (`src/vite-env.d.ts`, `GameContext.ts`, `tsconfig.node.json`,
      `StatusBar.tsx`) mais le bundle versionné, non. `vite build` vide `outDir`,
      donc les assets changent de hash : diff bruyant, c'est normal.
- [ ] ajouter `ui/*.tsbuildinfo` au `.gitignore` — deux fichiers traînent en
      non-suivis depuis l'ajout de `tsc -b` au script de build.

## Dette de documentation (introduite sciemment, à purger)

Trois endroits décrivent une architecture qu'on n'a plus :

- **ADR-003 « Zéro dépendance C++ tierce »** ([ARCHITECTURE.md:76](ARCHITECTURE.md))
  — faux. On a vendoré **civetweb v1.16** (MIT, un seul `.c`, tag épinglé parce
  que `master` ne compile pas) et **nlohmann/json v3.11.3** (MIT, un header).
  La raison du revirement : la plomberie HTTP/WS/JSON ne touche pas à l'intérêt
  du projet, qui est l'IA. À réécrire en ADR assumé, pas à effacer.
- **ADR-007 « Serveur mono-thread »** ([ARCHITECTURE.md:165](ARCHITECTURE.md))
  — faux, et mesuré comme tel : avec `num_threads 1` et une connexion WebSocket
  ouverte, le HTTP et un deuxième client WS partent tous les deux en timeout.
  On tourne en `num_threads 4`, et c'est un `std::mutex` dans `Session` qui
  sérialise les commandes (une recherche le garde pendant tout son budget ;
  `stop` est traité **hors** du verrou, exprès).
- **[INTEGRATION.md](INTEGRATION.md) étapes 1 à 3** — décrivent le serveur
  HTTP/WS écrit à la main, qui n'existe plus.

## Les trois chantiers

### 1. La recherche — profondeur 10 (J4, module 2)

Le seul point réellement bloquant pour la validation. Par ordre de rentabilité :

1. **table de transposition** — Zobrist est déjà là (`constinit`, 0
   initialiseur statique vérifié) ; reste la table, le remplacement et les
   bornes (`EXACT` / `LOWER` / `UPPER`)
2. **ordonnancement des coups** — PV en premier, puis killers, puis history
3. **PVS** (fenêtre nulle au-delà du premier coup)
4. **`movegen` plus sélectif** — c'est probablement là que se trouve le plus
   gros gain : couper les candidats hors menace au lieu d'élargir le voisinage

### 2. L'heuristique (J3, module 3)

`src/eval/eval.cpp` est naïf. C'est la moitié de ce qu'il faut savoir expliquer
en soutenance (le sujet n'exige d'expliquer que **le minimax et l'heuristique**).
À faire **avant** J4 : chercher à la profondeur 10 sur une évaluation fausse,
c'est chercher plus vite dans le vide.

### 3. Durcissement (J6, tous)

« Aucun crash, jamais » — sinon note 0.

- fuzz du protocole : JSON malformé, `idx` hors bornes, types inattendus,
  commandes inconnues, messages WebSocket fragmentés
- comportement en manque de mémoire
- fenêtre fermée en plein calcul → pas de moteur orphelin qui garde le port
- partie IA vs IA complète

## Petits points connus, non urgents

- [session.cpp:152](../backend/src/server/session.cpp) envoie le code
  d'erreur `"unkown"` (typo) ; `ErrorCode` dans `ui/src/protocol.ts` ne le
  contient pas. Une lettre.
- `Server::start()` ne sait pas distinguer « port occupé » de « `mg_start` a
  refusé les options » : le message `no free port from N` est trompeur. Le
  diagnostic exploitable passe par `mg_start2()` et son buffer d'erreur —
  civetweb ne route pas les erreurs de parsing d'options vers `log_message`.
- [PROTOCOL.md](PROTOCOL.md) « Maintien de connexion » annonce un ping toutes
  les 15 s et la fermeture d'un client muet depuis 45 s. **Ce n'est pas
  implémenté** : l'option civetweb `enable_websocket_ping_pong` vaut `"no"` par
  défaut et on ne la positionne pas. Soit on l'active, soit on corrige le
  document.
- `Server::broadcast()` est défini et jamais utilisé : `Session::pushState()`
  n'écrit qu'au client qui a envoyé la commande. Conforme au protocole (« un
  seul client »), mais deux onglets ouverts se désynchronisent silencieusement.
- `suggest` sur un plateau **vide** ne propose rien : le panneau de recherche
  reste à zéro et aucun coup n'est renvoyé (vraisemblablement zéro candidat
  renvoyé par `movegen` quand le plateau est vide). Sans conséquence en partie,
  mais un correcteur qui clique « Suggérer » en premier ne voit rien.
- l'arrêt prend ~1 s quand un client WebSocket est attaché (poignée de main de
  fermeture dans `mg_stop`), immédiat sans client.

## Commandes

```bash
cd backend && make          # compile Gomoku (binaire a la racine du depot)
cd backend && make test     # 81 tests, doit rester vert
cd backend && make bench    # profondeur atteinte et noeuds/s
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
  orphelins faites comme ça sont fausses.
