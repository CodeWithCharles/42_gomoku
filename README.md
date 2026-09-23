# Gomoku

Projet 42 — une IA capable de battre un joueur humain au Gomoku (variante
captures + interdiction du double-trois), avec une interface web.

> **État : le moteur C++ reste à écrire.** `ui/` (React + TypeScript) et `app/`
> (coque Electron) existent et fonctionnent, branchés sur un moteur factice —
> voir « Développer sans le moteur C++ » plus bas. `src/`, `include/`, `tests/`
> et `bench/` sont encore vides.

## La forme visée

Un binaire C++ autonome, `Gomoku`, qui sert l'interface en HTTP et dialogue
avec elle en WebSocket. Une coque Electron **optionnelle** par-dessus, pour
avoir une vraie fenêtre d'application.

```
make       → Gomoku           binaire C++ ; jouable dans un navigateur ordinaire
make app   → Gomoku-desktop   fenêtre Electron qui lance ./Gomoku
```

`make` ne dépend jamais d'Electron ni de npm : si la coque casse, `./Gomoku` +
navigateur reste une démonstration valide. Voir ADR-005.

## Ce que le Makefile devra fournir

| commande | effet | état |
|---|---|---|
| `make` | compile `Gomoku` (et ne relink pas) | à écrire |
| `make re` | recompile de zéro | à écrire |
| `make test` | batterie de tests des règles | à écrire |
| `make bench` | profondeur atteinte et nœuds/s — le critère du sujet | à écrire |
| `make debug` | compile avec AddressSanitizer + UBSan | à écrire |
| `make ui` | recompile `ui/dist` (nécessite node) | **existe** |
| `make app` | construit la coque Electron (nécessite node) | **existe** |
| `make mock` | sert `ui/dist` via le moteur factice | **existe** |
| `make dev` | moteur factice + serveur Vite | **existe** |
| `make help` | liste les cibles disponibles | **existe** |

`make` sans argument affiche `make help` tant que `all` n'existe pas. Le
`Makefile` ne déclare **que** les cibles qui font quelque chose aujourd'hui.
Les cibles du sujet arriveront avec `src/` : une cible vide qui laisserait croire
que le moteur existe serait pire que son absence.

Options prévues pour le binaire : `--port N`, `--no-browser`, `--ui <chemin>`,
`--parent-watchdog` (voir [docs/ELECTRON.md](docs/ELECTRON.md)).

## Arborescence visée

```
include/gomoku/   interfaces partagées — le contrat entre les 4 modules
src/core/         plateau, règles, génération de coups        (module 1)
src/eval/         heuristique                                 (module 3)
src/search/       minimax / alpha-bêta                        (module 2)
src/game/         arbitrage de la partie                      (partagé)
src/net/          HTTP + WebSocket + JSON, sans dépendance    (module 4)
src/server/       protocole moteur ↔ interface                (module 4)
ui/               interface React + TypeScript                (module 4)
app/              coque Electron                              (module 4)
tests/ bench/     tests des règles, banc de mesure
docs/             architecture, protocole, règles, organisation
```

## Développer sans le moteur C++

Le moteur n'existe pas encore, mais l'interface et la coque sont complètes et
démontrables. `tools/mock-engine.mjs` est un **moteur factice** en Node, sans
aucune dépendance : il annonce son port sur stdout comme le fera le binaire,
sert `ui/dist` en HTTP et implémente le protocole de
[docs/PROTOCOL.md](docs/PROTOCOL.md) — handshake RFC 6455 écrit à la main,
démasquage XOR, ping toutes les 15 s.

```bash
make dev     # moteur factice + Vite sur :5173, avec le proxy /ws
make mock    # moteur factice qui sert ui/dist sur :8642
make app     # la fenêtre Electron, lancée sur le moteur factice
```

Il pose des pierres, alterne les couleurs et détecte un alignement de cinq. Il
**n'arbitre rien d'autre** : ni captures, ni double-trois. Ces règles sont celles
de `src/core/rules.cpp` et n'ont pas à être dupliquées ici. Sa recherche est
simulée : une dizaine d'événements `progress` sur ~400 ms, puis un coup
aléatoire — de quoi exercer le chronomètre et le panneau de recherche.

Quand `Gomoku` existera, il suffira de retirer `GOMOKU_ENGINE` des scripts et de
faire pointer `make mock` sur `./Gomoku`.

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — découpage, ADR, chemin critique
- [docs/RULES.md](docs/RULES.md) — les règles et leurs cas limites
- [docs/PROTOCOL.md](docs/PROTOCOL.md) — WebSocket, commandes, événements
- [docs/INTEGRATION.md](docs/INTEGRATION.md) — brancher le moteur C++ sur l'interface, étape par étape
- [docs/ELECTRON.md](docs/ELECTRON.md) — la coque Electron et ses trois canaux de communication
- [docs/WORKFLOW.md](docs/WORKFLOW.md) — répartition à 4, git, jalons

## Les contraintes du sujet, en un coup d'œil

- exécutable nommé **`Gomoku`**, produit par un Makefile qui **ne relink pas**
- l'IA cherche **au moins 10 niveaux** de profondeur
- **moins de 0,5 s en moyenne** pour trouver un coup
- **aucun crash, jamais**, même en manque de mémoire — sinon note 0
- un **chronomètre visible** dans l'interface — sans lui, projet non validé
- chacun doit savoir **expliquer en détail** le minimax et l'heuristique
