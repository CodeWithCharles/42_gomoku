# Architecture

> Ce document décrit ce que nous allons construire. **Rien n'est encore écrit** :
> le dépôt ne contient que cette documentation.

## Vue d'ensemble

```
┌──────────────────────────────────────────────┐
│  Gomoku-desktop   (coque Electron)           │  make app  — optionnel
│    lance ./Gomoku, lit son port sur stdout,  │
│    ouvre une BrowserWindow dessus            │
└────────────────────┬─────────────────────────┘
                     │  http://localhost:PORT
                     │  (un navigateur ordinaire
                     │   marche tout aussi bien)
                     ▼
┌──────────────────────────────────────────────┐
│  Gomoku   (un seul binaire C++)              │  make — le livrable du sujet
│                                              │
│    src/net/      HTTP + SSE        ← module 4│
│    src/server/   protocole JSON    ← module 4│
│         │                                    │
│    src/game/     arbitrage         ← partagé │
│         │                                    │
│    src/search/   minimax           ← module 2│
│         │                                    │
│    src/eval/     heuristique       ← module 3│
│         │                                    │
│    src/core/     plateau + règles  ← module 1│
└──────────────────────────────────────────────┘
```

`Gomoku` sert lui-même `ui/dist` et pousse ses événements en SSE. Electron
n'est qu'une fenêtre par-dessus : **le binaire reste jouable seul**.

## Règle de dépendance

Les flèches ne vont que **vers le bas** :

| couche | a le droit d'inclure | n'a PAS le droit d'inclure |
|---|---|---|
| `core/` | `types.hpp` | tout le reste |
| `eval/` | `core/` | `search/`, `game/`, `net/` |
| `search/` | `core/`, `eval/`, `game/` | `net/`, `server/` |
| `net/` | rien du jeu | tout `gomoku::` |
| `server/` | tout | — |

Conséquence pratique : `src/net/` ne connaît pas le Gomoku, et `src/core/` ne
connaît ni le réseau ni le JSON. Si vous êtes tenté d'écrire une règle du jeu
dans `session.cpp`, c'est qu'elle va dans `rules.hpp`.

La coque Electron est **hors de ce graphe** : elle ne contient aucune logique
de jeu, seulement le lancement d'un process et une fenêtre.

## Décisions (ADR)

### ADR-001 — C++20 pour le moteur, TypeScript/React pour l'interface
Le sujet impose une profondeur 10 en moins de 0,5 s : cela exige du natif
optimisé. L'interface n'a aucune contrainte de performance mais doit être
« agréable à l'œil » et montrer le raisonnement de l'IA — le web est de loin le
plus rapide à écrire pour ça.
**Rejeté** : tout en C++ avec SFML/ImGui (panneau de debug bien plus coûteux à
écrire) ; moteur compilé en WebAssembly (≈ 2× plus lent, et plus d'exécutable
natif à rendre).

### ADR-002 — Le plateau cache sa représentation
`Board` expose `at`, `place`, `remove`, `countLine`, `window`, `hasNeighbor` —
et rien d'autre. On commence par un `std::array<Player, 361>` : correct,
lisible, suffisant pour valider les règles.
Le passage aux bitboards (6 × `uint64_t`, décalages masqués par ligne) sera une
optimisation **interne à `board.cpp`** : aucun autre fichier ne bougera.
On ne le fait qu'une fois le profilage disponible — le goulot sera
probablement l'évaluation, pas la représentation.

### ADR-003 — Zéro dépendance C++ tierce
Le sujet impose un Makefile ; y greffer CMake pour tirer une bibliothèque HTTP
coûte plus cher que ce qu'on écrit à la main. Avec SSE (ADR-004) la couche
réseau se réduit à : parsing de requête HTTP, service de fichiers statiques,
flux `text/event-stream`, et un petit JSON. Pas de handshake, pas de SHA-1,
pas de framing binaire.
Le dépôt se compile avec `make` sur une machine nue, et il n'y a pas de
submodule à oublier le jour de la soutenance.

### ADR-004 — SSE plutôt que WebSocket
Le jeu est au tour par tour : une commande = une requête `POST`. Le seul besoin
temps réel est de **pousser** la progression de la recherche (profondeur, nœuds,
meilleur coup courant) pendant qu'elle calcule — c'est le chronomètre et le
panneau de debug exigés par le sujet. SSE fait exactement ça, et rien de plus.

Ce que ça économise par rapport à WebSocket : le handshake
`Sec-WebSocket-Accept` (donc SHA-1 + base64), le masquage des trames, les
longueurs sur 7/16/64 bits, le ping/pong. Environ 200 lignes de C++ contre 60.
Ce que ça coûte : lire le corps d'une requête `POST` (`Content-Length`), une
trentaine de lignes.

**Rejeté** : le polling (débogage du chrono moins fluide, bruit réseau inutile).

### ADR-005 — Electron est une coque optionnelle
Le sujet exige un **exécutable `Gomoku` produit par le Makefile**. Une
application Electron n'est ni produite par `make`, ni nommable `Gomoku` sans
acrobatie, et son `node_modules` (~200 Mo) n'est pas commitable — `make`
dépendrait donc d'un `npm install` réussi sur la machine du correcteur.

Donc Electron n'enrobe, ne remplace rien :

```
make       → Gomoku           binaire C++ autonome, jouable dans un navigateur
make app   → Gomoku-desktop   fenêtre Electron qui lance ./Gomoku
```

Règles non négociables qui en découlent :

- `make` ne doit **jamais** dépendre d'Electron ni de npm.
- Si la coque casse, `./Gomoku` + navigateur reste la démonstration valide.
  C'est notre filet de sécurité pour la soutenance.
- La coque contient **zéro logique de jeu** : elle lance un process et affiche
  une fenêtre.

Les trois canaux de communication, les exemples de code et les pièges sont
détaillés dans [ELECTRON.md](ELECTRON.md). En résumé, les points à traiter :

- **Négociation du port** : `Gomoku` imprime son URL sur stdout au démarrage
  (il essaie le port demandé puis les suivants s'il est occupé). Electron lit
  stdout pour savoir où pointer la `BrowserWindow`. Pas de port codé en dur des
  deux côtés.
- **Cycle de vie** : Electron doit tuer le process enfant en quittant, y
  compris sur fermeture brutale. Un moteur orphelin qui garde le port est le
  bug classique de ce montage.
- **Mort du moteur** : si l'enfant meurt, la fenêtre doit le dire plutôt que
  d'afficher une page blanche.

### ADR-006 — `ui/dist` est versionné
`make` ne doit jamais dépendre de npm. Le bundle compilé est donc commité, et
`make ui` ne sert qu'aux développeurs de l'interface.
Electron charge `http://localhost:PORT`, pas `file://` : **un seul bundle sert
les deux coques**, il n'y a rien à dupliquer.
**En contrepartie** : quiconque touche à `ui/src` doit lancer `make ui` et
commiter `ui/dist` dans le même commit. Un `dist` périmé est un bug silencieux.

### ADR-007 — Serveur mono-thread
Un seul client local, jeu au tour par tour : ni concurrence ni backpressure à
gérer. Une boucle `poll()`, et la recherche tourne sur le thread du serveur en
poussant sa progression par un callback qui écrit directement sur le descripteur
du flux SSE.

Le `POST /api/play` reste donc en attente pendant tout le calcul de l'IA, ce qui
est sans importance : l'UI n'attend pas sa réponse, elle écoute le flux.

**Limite connue** : pendant la recherche, le serveur ne lit aucune requête
entrante — le bouton « Stop » n'agit donc qu'entre deux coups. Le jour où l'on
veut un vrai Stop ou un mode spectateur, il faudra un thread de recherche et
une file de messages réveillant `poll()` par self-pipe.

### ADR-008 — `Game` est le seul arbitre
Deux chemins distincts, volontairement :

- `Game::play()` — chemin UI : valide tout (double-trois compris), tient
  l'historique, tranche la fin de partie de façon exhaustive.
- `Game::makeMove()` / `unmakeMove()` / `terminalAfter()` — chemin recherche :
  pas de validation, pas d'allocation, annulable sans copier le plateau.

La recherche ne filtrera la légalité complète qu'**à la racine**. Dans les
nœuds internes, le double-trois est ignoré (trop coûteux). L'IA ne peut donc
jamais jouer un coup illégal, mais son évaluation profonde est légèrement
optimiste. C'est un compromis assumé, à expliquer en soutenance.

## Le chemin critique : profondeur 10 en 0,5 s

Sur un 19×19 le branchement brut est 361. Pour tenir, il faut l'effondrer :

| levier | effet attendu | module |
|---|---|---|
| voisinage rayon 2 | 361 → ~30 | 1 |
| tri des coups + élagage avant | ~30 → ~12 explorés | 2+3 |
| alpha-bêta bien ordonné | branchement effectif ≈ √b | 2 |
| table de transposition | 20-40 % de nœuds en moins | 2 |
| éval incrémentale | 5-10× sur le coût par feuille | 3 |
| détection de menaces forcées | effondre les lignes tactiques | 2+3 |

`make bench` devra mesurer profondeur atteinte et nœuds/s sur des positions
fixes. **Aucune optimisation ne se justifie sans un avant/après chiffré** — et
la première tâche du module 2 est d'établir la ligne de base.
