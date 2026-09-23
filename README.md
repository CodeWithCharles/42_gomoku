# Gomoku

Projet 42 — une IA capable de battre un joueur humain au Gomoku (variante
captures + interdiction du double-trois), avec une interface web.

> **État : rien n'est encore écrit.** Le dépôt ne contient que la
> documentation ci-dessous : architecture, protocole, règles et organisation.
> Tout le reste est à construire.

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

| commande | effet |
|---|---|
| `make` | compile `Gomoku` (et ne relink pas) |
| `make re` | recompile de zéro |
| `make test` | batterie de tests des règles |
| `make bench` | profondeur atteinte et nœuds/s — le critère du sujet |
| `make debug` | compile avec AddressSanitizer + UBSan |
| `make ui` | recompile `ui/dist` (nécessite node) |
| `make app` | construit la coque Electron (nécessite node) |

Options prévues pour le binaire : `--port N`, `--no-browser`, `--ui <chemin>`.

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

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — découpage, ADR, chemin critique
- [docs/RULES.md](docs/RULES.md) — les règles et leurs cas limites
- [docs/PROTOCOL.md](docs/PROTOCOL.md) — WebSocket, commandes, événements
- [docs/ELECTRON.md](docs/ELECTRON.md) — la coque Electron et ses trois canaux de communication
- [docs/WORKFLOW.md](docs/WORKFLOW.md) — répartition à 4, git, jalons

## Les contraintes du sujet, en un coup d'œil

- exécutable nommé **`Gomoku`**, produit par un Makefile qui **ne relink pas**
- l'IA cherche **au moins 10 niveaux** de profondeur
- **moins de 0,5 s en moyenne** pour trouver un coup
- **aucun crash, jamais**, même en manque de mémoire — sinon note 0
- un **chronomètre visible** dans l'interface — sans lui, projet non validé
- chacun doit savoir **expliquer en détail** le minimax et l'heuristique
