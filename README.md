# Gomoku

Projet 42 — une IA capable de battre un joueur humain au Gomoku (variante
captures + interdiction du double-trois), avec une interface web.

> **État** : le moteur C++ est jouable de bout en bout dans le navigateur,
> humain contre IA comme IA contre IA. En partie réelle (`make bench-games`),
> l'IA met **137 ms en moyenne** par coup et atteint la **profondeur 10 sur
> 96 %** des coups non forcés. L'avancement détaillé est dans
> [docs/HANDOFF.md](docs/HANDOFF.md), le travail restant côté interface dans
> [docs/FRONT.md](docs/FRONT.md).

## La forme

Un binaire C++ autonome, `Gomoku`, qui sert l'interface en HTTP et dialogue
avec elle en WebSocket. Une coque Electron **optionnelle** par-dessus, pour
avoir une vraie fenêtre d'application.

```
make       → Gomoku           binaire C++ ; jouable dans un navigateur ordinaire
make app   → Gomoku-desktop   fenêtre Electron (aujourd'hui sur le moteur factice)
```

`make` ne dépend jamais d'Electron ni de npm : si la coque casse, `./Gomoku` +
navigateur reste une démonstration valide. Voir ADR-005.

```bash
make
./Gomoku                      # ouvre le navigateur sur le port choisi
./Gomoku --no-browser --port 8642
```

Options du binaire : `--port N` (essaie les 20 ports suivants s'il est pris),
`--no-browser`, `--ui <chemin>` (défaut : `ui/dist` à côté du binaire),
`--parent-watchdog` (voir [docs/ELECTRON.md](docs/ELECTRON.md)).

## Cibles du Makefile

| commande | effet |
|---|---|
| `make` | compile `Gomoku` (et ne relink pas) |
| `make re` / `clean` / `fclean` | les cibles imposées par le sujet |
| `make test` | batterie de tests : règles, plateau, éval, recherche, JSON |
| `make bench` | profondeur et nœuds/s sur 4 positions fixes de début de partie, les plus dures |
| `make bench-games` | 8 parties moteur contre moteur : temps moyen, répartition des profondeurs — ce que le sujet juge |
| `make fuzz` | attaque le protocole WebSocket (nécessite node) ; après `make debug`, sous ASan/UBSan |
| `make debug` | reconfigure en AddressSanitizer + UBSan et recompile |
| `make release` | revient au build optimisé |
| `make format` / `format-check` | clang-format |
| `make ui` | recompile `ui/dist` (nécessite node) |
| `make app` | construit et lance la coque Electron (nécessite node) |
| `make mock` / `make dev` | moteur factice, pour travailler l'interface sans le C++ |
| `make help` | liste les cibles |

`make test` et `make bench` sont les deux garde-fous après toute modification
du moteur.

## Arborescence

```
backend/include/gomoku/   interfaces partagées — le contrat entre les 4 modules
backend/src/core/         plateau, règles, génération de coups        (module 1)
backend/src/eval/         heuristique : table de motifs               (module 3)
backend/src/search/       negamax alpha-bêta, TT, PVS, faisceau        (module 2)
backend/src/net/          HTTP + WebSocket (civetweb vendoré)         (module 4)
backend/src/server/       protocole, thread de partie, temps          (module 4)
backend/tests/ bench/     tests, fuzz du protocole, banc de mesure
backend/thirdparty/       civetweb v1.16, nlohmann/json v3.11.3 (ADR-003)
ui/                       interface React + TypeScript                (module 4)
app/                      coque Electron                              (module 4)
tools/mock-engine.mjs     moteur factice en Node
docs/                     architecture, protocole, règles, organisation
```

## Développer l'interface sans le moteur C++

`tools/mock-engine.mjs` est un **moteur factice** en Node, sans dépendance : il
annonce son port sur stdout comme le binaire, sert `ui/dist` et implémente le
protocole de [docs/PROTOCOL.md](docs/PROTOCOL.md). Il pose des pierres, alterne
les couleurs et détecte un alignement de cinq, sans arbitrer captures ni
double-trois. Sa recherche est simulée (une dizaine de `progress` sur ~400 ms
puis un coup aléatoire).

```bash
make dev     # moteur factice + Vite sur :5173, avec le proxy /ws
make mock    # moteur factice qui sert ui/dist sur :8642
```

Il ne suit pas toutes les évolutions du vrai moteur (arrêt en IA contre IA,
bornes des réglages) : en cas de doute, c'est `./Gomoku` qui fait foi. La liste
est dans [docs/FRONT.md](docs/FRONT.md).

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — découpage, ADR, chemin critique
- [docs/RULES.md](docs/RULES.md) — les règles et leurs cas limites
- [docs/PROTOCOL.md](docs/PROTOCOL.md) — WebSocket, commandes, événements, budget de temps
- [docs/INTEGRATION.md](docs/INTEGRATION.md) — comment le moteur est branché sur l'interface
- [docs/ELECTRON.md](docs/ELECTRON.md) — la coque Electron et ses trois canaux de communication
- [docs/WORKFLOW.md](docs/WORKFLOW.md) — répartition à 4, git, jalons
- [docs/HANDOFF.md](docs/HANDOFF.md) — où on en est, ce qui reste, pièges déjà payés
- [docs/FRONT.md](docs/FRONT.md) — ce qui reste à faire sur l'interface et la coque

## Les contraintes du sujet, en un coup d'œil

- exécutable nommé **`Gomoku`**, produit par un Makefile qui **ne relink pas**
- l'IA cherche **au moins 10 niveaux** de profondeur
- **moins de 0,5 s en moyenne** pour trouver un coup
- **aucun crash, jamais**, même en manque de mémoire — sinon note 0
- un **chronomètre visible** dans l'interface — sans lui, projet non validé
- chacun doit savoir **expliquer en détail** le minimax et l'heuristique
