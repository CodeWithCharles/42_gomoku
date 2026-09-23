# Protocole moteur ↔ interface

Transport : **HTTP + Server-Sent Events** (ADR-004).

- commandes client → moteur : `POST /api/*`, corps JSON
- événements moteur → client : un flux SSE unique sur `GET /events`

Le moteur écoute sur `http://localhost:8642` (port configurable, et il prend le
suivant si celui-ci est occupé). Il sert aussi les fichiers statiques de l'UI.

Trois fichiers doivent rester d'accord — toute modification les touche **tous
les trois dans le même commit** :

- `src/server/session.cpp` — production / consommation côté C++
- `ui/src/protocol.ts` — types côté TypeScript
- ce document

## Principe : une seule source de vérité

Les `POST` **ne renvoient pas l'état**. Ils répondent `204 No Content`, ou
`400` avec un corps d'erreur JSON. Tout ce que l'UI affiche arrive par le flux
SSE.

C'est le point important du design : un seul canal ordonné, donc pas de course
entre la réponse HTTP et un événement poussé, et aucun risque d'appliquer deux
fois le même état.

## Conventions

- Une intersection est un **index linéaire** `idx = y * 19 + x`, dans `[0, 361)`.
  `-1` signifie « aucune ».
- Les couleurs sont les chaînes `"black"` / `"white"` / `"none"`.
- Les durées sont en millisecondes.

## Endpoints

| méthode | chemin | corps | effet |
|---|---|---|---|
| `GET` | `/`, `/assets/*` | — | fichiers statiques de `ui/dist` |
| `GET` | `/events` | — | ouvre le flux SSE |
| `POST` | `/api/new-game` | `{config?, players?}` | réinitialise la partie |
| `POST` | `/api/play` | `{idx}` ou `{x,y}` | joue un coup pour le joueur au trait |
| `POST` | `/api/suggest` | — | lance une recherche **sans** jouer le coup |
| `POST` | `/api/undo` | — | revient à une position où c'est à un humain de jouer |
| `POST` | `/api/limits` | `{maxDepth,budgetMs,maxCandidates}` | règle la recherche |
| `POST` | `/api/weights` | `{weights:{…}}` | règle l'heuristique à chaud |
| `POST` | `/api/stop` | — | demande l'arrêt de la recherche |

Des chemins distincts plutôt qu'un `/api/command` unique : le routage C++ reste
une comparaison de chaînes, et on peut tout déboguer à la main sans l'UI.

```bash
curl -N http://localhost:8642/events                          # écoute le flux
curl -X POST http://localhost:8642/api/play -d '{"idx":180}'  # joue au centre
```

```json
{ "config":  { "captures": true, "doubleThree": true,
               "endgameCapture": true, "opening": "standard" },
  "players": { "black": "human", "white": "ai" } }
```

## Le flux SSE

`GET /events` répond :

```
HTTP/1.1 200 OK
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive
```

puis pousse des blocs `event:` / `data:` séparés par une ligne vide. Trois
événements nommés.

### `event: state`

Envoyé à l'ouverture du flux, puis après chaque changement.

```
event: state
data: {"event":"move","board":[0,0,1,2,…],"size":19,"toMove":"white", …}

```

```json
{ "event": "move",
  "board": [0, 0, 1, 2, "…361 entiers…"],
  "size": 19,
  "toMove": "white",
  "status": "ongoing",
  "winReason": "",
  "pairs":   { "black": 1, "white": 0 },
  "players": { "black": "human", "white": "ai" },
  "config":  { "captures": true, "doubleThree": true,
               "endgameCapture": true, "opening": "standard" },
  "limits":  { "maxDepth": 10, "budgetMs": 450, "maxCandidates": 20 },
  "history": [ { "idx": 180, "player": "black", "captured": [181, 182] } ],
  "lastStats": { "…": "voir event: progress" } }
```

`event` vaut `connected`, `new_game`, `move`, `ai_move`, `undo`, `limits` ou
`weights`. `status` vaut `ongoing`, `black_wins`, `white_wins` ou `draw`.
`board[i]` vaut `0` (vide), `1` (noir) ou `2` (blanc).

L'état part **en entier** à chaque fois : 361 entiers, quelques kilo-octets en
local. Pas de diff, donc pas de désynchronisation possible.

### `event: progress`

Poussé à chaque itération de l'approfondissement itératif, pendant que le
`POST /api/play` est encore en attente. C'est la matière du panneau de debug et
ce qui alimente le chronomètre exigé par le sujet.

```json
{ "depth": 7, "score": 1240, "best": 180,
  "nodes": 412339, "leaves": 301002, "cutoffs": 58211, "ttHits": 12044,
  "elapsedMs": 218,
  "pv": [180, 199, 161],
  "rootScores": [ { "idx": 180, "score": 1240 },
                  { "idx": 199, "score": 980 } ] }
```

`rootScores` donne le score de **chaque coup racine évalué** : c'est ce qui
permet d'afficher une carte de chaleur sur le goban, et c'est le meilleur
support pour expliquer le raisonnement de l'IA en soutenance.

Pour `suggest`, le dernier `progress` porte le coup proposé dans `best` ; le
moteur envoie ensuite un `state` inchangé pour clore l'échange.

### `event: error`

```json
{ "message": "double-trois interdit", "code": "double_three" }
```

`code` ∈ `legal`, `out_of_bounds`, `occupied`, `double_three`, `game_over`,
`not_your_turn`, `unknown`. Le même corps est renvoyé dans la réponse `400` du
`POST` fautif.

### Keep-alive

Le moteur pousse `: ping` suivi d'une ligne vide toutes les 15 s. C'est un
commentaire SSE, ignoré par `EventSource` ; il évite qu'un intermédiaire
considère la connexion comme morte.

### Reconnexion

`EventSource` se reconnecte tout seul. Le moteur poussant l'état complet à
chaque nouvelle connexion, une reconnexion vaut resynchronisation gratuite.
Pas de `Last-Event-ID`, pas de rejeu d'historique : inutile.

## Développement de l'interface

En `npm run dev`, Vite sert l'UI sur `:5173` alors que le moteur est sur
`:8642`. **Contrairement à WebSocket, `fetch` et `EventSource` sont soumis au
CORS** — sans précaution, le navigateur bloque tout.

La bonne réponse n'est pas d'ajouter des en-têtes CORS dans le C++, mais de
faire passer Vite pour le moteur :

```ts
// ui/vite.config.ts
server: {
  proxy: {
    '/api':    'http://localhost:8642',
    '/events': { target: 'http://localhost:8642', changeOrigin: true },
  },
}
```

Tout redevient same-origin, et le C++ n'a pas une ligne de CORS à porter.

Il faut donc les deux lancés :

```bash
./Gomoku --no-browser & (cd ui && npm run dev)
```

En production — navigateur sur le port du moteur, ou fenêtre Electron qui
pointe dessus — c'est déjà la même origine : rien à configurer.
