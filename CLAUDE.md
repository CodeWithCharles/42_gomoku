# CLAUDE.md

Repères pour travailler dans ce dépôt.

## Projet

Gomoku 42 : une IA minimax qui bat un humain, avec interface web.
Le sujet impose des contraintes non négociables :

- exécutable nommé **`Gomoku`**, produit par un **Makefile** (`all`, `clean`,
  `fclean`, `re`, `$(NAME)`) qui **ne relink pas** ;
- l'IA doit chercher **au moins 10 niveaux** de profondeur ;
- **moins de 0,5 s en moyenne** pour trouver un coup ;
- **aucun crash, jamais** (même en manque de mémoire) — sinon note 0 ;
- un **chronomètre visible** dans l'interface — sans lui, projet non validé ;
- les règles imposées : captures de paires (10 pierres = victoire), endgame
  capture, interdiction du double-trois.

## État actuel

**Le dépôt ne contient que `docs/`, `README.md` et ce fichier.** Aucun code
n'est encore écrit. Tout ce qui suit décrit la cible, pas l'existant :
avant d'affirmer qu'un fichier ou une fonction existe, vérifiez-le.

La feuille de route et l'ordre des jalons sont dans
[docs/WORKFLOW.md](docs/WORKFLOW.md).

## Architecture visée

Un binaire C++ autonome qui sert `ui/dist` en HTTP et pousse ses événements en
**SSE**. Une coque **Electron optionnelle** le lance et l'affiche dans une
fenêtre native. Couches, du bas vers le haut :

```
src/core/    plateau + règles      (aucune dépendance hors types.hpp)
src/eval/    heuristique           (dépend de core)
src/search/  minimax alpha-bêta    (dépend de core, eval, game)
src/game/    arbitrage             (dépend de core)
src/net/     HTTP/SSE/JSON maison  (ne connaît PAS le Gomoku)
src/server/  protocole             (dépend de tout)
app/         coque Electron        (aucune logique de jeu)
```

La coque et ses trois canaux de communication avec le moteur sont décrits dans
[docs/ELECTRON.md](docs/ELECTRON.md).

La règle de dépendance est stricte et détaillée dans
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). En particulier : **aucune règle du
jeu en dehors de `src/core/rules.cpp`**, et `src/net/` reste générique.

## Commandes prévues

```bash
make              # compile Gomoku
make test         # batterie de tests des règles (doit rester verte)
make bench        # profondeur atteinte et nœuds/s
make debug        # build ASan + UBSan
make ui           # recompile ui/dist (nécessite node)
make app          # construit la coque Electron (nécessite node)
./Gomoku --no-browser --port 8642
```

`make test` et `make bench` sont les deux garde-fous : les lancer après toute
modification du moteur.

## Conventions

- C++20, `-Wall -Wextra -Werror`. Le build doit rester sans avertissement.
- `.clang-format` fait foi (`make format`).
- Les en-têtes de `include/gomoku/` sont un **contrat entre quatre personnes** :
  ajouter une fonction est libre, en changer ou en retirer une se discute.
- Commentaires en français, **sans accents dans le code C++** (les sources
  restent en ASCII pur) ; les accents sont autorisés dans `docs/`, `README.md`
  et `ui/`.
- Les noms de fonctions et variables restent en anglais.

## Pièges à connaître

- **`make` ne doit jamais dépendre d'Electron ni de npm** (ADR-005). `Gomoku`
  seul, dans un navigateur, doit rester une démonstration valide — c'est le
  filet de sécurité de la soutenance.
- **`ui/dist` est versionné** (ADR-006). Toute modification de `ui/src` impose
  `make ui` et le commit de `ui/dist` dans le même commit.
- **Trois fichiers doivent rester synchronisés** pour le protocole :
  `src/server/session.cpp`, `ui/src/protocol.ts`, `docs/PROTOCOL.md`.
- **Les `POST /api/*` ne renvoient pas l'état** : ils répondent `204`, et tout
  passe par le flux SSE. Une seule source de vérité, volontairement.
- **CORS en développement** : Vite sur `:5173` et le moteur sur `:8642` ne sont
  pas la même origine. La réponse est le proxy Vite, **pas** des en-têtes CORS
  dans le C++.
- **`Game` aura deux chemins** : `play()` valide tout (chemin UI),
  `makeMove`/`unmakeMove`/`terminalAfter` ne valident rien (chemin recherche,
  optimisé pour être annulable). Ne pas les confondre.
- **La recherche ne filtrera le double-trois qu'à la racine.** C'est assumé,
  mais il faut le savoir avant de « corriger » ce qui ressemble à un bug.
- `rules::findCaptures(b, i, p, …)` répondra « si `p` vient de jouer en `i`,
  que capture-t-il ? ». L'appeler sur une pierre déjà ancienne n'a pas de sens.
- Le serveur sera mono-thread : pendant la recherche, il ne lit pas les
  requêtes entrantes (ADR-007).

## Ordre de travail

Les règles et l'heuristique **avant** l'optimisation de la recherche :
chercher plus profond avec une évaluation fausse ne sert à rien.
