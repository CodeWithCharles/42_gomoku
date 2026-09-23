# Brancher le moteur C++ sur l'interface

> Mode d'emploi du raccordement `src/net/` + `src/server/` à l'interface qui
> existe déjà. Pour le *pourquoi*, voir ADR-003, ADR-004 et ADR-007 dans
> [ARCHITECTURE.md](ARCHITECTURE.md). Pour le détail du protocole,
> [PROTOCOL.md](PROTOCOL.md). Pour la coque, [ELECTRON.md](ELECTRON.md).

## Ce qui existe déjà, et qu'il ne faut pas refaire

| élément | état |
|---|---|
| `ui/` interface React, goban, chronomètre, panneau de recherche | écrit, compilé dans `ui/dist` |
| `ui/src/protocol.ts` types du protocole | écrit |
| `app/` coque Electron, les trois canaux | écrit |
| `tools/mock-engine.mjs` moteur factice | écrit, sert de référence exécutable |
| `src/net/`, `src/server/` | **à écrire — objet de ce document** |

`tools/mock-engine.mjs` implémente le même protocole sur le fil : handshake,
démasquage XOR, cadrage, séquence d'événements. En cas de doute sur un octet,
c'est la référence à comparer, pas à interpréter.

**L'interface ne doit pas bouger.** Si vous êtes tenté de modifier `ui/src`
pour faire passer le C++, c'est le C++ qui s'écarte du contrat.

## Le contrat, en six points non négociables

1. Le moteur écoute sur **`INADDR_LOOPBACK`**, jamais `INADDR_ANY`.
2. Il imprime `GOMOKU_READY port=N\n` sur stdout **suivi d'un `fflush`**, juste
   après un `listen()` réussi et avant la boucle.
3. Il sert `ui/dist` en HTTP sur ce port, et ne promeut que `/ws`.
4. Toute trame client est **masquée** (XOR 4 octets) et doit être démasquée.
5. Il pousse un `state` **complet** à chaque changement : 361 entiers, pas de
   diff, pas d'accusé de réception.
6. Il ne ferme jamais la socket sur une commande fautive : il répond `error`.

## Découpage des fichiers

La règle de dépendance d'`ARCHITECTURE.md` est stricte : **`src/net/` ne
connaît pas le Gomoku**. Aucun `Board`, aucun `Player`, aucun `Game` dans ces
fichiers.

```
src/net/
  sha1.cpp       SHA-1, pour le handshake uniquement
  base64.cpp     encodage seul
  http.cpp       ligne de requete, en-tetes, fichiers statiques
  ws_frame.cpp   cadrage RFC 6455 : decodage, encodage, masquage
  server.cpp     socket, boucle poll(), promotion de /ws

src/server/
  session.cpp    JSON, dispatch des commandes, serialisation des evenements
```

`src/server/session.cpp` est le seul point de contact entre le réseau et le
jeu. Si une règle du Gomoku vous démange dans ce fichier, elle va dans
`src/core/rules.cpp`.

## Étapes, avec leur critère d'acceptation

Chaque étape se valide seule. Ne passez pas à la suivante sans son critère.

### Étape 0 — CLI et annonce du port

Options à accepter : `--port N`, `--no-browser`, `--ui <chemin>`,
`--parent-watchdog`. Le port demandé, ou le suivant s'il est occupé.

```cpp
// Juste apres un listen() reussi, AVANT d'entrer dans poll().
std::printf("GOMOKU_READY port=%u\n", server.port());
std::fflush(stdout);
```

**Critère** :

```bash
./Gomoku --no-browser --port 8642 | head -1
# GOMOKU_READY port=8642, immediatement, pas apres un Ctrl-C
```

Lancez-en deux : le second doit annoncer `8643`, avec **une seule** ligne
chacun.

### Étape 1 — HTTP statique

Servir `--ui` (défaut `ui/dist`). Types MIME utiles : `.html`, `.js`, `.css`,
plus `.svg`/`.png` si vous en ajoutez. Repli sur `index.html`. Refuser tout
chemin qui sort de la racine servie.

**Critère** :

```bash
./Gomoku --no-browser --port 8642 --ui ui/dist &
curl -s -o /dev/null -w '%{http_code} %{content_type}\n' http://127.0.0.1:8642/
curl -s http://127.0.0.1:8642/ | head -1     # <!doctype html>
```

Ouvrir la page dans un navigateur : le goban doit s'afficher, avec le bandeau
« déconnecté, nouvelle tentative » — c'est normal, `/ws` n'existe pas encore.

### Étape 2 — Handshake WebSocket

Sur `GET /ws` avec `Upgrade: websocket` :

```
accept = base64( sha1( Sec-WebSocket-Key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11" ) )
```

La constante est celle de la RFC, elle ne change jamais. SHA-1 et base64 sont
à écrire à la main (ADR-003). Testez SHA-1 contre les vecteurs de la RFC 3174
avant de l'utiliser ici : un handshake qui échoue et un SHA-1 faux se
ressemblent beaucoup.

Réponse :

```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: <accept>
```

**Critère** : le bandeau de l'interface passe à « moteur connecté ». Ou, sans
interface :

```bash
websocat ws://127.0.0.1:8642/ws
```

### Étape 3 — Cadrage RFC 6455

C'est la couche où un bug donne un 0 au projet. Elle est pure, sans I/O :
écrivez-la avec ses tests unitaires.

À gérer :

- **démasquage** : `payload[i] ^= mask[i & 3]`, clé de 4 octets avant la charge ;
- **longueurs** sur 7, 16 puis 64 bits. Un `state` fait quelques kilo-octets,
  donc le cas 16 bits est le cas courant, pas un cas rare ;
- **réassemblage** : `read()` rend des octets, pas des trames. Tampon
  persistant, on consomme tant qu'une trame est complète, on garde le reste ;
- **trames de contrôle** : `ping` (0x9) → répondre `pong` (0xA) avec la même
  charge utile, `close` (0x8), et une trame de contrôle peut s'intercaler entre
  deux fragments d'un message. Charge utile ≤ 125 octets ;
- en émission : jamais de masquage, jamais de fragmentation, opcode texte 0x1.

**Critère** : tests unitaires sur trame tronquée, longueur 16 bits, longueur
annoncée aberrante, deux trames dans un même buffer, trame coupée en trois.
Aucun de ces cas ne doit crasher ni boucler.

### Étape 4 — Session JSON

`src/server/session.cpp` lit le champ `type` et dispatche. Commandes et
événements : [PROTOCOL.md](PROTOCOL.md), rien de plus, rien de moins.

Le JSON est asymétrique, et c'est une bonne nouvelle. En **lecture** : un
`type`, parfois un `idx` entier, parfois trois entiers pour `limits`. Pas
besoin d'un parseur générique. En **écriture** : tout est généré, un
`std::string` construit à la main suffit.

Ordre de travail conseillé :

1. `state` à l'ouverture de la connexion (`event: "connected"`), plateau vide ;
2. `play` → `Game::play()` → `state` (`event: "move"`) ;
3. les `error` : `out_of_bounds`, `occupied`, `game_over`, `not_your_turn`,
   `double_three`, `unknown`. La socket **reste ouverte** ;
4. `new-game`, `undo`, `limits`, `weights` ;
5. `suggest`, `stop`.

Conventions à respecter à la lettre : `idx = y * 19 + x` dans `[0, 361)`,
`-1` pour « aucune » ; couleurs `"black"` / `"white"` ; durées en ms ;
`board[i]` vaut `0` vide, `1` noir, `2` blanc.

**Rappel de synchronisation** : `src/server/session.cpp`, `ui/src/protocol.ts`
et `PROTOCOL.md` changent **dans le même commit**.

**Critère** : cliquer sur le goban pose une pierre et l'interface se met à
jour. Cliquer sur une case occupée affiche l'erreur sans couper la connexion.

### Étape 5 — La recherche et les `progress`

Point le plus délicat de l'intégration, et celui qu'ADR-007 assume.

La recherche tourne **sur le thread du serveur**. Le minimax appelle un
callback à chaque itération de l'approfondissement itératif, et ce callback
**écrit directement une trame `progress` sur le descripteur** de la socket. Pas
de file, pas de réveil de `poll()`.

```
poll() rend le fd lisible
  -> read, demasquage, JSON : {"type":"play","idx":180}
  -> Game::play()                      coup humain
  -> send(state, event "move")
  -> Search::run(...)                  on ne revient plus dans poll()
       callback profondeur 1 -> send(progress)
       callback profondeur 2 -> send(progress)
       ...
  -> Game::play()                      coup de l'IA
  -> send(state, event "ai_move")
retour dans poll()
```

Deux conséquences à traiter explicitement :

- **Le Stop n'agit qu'entre deux coups.** Limite connue (ADR-007). Pour aller
  plus loin, sondez le descripteur depuis le callback de progression : c'est
  praticable justement parce que la socket est déjà ouverte.
- **`send()` peut bloquer ou rendre `EAGAIN`** si le tampon noyau est plein. En
  mono-thread, cela fige le minimax. Ne jamais ignorer le retour de `send()`,
  et utiliser `MSG_NOSIGNAL` : sinon un client qui ferme pendant la recherche
  tue le moteur par `SIGPIPE`, et « aucun crash, jamais » n'est plus vrai.

Le keep-alive suit la même logique : ping toutes les 15 s, client muet 45 s
fermé. Les pings ne partent pas pendant une recherche, ce qui est sans
conséquence tant qu'elle reste sous 0,5 s.

**Critère** : le chronomètre de l'interface avance pendant la réflexion, et le
panneau de recherche affiche profondeur et nœuds croissants.

### Étape 6 — Cycle de vie

Pour ne pas laisser d'orphelin quand Electron est tué brutalement :

```cpp
#include <sys/prctl.h>
::prctl(PR_SET_PDEATHSIG, SIGTERM);
// Course : si le parent est mort entre le fork et le prctl, on a ete adopte
// par init et le signal ne viendra jamais. On le detecte apres coup.
if (::getppid() == 1) return 1;
```

Plus le fil watchdog portable qui lit `stdin` jusqu'à EOF, **activé uniquement
si `--parent-watchdog` est passé** : lancé depuis un terminal avec `stdin` sur
`/dev/null`, `read()` rend `0` immédiatement et le moteur s'arrêterait à la
seconde où vous le démarrez.

**Critère** :

```bash
make app        # apres avoir bascule app/ sur ./Gomoku
# fermer la fenetre, puis :
pgrep -f Gomoku     # ne doit rien renvoyer
```

## Bascule du moteur factice vers le vrai binaire

Trois retouches, une fois l'étape 4 passée :

1. `app/package.json` : le script `start` (sans `GOMOKU_ENGINE`) devient celui
   qu'on utilise ; `start:mock` reste pour l'interface.
2. `Makefile` : `app: $(NAME)` au lieu de `app: ui`, et `mock` devient inutile.
   Voir la recette de [ELECTRON.md](ELECTRON.md).
3. `README.md` : le tableau des cibles.

Rien à changer dans `ui/`.

## Pièges recensés

| symptôme | cause |
|---|---|
| Electron attend puis affiche un timeout, alors que le moteur tourne | `fflush(stdout)` oublié après `GOMOKU_READY` |
| fenêtre blanche / `ERR_CONNECTION_REFUSED` | `loadURL` avant l'annonce du port |
| le moteur lit du charabia dans les commandes | démasquage XOR oublié |
| la socket se rouvre en boucle | le moteur ferme la connexion au lieu de la garder ouverte |
| la socket échoue en `400` en dev | proxy Vite sans `ws: true` |
| le moteur s'arrête aussitôt démarré | watchdog stdin actif sans le garde `--parent-watchdog` |
| au 2ᵉ lancement, port différent puis échec | moteur orphelin du lancement précédent |
| `make app` ne fait rien | `.PHONY` manquant, `app` est aussi un nom de dossier |

## Définition de terminé

- `./Gomoku --no-browser` puis un navigateur sur le port : partie jouable de
  bout en bout, chronomètre visible, panneau de recherche alimenté.
- `make app` : même chose dans la fenêtre Electron, sans orphelin à la
  fermeture.
- Tuer le moteur pendant une partie : l'interface affiche « moteur arrêté »,
  et se reconnecte seule si on le relance.
- Trames tronquées ou aberrantes : aucun crash.
- `make test` vert.
