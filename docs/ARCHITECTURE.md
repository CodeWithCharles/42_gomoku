# Architecture

> Ce document décrit le découpage et les décisions qui le justifient. Les ADR
> gardent la trace des choix renversés en route (003, 004, 007) : la version
> actuelle est en tête, l'historique ensuite. L'état d'avancement est dans
> [HANDOFF.md](HANDOFF.md).

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

Toutes ces couches vivent sous `backend/`. L'arbitrage (`Game`) a finalement
pris place dans `src/core/game.cpp` : `src/game/` est vide.

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
et rien d'autre. Il a commencé par un `std::array<Player, 361>` : correct,
lisible, suffisant pour valider les règles.

Le profilage a désigné le goulot : `Board::window`, appelée par l'ordonnancement
des coups, prenait près de la moitié du temps. `Board` tient donc en plus
**une copie de chaque ligne** du plateau dans les 4 directions (19 + 19 + 37 +
37 mots de 64 bits, 2 bits par case, 4 cases de mur de chaque côté).
`place` et `remove` mettent 4 mots à jour, et `window` n'est plus qu'un
décalage et un masque. Comme prévu, le changement est resté **interne à
`board.cpp`** : aucun autre fichier n'a bougé, et la nouvelle version a été
comparée à l'ancienne sur 1,4 M de fenêtres tirées au hasard.

### ADR-003 — Deux dépendances C++ vendorées, et pas une de plus
Le moteur embarque, copiées dans `backend/thirdparty/` :

- **civetweb v1.16** (MIT) : HTTP statique et WebSocket, un seul `.c`. Le tag
  est épinglé : `master` ne compile pas ;
- **nlohmann/json v3.11.3** (MIT) : un seul en-tête.

Le Makefile les compile directement : pas de CMake, pas de submodule, pas de
réseau, `make` marche sur une machine nue le jour de la soutenance.

**Pourquoi le revirement.** La décision initiale était « zéro dépendance » :
écrire à la main HTTP, SHA-1, base64, le cadrage RFC 6455 et un petit JSON.
C'est du code sans rapport avec l'intérêt du projet, l'IA, et c'est
précisément la couche où un bug de parsing donne un crash, donc un 0. Deux
bibliothèques mûres, testées et sous licence MIT coûtent moins cher que leur
réécriture et que les tests qu'elle aurait exigés.

**Ce que ça coûte** : 22 000 lignes de C que nous ne maîtrisons pas. On ne
leur fait confiance qu'à travers `make fuzz`, sous ASan et UBSan, et
ThreadSanitizer n'y signale qu'une course bénigne dans `mg_stop`.
L'enveloppe `src/net/server.cpp` rattrape toute exception avant qu'elle ne
traverse un callback C, et borne la taille des messages.

### ADR-004 — WebSocket plutôt que SSE
Le moteur et l'UI ont besoin des deux sens : le client envoie des commandes, le
moteur pousse l'état et la progression de la recherche (profondeur, nœuds,
meilleur coup courant) pendant qu'il calcule — c'est le chronomètre et le
panneau de debug exigés par le sujet. WebSocket porte les deux sur **une seule
socket ordonnée**, ce qui supprime par construction toute course entre une
réponse HTTP et un événement poussé.

Le gain décisif n'est pas le débit, dérisoire ici, c'est le **canal montant
permanent**. Le `stop` arrive sur la socket déjà ouverte pendant que la
recherche tourne sur un autre thread, et l'arrête aussitôt (ADR-007). Avec un
`POST`, il aurait fallu accepter et parser une requête HTTP entière au milieu
du minimax.

Ce que ça coûte, et il faut l'assumer :

- la poignée de main, le cadrage RFC 6455 et les trames de contrôle sont
  délégués à civetweb (ADR-003) ; le ping/pong est activé, avec un ping après
  15 s de silence ;
- civetweb livre des **trames**, pas des messages : `src/net/server.cpp`
  recolle les fragments, avec une limite de 1 Mio par message ;
- civetweb lit les trames d'une connexion **une par une** : aucun calcul long
  ne doit tourner dans un gestionnaire, sinon le `stop` suivant n'est pas lu
  (ADR-007) ;
- la perte du `curl` : déboguer le protocole nu demande `websocat`, ou les
  scripts Node de `backend/tests/` ;
- la reconnexion automatique d'`EventSource`, réécrite en TypeScript dans
  `ui/src/useEngine.ts` (backoff de 250 ms à 5 s).

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

### ADR-007 — Un seul thread possède la partie
Un seul client local, jeu au tour par tour : la partie n'a besoin d'**aucun
verrou** si un seul thread y touche. C'est le choix fait, après deux versions
mesurées puis abandonnées :

1. **mono-thread** (`num_threads 1`) : une socket WebSocket ouverte occupe le
   thread civetweb pour toute sa durée, le HTTP et un second client partent en
   timeout ;
2. **un `std::mutex` dans `Session`**, la recherche tournant dans le
   gestionnaire civetweb : civetweb lit les trames d'une connexion une par une,
   donc le `stop` envoyé pendant une recherche n'était lu qu'après elle. Le
   bouton Stop n'a jamais rien arrêté, et une partie IA contre IA bloquait tout
   le serveur, nouvel onglet compris.

**Modèle actuel** (`src/server/session.cpp`) :

- les gestionnaires civetweb ne touchent jamais `Game` : ils déposent une tâche
  dans une file et rendent la main, la trame suivante est lue aussitôt ;
- un **thread de travail** unique exécute les tâches dans l'ordre et possède
  `Game`, `Engine` et la banque de temps ;
- `stop` agit depuis le gestionnaire, par deux atomiques : le drapeau d'arrêt
  du moteur et `halted_`, qui empêche le coup IA suivant. Le callback de
  progression réarme l'arrêt, car `Engine::search` efface le drapeau en
  démarrant ;
- chaque coup IA est **sa propre tâche** : entre deux coups, `new-game`,
  `undo` ou un nouvel onglet passent. Il n'y en a jamais deux en attente ;
- une connexion neuve passe en tête de file, un seul `suggest` peut attendre ;
- `Server::sendText` écrit **sous le verrou** des clients : le gestionnaire de
  fermeture le prend aussi, donc civetweb ne peut pas libérer une connexion
  pendant qu'un autre thread y écrit ;
- civetweb tourne avec **32 threads**, car chaque WebSocket ouverte en garde un.
  Avec 4, le quatrième onglet ne recevait plus rien.

Vérifié par `make fuzz` (rafales de commandes pendant une partie IA contre IA,
déconnexions en pleine recherche, huit clients à la fois) sous ASan/UBSan, et
par ThreadSanitizer : aucune course dans notre code.

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

Sur un 19×19 le branchement brut est 361. Ce qui a été fait, dans l'ordre où
ça a été mesuré, avec `make bench` comme juge (budget 450 ms) :

| levier | effet mesuré | module |
|---|---|---|
| table de motifs (65 536 entrées), éval par niveaux | profondeur inchangée, éval juste ; 6 % du temps | 3 |
| `moveScore` sur la même table (attaque + défense + capture) | gain forcé trouvé en 1 911 nœuds au lieu de 15 067 | 3 |
| `movegen` partant des pierres au lieu des cases vides | ×1,5 nœuds/s, arbre identique | 1 |
| table de transposition | −42 % de nœuds à 8 candidats | 2 |
| une seule fenêtre par axe dans `moveScore` (miroir par bits) | +40 % nœuds/s | 3 |
| faisceau adaptatif : 20 → −3 par niveau → 5, coups forcés seuls si un cinq menace | profondeur 6-7 → 8-9, **et 19-5 en match** contre le faisceau large | 2 |
| PVS | −5 à −8 % de nœuds | 2 |
| bitboards par ligne (ADR-002) | +50 % nœuds/s | 1 |

Essayés, mesurés, **écartés** : coups killer (le bonus de tri change les coups
que le faisceau garde : jusqu'à +25 % de nœuds), bonus de trait dans l'éval (aucun nœud
gagné : un décalage constant ne change aucune décision d'alpha-bêta),
réductions de fin de liste (8-8 en match, pas de gain en partie réelle).

Les 4 positions de `make bench` sont des débuts de partie, le pire cas. Le
sujet juge une **moyenne** : `make bench-games` joue des parties entières dans
les conditions de `Session` (profondeur plafonnée à 10, banque de temps de
PROTOCOL.md). Dernière mesure : 137 ms en moyenne, profondeur 10 sur 96 % des
coups non forcés. **Aucune optimisation ne se justifie sans un avant/après
chiffré**, et la qualité de jeu se vérifie en match, pas seulement au bench.
