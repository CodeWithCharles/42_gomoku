# Protocole moteur ↔ interface

Transport : **WebSocket** (ADR-004).

- une seule connexion `ws://localhost:8642/ws`, ouverte au chargement de la page
- commandes client → moteur : messages texte JSON
- événements moteur → client : messages texte JSON

Le moteur écoute sur `http://localhost:8642` (port configurable, et il prend le
suivant si celui-ci est occupé). Il sert aussi les fichiers statiques de l'UI en
HTTP ordinaire ; seul `/ws` est promu en WebSocket.

Trois fichiers doivent rester d'accord — toute modification les touche **tous
les trois dans le même commit** :

- `src/server/session.cpp` — production / consommation côté C++
- `ui/src/protocol.ts` — types côté TypeScript
- ce document

## Principe : une seule source de vérité

Les commandes **ne sont pas acquittées par une réponse**. Le moteur répond en
poussant un `state` (ou un `error`) sur la même socket.

C'est le point important du design : un seul canal ordonné, donc pas de course
entre un accusé de réception et un événement poussé, et aucun risque
d'appliquer deux fois le même état. Le WebSocket rend cet invariant naturel :
il n'existe littéralement pas d'autre chemin de retour.

## Conventions

- Une intersection est un **index linéaire** `idx = y * 19 + x`, dans `[0, 361)`.
  `-1` signifie « aucune ».
- Les couleurs sont les chaînes `"black"` / `"white"` / `"none"`.
- Les durées sont en millisecondes.
- Tout message, dans les deux sens, est une **trame texte** contenant un objet
  JSON avec un champ `type`. Pas de trame binaire, pas de fragmentation en
  émission.

## Établissement de la connexion

Le client ouvre la socket sur le chemin `/ws` :

```ts
const socket = new WebSocket(`ws://${location.host}/ws`);
```

Côté moteur, c'est une requête HTTP ordinaire promue en WebSocket (RFC 6455) :

```
GET /ws HTTP/1.1
Host: localhost:8642
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
```

```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

`Sec-WebSocket-Accept` est `base64(sha1(clé + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))`.
La constante est celle de la RFC, elle ne change jamais. SHA-1 et base64 sont à
écrire dans `src/net/` (ADR-003 : aucune dépendance tierce).

Deux règles de la RFC qui ne se négocient pas :

- **toute trame client → serveur est masquée** : un XOR de 4 octets dont la clé
  précède la charge utile. Un serveur qui oublie le démasquage lit du bruit ;
- **une trame de contrôle (ping, pong, close) peut s'intercaler** entre deux
  fragments d'un message, et sa charge utile ne dépasse pas 125 octets.

Le moteur n'accepte **qu'une connexion à la fois**. Une seconde ouverture
remplace la première, qui est fermée avec le code `1001`. Un client local, une
partie : ADR-007.

## Commandes — client → moteur

| `type` | champs | effet |
|---|---|---|
| `new-game` | `config?`, `players?` | réinitialise la partie |
| `play` | `idx` ou `x`,`y` | joue un coup pour le joueur au trait |
| `suggest` | — | lance une recherche **sans** jouer le coup |
| `undo` | — | revient à une position où c'est à un humain de jouer |
| `limits` | `maxDepth`, `budgetMs`, `maxCandidates` | règle la recherche |
| `weights` | `weights: {…}` | règle l'heuristique à chaud |
| `stop` | — | demande l'arrêt de la recherche |

```json
{ "type": "play", "idx": 180 }
```

```json
{ "type": "new-game",
  "config":  { "captures": true, "doubleThree": true,
               "endgameCapture": true, "opening": "standard" },
  "players": { "black": "human", "white": "ai" } }
```

Un `type` inconnu, un JSON invalide ou un champ manquant donnent un `error` de
code `unknown` ; la connexion **n'est pas** fermée pour autant. Fermer la socket
sur une commande fautive rendrait le débogage pénible pour rien.

## Événements — moteur → client

### `state`

Envoyé à l'ouverture de la connexion, puis après chaque changement.

```json
{ "type": "state",
  "event": "move",
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
  "lastStats": { "…": "voir progress" } }
```

`event` vaut `connected`, `new_game`, `move`, `ai_move`, `undo`, `limits` ou
`weights`. `status` vaut `ongoing`, `black_wins`, `white_wins` ou `draw`.
`board[i]` vaut `0` (vide), `1` (noir) ou `2` (blanc).

L'état part **en entier** à chaque fois : 361 entiers, quelques kilo-octets en
local. Pas de diff, donc pas de désynchronisation possible.

### `progress`

Poussé à chaque itération de l'approfondissement itératif, pendant que la
recherche tourne. C'est la matière du panneau de debug et ce qui alimente le
chronomètre exigé par le sujet.

```json
{ "type": "progress",
  "depth": 7, "score": 1240, "best": 180,
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

### `error`

```json
{ "type": "error", "message": "double-trois interdit", "code": "double_three" }
```

`code` ∈ `legal`, `out_of_bounds`, `occupied`, `double_three`, `game_over`,
`not_your_turn`, `unknown`.

### Keep-alive

Le moteur envoie une **trame de contrôle ping** (opcode `0x9`) toutes les 15 s.
Le navigateur y répond par un pong sans que l'UI ait une ligne à écrire ; côté
C++, il faut en revanche répondre aux pings du client par un pong portant la
même charge utile, sous peine de voir la connexion coupée.

Un client muet pendant 45 s est considéré mort et sa socket est fermée.

### Fermeture et reconnexion

**Contrairement à `EventSource`, `WebSocket` ne se reconnecte pas tout seul.**
C'est le principal code supplémentaire côté UI :

```ts
// ui/src/useEngine.ts
let backoff = 250;
function connect() {
  const socket = new WebSocket(`ws://${location.host}/ws`);
  socket.onopen  = () => { backoff = 250; };
  socket.onclose = () => {
    setTimeout(connect, backoff);
    backoff = Math.min(backoff * 2, 5000);   // 250 ms → 5 s
  };
}
```

Le moteur poussant l'état complet à chaque nouvelle connexion, une reconnexion
vaut resynchronisation gratuite. Pas de rejeu d'historique, pas de numéro de
séquence : inutile.

## Déboguer sans l'interface

`curl` ne parle pas WebSocket. Il faut un client dédié, par exemple
[`websocat`](https://github.com/vi/websocat) :

```bash
websocat ws://localhost:8642/ws                       # écoute les événements
echo '{"type":"play","idx":180}' | websocat ws://localhost:8642/ws
```

C'est le coût assumé d'ADR-004 : on perd le `curl -N` d'un flux HTTP, on gagne
un canal symétrique. **Installez `websocat` avant la soutenance** — c'est le
seul moyen de montrer le protocole nu si l'UI se met en travers.

## Développement de l'interface

En `npm run dev`, Vite sert l'UI sur `:5173` alors que le moteur est sur
`:8642`. Le WebSocket n'est pas soumis au CORS, donc rien ne le bloquerait ;
mais coder `ws://localhost:8642` en dur dans l'UI casserait la production, où
le port est choisi au démarrage. La réponse reste le proxy Vite, **pas** des
en-têtes CORS dans le C++ :

```ts
// ui/vite.config.ts
server: {
  proxy: {
    '/ws': { target: 'ws://localhost:8642', ws: true },
  },
}
```

`ws: true` n'est pas optionnel : sans lui, Vite ne relaie pas l'`Upgrade` et la
connexion échoue en `400`.

Tout redevient same-origin, l'UI n'écrit que des URL relatives, et le C++ n'a
pas une ligne de CORS à porter.

Il faut donc les deux lancés :

```bash
./Gomoku --no-browser & (cd ui && npm run dev)
```

En production — navigateur sur le port du moteur, ou fenêtre Electron qui
pointe dessus — c'est déjà la même origine : rien à configurer.
