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
| `new-game` | `config?`, `players?` | réinitialise la partie ; interrompt l'IA en cours |
| `play` | `idx` (ou `x` + `y`) | joue un coup pour le joueur au trait |
| `suggest` | — | lance une recherche **sans** jouer le coup |
| `undo` | — | revient à une position où c'est à un humain d'agir ; interrompt l'IA en cours |
| `choose` | `color` | choix de couleur pendant une ouverture Swap ou Swap2 : `"black"`, `"white"` ou `"place2"` (voir [Ouvertures](#ouvertures)) |
| `limits` | `maxDepth`, `budgetMs`, `maxCandidates` | règle la recherche ; `budgetMs` est une **moyenne** (voir [Budget de temps](#budget-de-temps)) |
| `weights` | `weights` | règle l'heuristique à chaud |
| `stop` | — | arrête la recherche en cours et met l'IA en pause (voir [Arrêt](#arrêt)) |
| `pause` | — | met l'IA en pause **après** l'action en cours, sans couper la recherche (voir [Pause et pas à pas](#pause-et-pas-à-pas)) |
| `resume` | — | lève la pause et relance l'IA |
| `step` | — | fait jouer **une** action à l'IA, puis la laisse en pause |

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

Les champs numériques hors bornes ou d'un mauvais type sont **ignorés**, sans
erreur : `maxDepth` ∈ [1, 64], `budgetMs` ∈ [10, 60 000], chaque poids de
`weights` ∈ [0, 200 000]. Un `idx` hors du plateau donne `out_of_bounds`.

Un message de plus de **1 Mio** (fragments compris) ferme la connexion : une
commande en fait quelques centaines d'octets.

### Ouvertures

`config.opening` ∈ `standard`, `pro`, `longpro`, `swap`, `swap2` (défaut
`standard`). Les règles sont dans [RULES.md](RULES.md), section 6.

- **Pro, Long Pro** : de simples contraintes de placement. Un coup qui ne les
  respecte pas donne l'erreur `opening_rule`.
- **Swap, Swap2** : le joueur de la place Noire pose les 3 premières pierres
  (noire, blanche, noire), puis le joueur de la place Blanche choisit sa
  couleur avec `choose`. En Swap2, il peut aussi répondre `"place2"`, poser
  une blanche et une noire, puis laisser le joueur Noir choisir.

Choisir la couleur que tient l'autre joueur **échange les places** : `players`
change dans le `state` suivant. Un `play` pendant un choix donne
`opening_rule`. Quand l'IA doit choisir, elle cherche la position et prend la
couleur que son score favorise ; elle ne répond jamais `"place2"`. `undo`
annule un choix si l'on revient avant le coup qui l'a précédé.

```json
{ "type": "choose", "color": "black" }
```

### Arrêt

`stop` est traitée **immédiatement**, sans attendre son tour dans la file du
moteur :

- pendant une recherche de l'IA, celle-ci s'arrête et **l'IA joue le meilleur
  coup trouvé jusque-là** (quelques millisecondes plus tard) ;
- l'IA est ensuite **en pause** (`paused: true`) : `resume` ou `step` la
  relancent ;
- les `suggest` en attente sont abandonnés ;
- un `state` d'événement `stop` suit, une fois les commandes déjà en file
  traitées.

`new-game` et `undo` interrompent l'IA de la même façon avant de s'exécuter.
`new-game`, un `play` humain et un `choose` humain lèvent la pause.

### Pause et pas à pas

- `pause` est traitée immédiatement, comme `stop`, mais **ne coupe pas la
  recherche** : l'IA finit et joue son coup, puis n'en commence pas d'autre.
  Un `state` d'événement `pause` suit.
- `resume` lève la pause (et celle d'un `stop`) ; un `state` d'événement
  `resume` suit, puis les coups de l'IA reprennent.
- `step` fait jouer **une seule** action à l'IA, coup ou choix de couleur, sans
  l'attente de 300 ms, puis la laisse en pause. Elle ne produit pas de `state`
  propre : le `state` `ai_move` ou `choice` de l'action en tient lieu. Sans IA
  appelée à agir, elle ne fait rien.

Ces trois commandes sont acceptées dans tous les modes ; elles n'ont d'intérêt
qu'en IA contre IA. La pause n'entre jamais dans `elapsedMs`.

En partie IA contre IA, le moteur marque une pause de **300 ms** entre deux
coups pour que la partie se suive à l'écran. Cette pause n'est pas comptée
dans `elapsedMs`.

### Budget de temps

Le sujet exige moins de 0,5 s **en moyenne** par coup, pas à chaque coup.
`budgetMs` (450 par défaut) est donc la **moyenne visée sur la partie**, gérée
par une banque de temps (`TimeBank`, dans `search.hpp`), utilisée par
`Session` et par `make bench-games` :

- après `n` recherches ayant coûté `dépensé` ms au total, la suivante reçoit
  `budgetMs × (n + 1) − dépensé` ;
- ce budget est borné entre `budgetMs / 2` et `2 × budgetMs` : un coup ne dure
  jamais plus de 0,9 s avec les réglages par défaut ;
- la banque repart à zéro à chaque `new-game`. `suggest` y est compté, `undo`
  ne rembourse rien.

Un coup rapide (coup forcé, profondeur maximale atteinte tôt) laisse donc du
crédit au suivant. Par récurrence, **la moyenne courante ne dépasse jamais
`budgetMs`**, à quelques millisecondes près : la recherche ne consulte
l'horloge que tous les 1 024 nœuds.

L'état publié (`limits.budgetMs`) montre toujours la moyenne visée, jamais le
budget accordé au coup en cours. Le temps réellement consommé est
`elapsedMs`, dans `progress` et `lastStats`.

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
  "history": [ { "idx": 180, "player": "black", "captured": [181, 182],
                 "stats": { "…": "voir Stats" } } ],
  "choices": [ { "afterMove": 3, "choice": "black", "swapped": true,
                 "actor": "white", "stats": { "…": "voir Stats" } } ],
  "paused": false,
  "lastStats": { "…": "voir Stats" },
  "opening": { "step": "play", "actor": "white", "options": [] } }
```

`event` ∈ `connected`, `new_game`, `move`, `ai_move`, `suggestion`, `undo`,
`limits`, `weights`, `choice`, `stop`, `pause`, `resume`.

`history[i].stats` est l'analyse enregistrée quand l'IA a joué ce coup, au
format `Stats` ; il est absent pour un coup humain. `choices` liste les choix
de couleur Swap / Swap2 dans l'ordre : `afterMove` est le nombre de pierres
posées au moment du choix, `choice` vaut `black`, `white` ou `place2`,
`swapped` dit si les places ont été échangées, `actor` est la place qui a
choisi (nommée par sa couleur de départ, comme `opening.actor`), `stats`
n'existe que pour un choix de l'IA. `paused` dit si l'IA est en pause (`pause` ou `stop`). Un
moteur qui n'envoie pas `choices` ou `paused` équivaut à `[]` et `false`.

`opening.step` ∈ `play` (coup normal), `place` (pierre d'ouverture), `choose`
(choix de couleur attendu). `opening.actor` est la **place qui doit agir** :
la couleur au trait en jeu normal, le joueur qui pose ou qui choisit pendant
l'ouverture. C'est `players[opening.actor]`, et non `players[toMove]`, qui dit
si un humain doit agir. `opening.options` liste les réponses possibles pendant
un choix, et reste vide sinon. Un moteur qui n'envoie pas `opening` équivaut à
`{ "step": "play", "actor": toMove, "options": [] }`.

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
`not_your_turn`, `opening_rule`, `unknown`. Le `code` est stable et machine-lisible ; le
`message` est en anglais et ne sert qu'au debug — l'interface affiche ce
qu'elle veut à partir du `code`.

## Maintien de connexion

Après **15 s** sans rien recevoir d'un client, le moteur lui envoie un `ping`
(opcode `0x9`). Un client qui laisse **6 pings de suite** sans `pong` est
fermé, soit environ 1 min 45 de silence. Les navigateurs répondent aux pings
d'eux-mêmes : l'interface n'a rien à faire. Les pings partent aussi pendant
une recherche, qui ne bloque plus les threads réseau (ADR-007).

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
