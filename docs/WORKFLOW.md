# Organisation à 4

## Répartition

Chaque module a **un propriétaire** : c'est lui qui décide de l'implémentation
interne et qui, en soutenance, doit savoir l'expliquer en détail. Les autres
peuvent y toucher, mais via une PR relue par le propriétaire.

| # | module | périmètre | livrable |
|---|---|---|---|
| **1** | Plateau & règles | `include/gomoku/{types,board,rules,movegen}.hpp`, `src/core/`, `tests/` | règles exactes, batterie de tests verte, puis bitboards |
| **2** | Recherche | `include/gomoku/search.hpp`, `src/search/`, `bench/` | profondeur ≥ 10 en < 0,5 s |
| **3** | Heuristique | `include/gomoku/eval.hpp`, `src/eval/` | évaluation rapide, incrémentale, et qui **joue bien** |
| **4** | Plomberie & interface | `Makefile`, `src/net/`, `src/server/`, `ui/`, `app/` (Electron) | UI jouable, panneau de debug, chrono, coque Electron, aucun crash |

### Le module 4 est surchargé, sachez-le

Il porte le Makefile, la couche HTTP/SSE, le protocole, l'UI React **et** la
coque Electron. C'est plus que les trois autres. Deux conséquences :

- il démarre en premier (J0 débloque tout le monde) ;
- la coque Electron (J5) est le morceau le plus facile à déléguer ou à décaler
  — elle est indépendante du reste et ne conditionne pas la note.

### Ce que chacun doit savoir expliquer en soutenance

Le sujet est brutal là-dessus : « if you can not explain, in detail, your
implementation of the algorithm […] then you will not get any points for it ».

- **Tout le monde** doit savoir expliquer le minimax et l'heuristique, pas
  seulement les modules 2 et 3. Prévoyez une séance croisée où chacun explique
  le module d'un autre.
- Le panneau de debug de l'UI est votre meilleur support : profondeur, nœuds,
  coupures, variante principale, score par coup candidat sur le goban.

## Interfaces gelées

Les `.hpp` de `include/gomoku/` sont le **contrat** entre les modules. Ils
doivent être écrits et compiler **avant tout le reste** (jalon J0) : c'est ce
qui permet aux quatre de travailler sans s'attendre.

Règle : **on ne modifie pas une signature publique seul**. Ça se discute à 4,
parce qu'une signature qui change casse trois personnes en même temps.
Ajouter une fonction : libre. En changer ou en retirer une : discussion.

## Git

```
main        ← toujours compilable, tests verts
  └─ feat/2-transposition-table
  └─ feat/1-bitboard
  └─ fix/3-open-three-scoring
```

- Une branche par sujet, préfixée par le numéro de module : `feat/2-…`,
  `fix/1-…`, `perf/3-…`, `docs/…`.
- Pas de push direct sur `main`. PR + relecture par au moins une personne.
- Messages en impératif : `search: ajoute la table de transposition`.
- **Avant chaque push** : `make re && make test` doit passer.
- Si vous touchez `ui/src`, lancez `make ui` et commitez `ui/dist` dans le
  **même commit** (ADR-006).
- Si vous touchez le protocole, les trois fichiers de `docs/PROTOCOL.md`
  bougent ensemble, dans le même commit.

## Jalons

| jalon | critère de sortie | module |
|---|---|---|
| **J0** — fondations | `make` produit `Gomoku` et ne relink pas ; les en-têtes de `include/gomoku/` compilent ; `make test` tourne (même à vide) | 4 + 1 |
| **J1** — règles exactes | toute la batterie `tests/` verte, **endgame capture comprise** | 1 |
| **J2** — jouable | HTTP + SSE, protocole, goban React, chrono, hotseat, suggestion | 4 |
| **J3** — heuristique crédible | table de motifs, éval incrémentale ; l'IA bloque les quatre, voit les trois ouverts, joue les captures | 3 |
| **J4** — profondeur 10 | table de transposition, killers + history, PVS, menaces forcées → `make bench` `OK` partout sous 450 ms | 2 |
| **J5** — coque Electron | `make app`, fenêtre native, négociation du port, process enfant tué proprement — et `Gomoku` toujours autonome. Guide : [ELECTRON.md](ELECTRON.md) | 4 |
| **J6** — durcissement | `make debug` (ASan/UBSan) propre, partie IA vs IA complète, fenêtre tuée en plein calcul → le moteur survit | tous |
| **J7** — bonus | uniquement si J1-J6 sont parfaits (le sujet ne note pas le bonus sinon) | tous |

**L'ordre entre J1, J3 et J4 n'est pas négociable.** Chercher à la profondeur 10
sur une évaluation fausse, c'est chercher plus vite dans le vide.

J2 peut avancer en parallèle de J1 : le module 4 n'a besoin que des en-têtes,
pas des implémentations. Une IA qui joue au hasard suffit à valider toute la
plomberie.

## Avant la soutenance

- [ ] `make fclean && make` sur une machine propre, **sans `node_modules`**
- [ ] `make` deux fois de suite → « Nothing to be done » (pas de relink)
- [ ] `make test` vert
- [ ] `make bench` : profondeur ≥ 10 partout, temps moyen < 500 ms
- [ ] chronomètre visible à l'écran — sans ça, projet non validé
- [ ] `./Gomoku` seul + navigateur fonctionne, **sans Electron** (le filet)
- [ ] une partie complète sans crash, puis une partie IA vs IA jusqu'au bout
- [ ] fermer la fenêtre en plein calcul → pas de moteur orphelin qui garde le port
- [ ] chacun sait expliquer le module d'un autre
