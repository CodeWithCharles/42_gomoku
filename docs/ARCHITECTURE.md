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
│    src/net/      HTTP + WebSocket  ← module 4│
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

`Gomoku` sert lui-même `ui/dist` et pousse ses événements en WebSocket.
Electron n'est qu'une fenêtre par-dessus : **le binaire reste jouable seul**.

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
coûte plus cher que ce qu'on écrit à la main. La couche réseau se réduit à :
parsing de requête HTTP, service de fichiers statiques, handshake WebSocket
(SHA-1 + base64), framing RFC 6455, et un petit JSON.
Le WebSocket (ADR-004) est ce qui coûte le plus cher ici : SHA-1 est à écrire
à la main, puisque le tirer d'OpenSSL serait précisément la dépendance qu'on
refuse. C'est une soixantaine de lignes mécaniques et testables contre des
vecteurs connus — à faire une fois, à ne plus jamais toucher.
Le dépôt se compile avec `make` sur une machine nue, et il n'y a pas de
submodule à oublier le jour de la soutenance.

### ADR-004 — WebSocket plutôt que SSE
Le moteur et l'UI ont besoin des deux sens : le client envoie des commandes, le
moteur pousse l'état et la progression de la recherche (profondeur, nœuds,
meilleur coup courant) pendant qu'il calcule — c'est le chronomètre et le
panneau de debug exigés par le sujet. WebSocket porte les deux sur **une seule
socket ordonnée**, ce qui supprime par construction toute course entre une
réponse HTTP et un événement poussé.

Le gain décisif n'est pas le débit, dérisoire ici, c'est le **canal montant
permanent**. Une socket déjà ouverte se sonde depuis le callback de progression
de la recherche : c'est la seule voie praticable vers un bouton « Stop » qui
agisse pendant le calcul (voir la limite d'ADR-007). Avec un `POST`, il aurait
fallu accepter et parser une requête HTTP entière au milieu du minimax.

Ce que ça coûte, et il faut l'assumer :

- le handshake `Sec-WebSocket-Accept`, donc SHA-1 et base64 écrits à la main
  (ADR-003) ;
- le framing RFC 6455 : masquage XOR obligatoire des trames client → serveur,
  longueurs sur 7/16/64 bits, trames de contrôle ping/pong/close. Environ
  200 lignes de C++ contre 60 pour un flux `text/event-stream` ;
- du parsing binaire là où SSE n'avait que du texte ligne à ligne. Le sujet
  sanctionne tout crash par un 0 : cette couche se teste, y compris sur des
  trames tronquées ou aberrantes, avant d'être considérée comme acquise ;
- la perte du `curl` : déboguer le protocole nu demande `websocat`, à installer
  avant la soutenance ;
- la reconnexion automatique d'`EventSource`, à réécrire en TypeScript (une
  dizaine de lignes avec backoff, voir `docs/PROTOCOL.md`).

**Rejeté** : SSE plus `POST /api/*` — deux canaux asymétriques, aucun moyen
d'être lu pendant la recherche, mais nettement moins de C++ à écrire et
déboguable au `curl`. C'était la décision initiale ; elle a été renversée au
profit du canal montant.
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
poussant sa progression par un callback qui écrit directement une trame sur le
descripteur de la socket WebSocket.

L'UI n'attend aucun accusé de réception : elle a envoyé sa commande et écoute la
socket. Rien ne bloque de son côté pendant le calcul de l'IA.

**Limite connue** : pendant la recherche, le serveur ne lit aucun message
entrant — le bouton « Stop » n'agit donc qu'entre deux coups. La socket étant
déjà ouverte, la levée est plus simple qu'avec un transport requête/réponse :
il suffit de sonder le descripteur depuis le callback de progression. Pour un
vrai Stop coopératif ou un mode spectateur, il faudra tout de même un thread de
recherche et une file de messages réveillant `poll()` par self-pipe.

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
