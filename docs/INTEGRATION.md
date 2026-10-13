# Le moteur C++ branché sur l'interface

> Comment `backend/src/net/` et `backend/src/server/` raccordent le moteur à
> l'interface, tel que c'est construit aujourd'hui. Pour le *pourquoi*, voir
> ADR-003, ADR-004 et ADR-007 dans [ARCHITECTURE.md](ARCHITECTURE.md). Pour le
> détail du protocole, [PROTOCOL.md](PROTOCOL.md). Pour la coque,
> [ELECTRON.md](ELECTRON.md).
>
> Ce document était à l'origine un guide pour écrire HTTP, SHA-1 et le cadrage
> RFC 6455 à la main. Ce plan a été abandonné au profit de civetweb (ADR-003) ;
> l'historique git garde l'ancienne version.

## Ce qui existe

| élément | état |
|---|---|
| `ui/` interface React, goban, chronomètre, panneau de recherche | écrit, compilé dans `ui/dist` |
| `ui/src/protocol.ts` types du protocole | écrit, **fait foi** |
| `app/` coque Electron, les trois canaux | écrit, lancé sur le moteur factice par `make app` |
| `tools/mock-engine.mjs` moteur factice | écrit, en retard sur le vrai moteur (voir [FRONT.md](FRONT.md)) |
| `backend/src/net/server.cpp` HTTP + WebSocket sur civetweb | écrit |
| `backend/src/server/session.cpp` protocole, thread de partie, temps | écrit |
| `backend/tests/fuzz_protocol.mjs` fuzz du protocole (`make fuzz`) | écrit |

**L'interface ne doit pas bouger pour faire passer le C++.** Si un écart
apparaît, c'est le C++ qui s'écarte du contrat. Inversement, toute évolution du
protocole touche `session.cpp`, `ui/src/protocol.ts` et `PROTOCOL.md` **dans le
même commit**.

## Le contrat, en six points non négociables

1. Le moteur écoute sur **`127.0.0.1`**, jamais sur toutes les interfaces.
2. Il imprime `GOMOKU_READY port=N\n` sur stdout **suivi d'un `fflush`**, juste
   après un démarrage réussi du serveur.
3. Il sert `ui/dist` en HTTP sur ce port, et ne promeut que `/ws`.
4. Il pousse un `state` **complet** à chaque changement : 361 entiers, pas de
   diff, pas d'accusé de réception.
5. Il ne ferme jamais la socket sur une commande fautive : il répond `error`.
6. Aucune exception ne traverse un callback civetweb, aucun calcul long n'y
   tourne.

## Découpage des fichiers

`backend/src/net/` **ne connaît pas le Gomoku** : aucun `Board`, aucun
`Player`, aucun `Game` dans ces fichiers.

```
backend/src/net/
  server.cpp     civetweb : demarrage, choix du port, fichiers statiques,
                 promotion de /ws, recollage des fragments, envoi sous verrou

backend/src/server/
  session.cpp    JSON, file de commandes, thread de partie, banque de temps
```

`session.cpp` est le seul point de contact entre le réseau et le jeu. Si une
règle du Gomoku vous démange dans ce fichier, elle va dans
`backend/src/core/rules.cpp`.

## Le parcours d'une commande

```
thread civetweb (un par socket ouverte)      thread de partie (un seul)
---------------------------------------      --------------------------
trame texte, fragments recolles
  -> {"type":"play","idx":180}
  -> depot dans la file, retour immediat ---> Game::play()        coup humain
                                              send(state "move")
                                              file <- tour de l'IA
                                              Engine::search(...)
                                                progress profondeur 1
  -> {"type":"stop"}                            progress profondeur 2
  -> drapeaux atomiques, retour immediat        ... la recherche s'arrete
                                              Game::play()        meilleur coup trouve
                                              send(state "ai_move")
```

- **Les gestionnaires civetweb ne touchent jamais la partie.** civetweb lit
  les trames d'une connexion une par une : une recherche lancée dans un
  gestionnaire empêcherait de lire le `stop` qui la suit.
- **Un seul thread possède `Game`, `Engine` et la banque de temps**, donc
  aucun verrou n'est nécessaire autour de la partie.
- **`stop` n'entre pas dans la file** : il agit tout de suite, par le drapeau
  d'arrêt du moteur et par `halted_`.
- **Chaque coup de l'IA est une tâche** : entre deux coups d'une partie IA
  contre IA, un `new-game` ou un nouvel onglet passent.
- **Les `progress` partent du thread de partie** via `Server::sendText`, qui
  écrit sous le verrou des clients pour qu'une connexion fermée au même instant
  ne soit pas libérée en pleine écriture.

Le détail et l'historique de ce modèle sont dans ADR-007.

## Cycle de vie

Pour ne pas laisser d'orphelin quand Electron est tué brutalement, `main.cpp`
fait :

```cpp
::prctl(PR_SET_PDEATHSIG, SIGTERM);
// Course : si le parent est mort entre le fork et le prctl, on a ete adopte
// par init et le signal ne viendra jamais. On le detecte apres coup.
if (::getppid() == 1) return 1;
```

plus un fil watchdog qui lit `stdin` jusqu'à EOF, **activé uniquement avec
`--parent-watchdog`** : lancé depuis un terminal avec `stdin` sur `/dev/null`,
`read()` rendrait `0` immédiatement et le moteur s'arrêterait aussitôt.

SIGTERM arrête le serveur, interrompt la recherche en cours et rejoint le
thread de partie : 0,5 s sans client, 1 à 2 s avec des sockets ouvertes (la
poignée de main de fermeture de `mg_stop`).

## Vérifier le branchement

```bash
make
./Gomoku --no-browser --port 8642 | head -1   # GOMOKU_READY port=8642, tout de suite
make fuzz                                       # protocole sous attaque (node)
make debug && make fuzz && make release         # idem sous ASan + UBSan
```

Lancer deux moteurs : le second annonce `8643`. Ouvrir
`http://127.0.0.1:8642/` : le bandeau passe à « moteur connecté », un clic pose
une pierre, l'IA répond, le chronomètre avance pendant qu'elle réfléchit.

## Bascule de la coque vers le vrai binaire

Pas encore faite (voir [FRONT.md](FRONT.md)) :

1. `app/package.json` : le script `start` (sans `GOMOKU_ENGINE`) devient celui
   qu'on utilise ; `start:mock` reste pour travailler l'interface.
2. `Makefile` racine : `app` dépend de `all` et lance `npm start` au lieu de
   `npm run start:mock`. Voir la recette de [ELECTRON.md](ELECTRON.md).

Rien à changer dans `ui/`.

## Pièges recensés

| symptôme | cause |
|---|---|
| Electron attend puis affiche un timeout, alors que le moteur tourne | `fflush(stdout)` oublié après `GOMOKU_READY` |
| fenêtre blanche / `ERR_CONNECTION_REFUSED` | `loadURL` avant l'annonce du port |
| le bouton Stop n'arrête rien | une recherche lancée dans un gestionnaire civetweb (ADR-007) |
| un onglet de plus ne reçoit plus rien, pas même `index.html` | pool de threads civetweb trop petit : chaque socket ouverte en garde un |
| la socket se rouvre en boucle | le moteur ferme la connexion au lieu de la garder ouverte |
| la socket échoue en `400` en dev | proxy Vite sans `ws: true` |
| le moteur s'arrête aussitôt démarré | watchdog stdin actif sans le garde `--parent-watchdog` |
| au 2ᵉ lancement, port différent puis échec | moteur orphelin du lancement précédent |
| `make app` ne fait rien | `.PHONY` manquant, `app` est aussi un nom de dossier |

## Définition de terminé

- [x] `./Gomoku --no-browser` puis un navigateur sur le port : partie jouable
  de bout en bout, chronomètre visible, panneau de recherche alimenté.
- [x] Trames tronquées, aberrantes ou fragmentées sans fin : aucun crash
  (`make fuzz`, sous ASan/UBSan).
- [x] `stop` arrête la recherche en cours, y compris en IA contre IA.
- [x] `make test` vert.
- [ ] `make app` : même chose dans la fenêtre Electron sur `./Gomoku`, sans
  orphelin à la fermeture.
- [ ] Tuer le moteur pendant une partie : l'interface affiche « moteur
  arrêté », et se reconnecte seule si on le relance (à revérifier sur le vrai
  binaire).
