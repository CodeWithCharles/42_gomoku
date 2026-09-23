# Protocole moteur ↔ interface

Transport : **WebSocket**, un seul client, messages **JSON texte** (ADR-004).
Le moteur sert `ui/dist` en HTTP et ne promeut en WebSocket que le chemin
**`/ws`**.

## La référence fait foi

`ui/src/protocol.ts` **est** le contrat : l'interface et `tools/mock-engine.mjs`
sont écrits dessus. Ce document le décrit, il ne le définit pas. En cas de
désaccord entre les deux, c'est ce document qui a tort.

Trois fichiers changent **dans le même commit** :

- `ui/src/protocol.ts` — les types, la référence
- `backend/src/server/session.cpp` — la production et la consommation côté C++
- ce document

## Principe : une seule source de vérité

Le moteur ne répond jamais à une commande. Il pousse des **états complets**, et
l'interface se contente de les afficher : 361 entiers, pas de diff, pas
d'accusé de réception. Un seul canal ordonné, donc aucune désynchronisation
possible.

## Conventions

- Une intersection est un **index linéaire** `idx = y * 19 + x`, dans `[0, 361)`.
  `-1` signifie « aucune ».
- `board[i]` vaut `0` (vide), `1` (noir) ou `2` (blanc).
- Les couleurs sont `"black"` / `"white"`.
- Les durées sont en millisecondes.
- Les **commandes** sont en kebab-case (`new-game`), les **événements** en
  snake_case (`new_game`). Incohérence connue et assumée : la corriger coûterait
  du code pour aucun bénéfice.

## Client → moteur

| `type` | champs | effet |
|---|---|---|
| `new-game` | `config?`, `players?` | réinitialise la partie |
| `play` | `idx` (ou `x` + `y`) | joue un coup pour le joueur au trait |
| `suggest` | — | lance une recherche **sans** jouer le coup |
| `undo` | — | revient à une position où c'est à un humain de jouer |
| `limits` | `maxDepth`, `budgetMs`, `maxCandidates` | règle la recherche |
| `weights` | `weights` | règle l'heuristique à chaud |
| `stop` | — | demande l'arrêt de la recherche en cours |

```json
{ "type": "new-game",
  "config":  { "captures": true, "doubleThree": true,
               "endgameCapture": true, "opening": "standard" },
  "players": { "black": "human", "white": "ai" } }
```

```json
{ "type": "play", "idx": 180 }
```

Une commande fautive donne un `error`. **La socket reste ouverte**, toujours.

## Moteur → client

Trois événements seulement : `state`, `progress`, `error`.

### `state` — l'état complet

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
  "lastStats": { "…": "voir Stats" } }
```

`event` ∈ `connected`, `new_game`, `move`, `ai_move`, `suggestion`, `undo`,
`limits`, `weights`.

`status` ∈ `ongoing`, `black_wins`, `white_wins`, `draw`.

`winReason` est un **code stable**, jamais affiché tel quel : `""`,
`five_in_a_row`, `captures`, `board_full`, `resignation`. L'interface en dérive
un libellé (`winReasonLabel`). Le moteur ne décide pas de la langue.

### `progress` — pendant la réflexion

Poussé à chaque itération de l'approfondissement itératif. C'est la matière du
panneau de debug, et ce qui alimente le chronomètre exigé par le sujet.
**Les champs sont à plat**, pas imbriqués.

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
permet la carte de chaleur sur le goban, et le meilleur support pour expliquer
le raisonnement de l'IA en soutenance.

### Fin de recherche

Il n'y a pas de message dédié : **un `state` clôt toujours une recherche**, et
son champ `lastStats` porte la statistique finale, au format `Stats` — les
mêmes champs que `progress`, sans le `type`.

- `event: "ai_move"` — l'IA a joué ; `lastStats.best` est le coup joué.
- `event: "suggestion"` — réponse à `suggest` ; `lastStats.best` est le coup
  proposé, **le plateau n'a pas changé**.

Entre deux recherches, `lastStats` vaut `NO_STATS` (tout à zéro, `best: -1`).

### `error`

```json
{ "type": "error", "message": "intersection already taken", "code": "occupied" }
```

`code` ∈ `legal`, `out_of_bounds`, `occupied`, `double_three`, `game_over`,
`not_your_turn`, `unknown`. Le `code` est stable et machine-lisible ; le
`message` est en anglais et ne sert qu'au debug — l'interface affiche ce
qu'elle veut à partir du `code`.

## Maintien de connexion

Le moteur envoie un `ping` (opcode `0x9`) toutes les **15 s** et ferme un
client muet depuis **45 s**. Les pings ne partent pas pendant une recherche,
ce qui est sans conséquence tant qu'elle reste sous 0,5 s (ADR-007).

## Reconnexion

`WebSocket` ne se reconnecte pas tout seul, contrairement à `EventSource` :
`ui/src/useEngine.ts` le fait, en backoff exponentiel de 250 ms à 5 s. Le
moteur poussant un `state` complet à chaque nouvelle connexion, une reconnexion
vaut resynchronisation gratuite — rien à rejouer.

## URL et développement

L'interface construit **toujours** une URL relative :

```ts
new WebSocket(`ws://${location.host}/ws`)
```

Aucun port n'est écrit en dur : le moteur choisit le sien au démarrage
(`GOMOKU_READY port=N`), et la page servie par ce même moteur tombe forcément
dessus. C'est aussi ce qui rend le code identique dans un navigateur et dans la
fenêtre Electron.

En `npm run dev`, Vite sert l'interface sur `:5173` et relaie `/ws` vers le
moteur — **avec `ws: true`, obligatoire** : sans cette option l'en-tête
`Upgrade` n'est pas transmis et la socket échoue en `400`.

```ts
// ui/vite.config.ts
server: {
  proxy: { '/ws': { target: 'ws://localhost:8642', ws: true } },
}
```

Il faut donc les deux lancés pendant le développement :

```bash
./Gomoku --no-browser & (cd ui && npm run dev)
```
