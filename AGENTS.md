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

## Architecture visée

Un binaire C++ autonome qui sert `ui/dist` en HTTP et dialogue avec l'interface
en **WebSocket**. Une coque **Electron optionnelle** le lance et l'affiche dans
une fenêtre native. Couches, du bas vers le haut :

```
src/core/    plateau + règles      (aucune dépendance hors types.hpp)
src/eval/    heuristique           (dépend de core)
src/search/  minimax alpha-bêta    (dépend de core, eval, game)
src/game/    arbitrage             (dépend de core)
src/net/     HTTP/WS/JSON maison   (ne connaît PAS le Gomoku)
src/server/  protocole             (dépend de tout)
app/         coque Electron        (aucune logique de jeu)
```

La coque et ses trois canaux de communication avec le moteur sont décrits dans
[docs/ELECTRON.md](docs/ELECTRON.md).

La règle de dépendance est stricte et détaillée dans
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). En particulier : **aucune règle du
jeu en dehors de `src/core/rules.cpp`**, et `src/net/` reste générique.

## Commandes

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

## Scope

- Use only the context needed for the requested task.
- Prefer repository-local conventions over generic assumptions.
- Do not modify unrelated files or behavior.
- Do not introduce new tools, dependencies, services, or workflows unless explicitly requested.
- Do not assume project-specific rules that are not documented in the repository.

## Context efficiency

- Start with the closest relevant files.
- Prefer file paths, symbols, and concise summaries over long excerpts.
- Avoid reading or summarizing large parts of the repository unless necessary.
- Ask for clarification only when the task cannot be completed safely without it.

## Change policy

- Make the smallest change that solves the request.
- Preserve existing architecture, naming, formatting, and public behavior.
- Do not perform broad refactors unless explicitly requested.
- Do not rewrite working code for style-only reasons.
- Do not remove tests or safeguards unless explicitly requested.
- Keep error and debug messages in English.

## Comments

- Inline comments are forbidden: no comment inside a function body, at the end of a line, or inside JSX.
- Every function must carry a short comment directly above it, describing what it does and, when the signature is not self-explanatory, its arguments.
- Keep that comment efficient: one or two lines, no restating of the code.
- Do not remove existing comments unless explicitly requested. When you change a function, keep the comment above it accurate.

## Safety

- Do not invent commands, scripts, test names, files, APIs, or configuration.
- Do not claim that validation was run unless it actually was.
- Do not make destructive changes unless explicitly requested.
- Do not discard, overwrite, or revert user changes unless explicitly requested.
- When unsure, state the assumption or limitation clearly.
- Do not try to modify, the api.ts file.

## Validation

- Use only validation steps that already exist in the repository or were provided by the user.
- Prefer the narrowest relevant validation.
- If no reliable validation is available, say so instead of guessing.
- Report failures with the relevant command or action and the observed result.

## Git

- Do not create commits unless explicitly requested.
- Do not amend, rebase, reset, force-push, or change branches unless explicitly requested.
- Do not stage files unless explicitly requested.

## Final response

When reporting work, use:

- `Changed:` files modified
- `Why:` concise reason for the change
- `Checked:` validation actually performed
- `Notes:` assumptions, limitations, or risks