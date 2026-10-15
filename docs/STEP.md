# Mode pas à pas en IA contre IA

> Plan d'implémentation d'un mode qui permet d'arrêter une partie IA contre IA,
> d'avancer coup par coup et de revenir sur les positions déjà jouées pour
> analyser ce que l'IA a pensé. Le protocole est décrit dans
> [PROTOCOL.md](PROTOCOL.md), le raccordement moteur / interface dans
> [INTEGRATION.md](INTEGRATION.md).

## Décisions

| sujet | décision |
|---|---|
| portée | avance coup par coup + navigation arrière **en lecture seule**, pas d'embranchement |
| Lecture / Pause | état basculable à tout moment pendant une partie IA contre IA ; une partie démarre en Lecture |
| Pause pendant une recherche | la recherche **va au bout**, le coup est joué, la boucle s'arrête avant le suivant |
| `stop` | garde son sens (arrêt immédiat, le meilleur coup partiel est joué) et laisse la partie en pause |
| suspension | un seul état visible, `paused` ; `resume` et `step` en sortent |
| analyse affichée | stats du coup (score, profondeur, nœuds, temps), meilleur coup, heat map des `rootScores` présentés comme **des bornes** |
| variation principale | hors périmètre (`pv` ne contient que le meilleur coup, `search.cpp`) |
| stockage de l'analyse | côté serveur, `Stats` complet par coup, enregistré au moment où le coup est joué |
| navigation | entièrement dans l'interface, reconstruite depuis `history` ; le moteur n'est pas touché |
| commandes | ← et → déplacent le curseur ; → sur le dernier coup demande un pas ; Lecture ramène au présent ; ← en Lecture met en pause |
| modes | Lecture / Pause / Pas en IA contre IA seulement ; navigation dans tous les modes ; un clic sur le goban dans le passé ramène au présent sans jouer |
| ouverture Swap / Swap2 | le choix de couleur de l'IA est un pas à part entière |
| chronomètre | ne compte que le temps de recherche, jamais la pause |
| clavier | Espace (Lecture / Pause), ← →, Début, Fin ; muets dans un champ de saisie |
| présentation | le visuel est confié à Claude Design (voir [Prompt Claude Design](#prompt-claude-design)) ; la logique est écrite avant, contre un contrat de props |

## Ordre de livraison

1. Moteur et protocole, moteur factice, fuzz.
2. Logique d'interface sans visuel, branchée sur des boutons bruts.
3. Prompt Claude Design (dernière section de ce document).
4. Intégration des composants livrés par Claude Design.

Les étapes 2 et 3 avancent en parallèle une fois le contrat de props figé.
Chaque étape se livre et se vérifie seule.

## Étape 1 : moteur et protocole

`ui/src/protocol.ts`, `backend/src/server/session.cpp` et `docs/PROTOCOL.md`
changent **dans le même commit** (PROTOCOL.md, introduction).

### Protocole

Commandes ajoutées :

| `type` | champs | effet |
|---|---|---|
| `pause` | aucun | suspend la boucle de l'IA **après** l'action en cours ; ne coupe pas la recherche |
| `resume` | aucun | lève la suspension et relance la boucle |
| `step` | aucun | exécute **une** action de l'IA (coup ou choix de couleur) sans attente `kAiPace`, puis reste en pause |

Champs ajoutés à `state` :

```json
{ "paused": false,
  "history": [ { "idx": 180, "player": "black", "captured": [],
                 "stats": { "…": "voir Stats" } } ],
  "choices": [ { "afterMove": 3, "choice": "black", "swapped": true,
                 "actor": "white", "stats": { "…": "voir Stats" } } ] }
```

- `history[i].stats` : présent seulement pour un coup joué par l'IA. Absent
  pour un coup humain.
- `choices` : tous les choix de couleur Swap / Swap2, dans l'ordre.
  `afterMove` est le nombre de pierres posées au moment du choix, `choice` vaut
  `black`, `white` ou `place2`, `swapped` indique si les places ont été
  échangées, et `stats` n'est présent que pour un choix de l'IA. Le tableau est
  vide hors Swap / Swap2.
- `pause`, `resume` et `step` sont acceptées dans tous les modes. L'interface
  ne les propose qu'en IA contre IA.
- Nouveaux événements de `state` : `pause`, `resume` et `stop`. `stop` pousse
  désormais un `state`, sans quoi l'interface ne verrait pas `paused` passer à
  vrai après un arrêt entre deux coups. `step` n'a pas d'événement propre : le
  `ai_move` ou le `choice` qui suit en tient lieu.
- Dans `protocol.ts`, `paused` et `choices` sont optionnels, comme `opening` :
  un moteur qui ne les envoie pas équivaut à `false` et `[]`.

### `Game` (`backend/include/gomoku/game.hpp`)

- Rendre public le type des choix, aujourd'hui `ChoiceRecord` privé, et
  ajouter un accesseur en lecture `choices()`. C'est un ajout de fonction, ce
  qui est libre par convention : aucune signature existante ne change.
- `Game` ne stocke aucune stats : le cœur ignore la recherche.

### `Session` (`backend/src/server/session.cpp`, `session.hpp`)

**Suspension.** On garde `halted_` comme signal "couper la recherche en
cours" : `think` le relit pour réarmer `requestStop`. On ajoute `paused_`
(atomique), l'état exposé :

| événement | `halted_` | `paused_` | autre |
|---|---|---|---|
| `stop` | vrai | vrai | `requestStop` (comme aujourd'hui) |
| `pause` | inchangé | vrai | pas de `requestStop` |
| `resume` | faux | faux | `scheduleAiTurn` |
| `step` | faux | reste vrai | `stepPending_ = true`, `scheduleAiTurn` |
| `new-game`, `play`, `choose` humain | faux | faux | comme aujourd'hui pour `halted_` |

- `pause` est traitée **sur le thread civetweb**, comme `stop` : le drapeau est
  posé sous `queueMutex_`, puis `notify_all` pour couper l'attente `kAiPace`.
  En file, elle passerait derrière l'`AiTurn` suivant.
- `resume` et `step` passent par la file.
- `scheduleAiTurn` refuse si `paused_ && !stepPending_`. `aiTurn` consomme
  `stepPending_` et saute `kAiPace` dans ce cas.
- `state.paused` reflète `paused_`.

**Stats par action.**
- `std::vector<SearchStats> moveStats_` est aligné sur `game_.history()`, avec
  une entrée vide (`best == kNoIdx`) pour un coup humain. Il est alimenté dans
  `aiTurn` et `play`.
- `choiceStats_` est aligné sur `game_.choices()`. Il est alimenté dans
  `aiChoose` et `chooseColour`.
- `new-game` vide les deux tableaux. Après `undo`, ils sont tronqués à la
  taille de `history()` et `choices()`.
- `buildState` ajoute `stats` à une entrée seulement si elle n'est pas vide, et
  sérialise `choices`.
- Les stats d'une `suggest` ne sont pas stockées.

**Taille.** Au pire 361 coups × (stats + 20 `rootScores`), soit quelques
centaines de Kio par `state`, sous la limite de 1 Mio qui ne s'applique
qu'aux messages entrants. À mesurer avec `bench-games` (voir Vérifier).

### Moteur factice (`tools/mock-engine.mjs`)

- Ajouter `pause`, `resume`, `step` et `paused`, ainsi que `stats` dans
  `history` pour les coups de l'IA, et `choices: []`.
- Comme indiqué dans FRONT.md, le moteur factice n'enchaîne pas avec une pause
  entre deux coups d'IA et ne se suspend pas sur `stop`. Il faut au minimum lui
  donner la même suspension que le vrai moteur.

### Fuzz (`backend/tests/fuzz_protocol.mjs`)

- Ajouter `pause`, `resume`, `step` à la liste `kinds` du scénario "command
  storm during an AI versus AI game".
- Ajouter un scénario : partie IA contre IA, `pause`, puis vérifier qu'aucun
  `ai_move` n'arrive pendant une seconde. Ensuite `step`, puis vérifier qu'il
  arrive **exactement un** `ai_move` et que `paused` vaut `true`.

### Vérifier

- `make test` reste vert ; ajouter dans `backend/tests/test_game.cpp` un test
  de `choices()` (Swap, choix qui échange, undo qui retire le choix).
- `make fuzz` passe, nouveaux scénarios compris.
- `make bench-games` : la moyenne par coup reste sous 0,5 s et la profondeur ne
  bouge pas.
- `make debug` puis une partie IA contre IA avec pause, pas, reprise et stop :
  aucun rapport ASan / UBSan.

Aucun test unitaire ne couvre `Session` (`backend/tests/`) ; on n'en crée pas.

## Étape 2 : logique d'interface

Tout ce qui suit est écrit sans travail visuel. Le temps de l'étape, les
contrôles sont des boutons bruts dans `Controls.tsx`.

### Données

- `protocol.ts` (étape 1) : `paused`, `choices: ChoiceEntry[]`,
  `HistoryEntry.stats?: Stats`, et les commandes `pause`, `resume`, `step`.
- `GameContext` / `EngineProvider` exposent `paused` et `choices`.
- Nouveau module pur `ui/src/timeline.ts` :
  - `buildTimeline(history, choices)` produit la liste ordonnée des actions
    (`{ kind: 'move', … } | { kind: 'choice', … }`), chacune avec ses stats
    éventuelles ;
  - `boardAt(history, moves)` reconstruit les 361 cases après `moves` coups, en
    retirant les pierres de `captured[]`. Aucune règle n'est réimplémentée : les
    captures viennent du moteur.

### Curseur

Dans `ViewContext` :

- `cursor: number | null`, où `null` signifie "présent" et un entier est une
  position dans la timeline ;
- les actions `first`, `back`, `forward`, `last`, `goLive`.

| action | effet |
|---|---|
| `back` en Lecture (IA contre IA) | envoie `pause`, puis recule |
| `forward` sur le dernier élément, en pause, IA contre IA | envoie `step` |
| `forward` sur le dernier élément, sinon | ne fait rien |
| `forward` qui atteint la fin | `cursor = null` |
| Lecture | `cursor = null` et envoie `resume` |
| clic sur le goban avec `cursor !== null` | `cursor = null`, aucun coup joué |
| `state` `new_game`, ou timeline plus courte que le curseur (undo) | `cursor = null` |

### Affichage

- `Goban` affiche `boardAt(…)` quand `cursor !== null`, sinon `board`.
- `SearchPanel` et la heat map du `Goban` :
  - pendant une recherche : la progression en direct, comme aujourd'hui ;
  - sinon, avec `cursor !== null` ou en pause : les `stats` de l'élément sous le
    curseur (ou du dernier) ;
  - la heat map est alors dessinée hors recherche, avec la mention "bornes".
- Le chronomètre n'est pas touché : il ne compte déjà que les recherches.

### Clavier

Ajouter à `useShortcuts` dans `Controls.tsx` :

| touche | action |
|---|---|
| `' '` (Espace) | Lecture / Pause |
| `ArrowLeft`, `ArrowRight` | `back`, `forward` |
| `Home`, `End` | `first`, `last` |

`useShortcuts` ignore déjà les champs de saisie et la répétition automatique
(`ui/src/useShortcuts.ts`) : garder une touche enfoncée n'enchaîne donc pas les
pas.

### Vérifier

- `make ui` compile sans erreur.
- Avec le moteur factice puis avec le vrai binaire (`./Gomoku --no-browser
  --port 8642`) :
  - une partie IA contre IA à mettre en pause, avancer, reculer et reprendre ;
  - une partie en Swap2, où le choix apparaît comme un pas ;
  - une partie humain contre IA, avec navigation puis clic qui ramène au
    présent.

## État d'avancement

Étapes 1, 2 et 4 faites ; les étapes 2 et 4 ont été menées ensemble, le
retour de Claude Design étant arrivé avant les boutons provisoires.

Écarts avec le plan :

- `choices[].actor` ajouté au protocole (et `ChoiceRecord::actor` dans
  `Game`) : sans lui, l'interface devait rejouer la règle d'ouverture pour
  savoir qui a choisi.
- La barre de lecture et l'historique affichent "position N / M" et non
  "coup N / M" : une position compte aussi les choix de couleur.
- "l'IA finit son coup" remplace "pas en cours de calcul", juste aussi
  quand une pause attend la fin d'une recherche.
- L'interrupteur des numéros de coups passe de l'historique à `Controls`.
- Logique : `ui/src/timeline.ts`, `ui/src/view/useTimeline.ts`,
  `ui/src/components/Playback.tsx`.

Place dans la colonne de gauche : à la taille par défaut de la fenêtre
(1280 x 820), l'historique n'avait déjà que 30 px avant la barre de lecture.
Les sections Partie, Joueurs et Règles sont donc repliables
(`ui/src/components/Fold.tsx`), et l'état replié est retenu dans
`localStorage`. Règles est repliée au premier lancement. Partie et Règles repliées, l'historique passe à 139 px. Le
chronomètre, la barre de lecture et l'historique ne se replient pas ; le
choix de couleur et la dernière erreur restent visibles même repliés.

## Étape 4 : intégration du retour Claude Design

- Remplacer les boutons bruts par les composants livrés, branchés sur les props
  du contrat ci-dessous. Aucune logique ne doit migrer dans les composants.
- Vérifier : `make ui`, puis le parcours de l'étape 2.

## Définition de terminé

- Les quatre étapes sont livrées et `make test`, `make fuzz` et
  `make bench-games` passent.
- `PROTOCOL.md` décrit `pause`, `resume`, `step`, `paused`, `stats` et `choices`.
- FRONT.md, item 10 (resume + paused) : marqué comme fait.

## Prompt Claude Design

> À copier tel quel dans la conversation Claude Design qui a produit le
> tableau de bord actuel.

---

**Contexte.** On ajoute au tableau de bord Gomoku que tu as conçu un **mode pas
à pas** pour analyser les parties IA contre IA. La logique est déjà écrite côté
React. J'ai besoin de toi **pour la présentation seulement** : des composants
qui reçoivent des props et ne gardent aucun état métier.

Ce que fait le mode :

- En IA contre IA, on peut mettre la partie en **Pause** à tout moment.
  L'IA finit le coup qu'elle calcule, puis s'arrête.
- En pause, **Pas** fait jouer une seule action de l'IA : un coup, ou un
  choix de couleur en ouverture Swap / Swap2.
- Dans tous les modes, on peut **revenir en arrière** sur les positions déjà
  jouées, en lecture seule. On y voit la position et l'analyse que l'IA avait
  à ce moment : score, profondeur, nœuds, temps, meilleur coup et heat map
  des coups candidats.
- **Les scores de la heat map sont des bornes, pas des valeurs exactes.** Seul
  le meilleur coup a un score exact. L'interface doit le faire comprendre.
- Revenir au présent : bouton Lecture, touche Fin, ou clic sur le goban.
- Clavier : Espace (Lecture / Pause), ← → (reculer / avancer ; → au dernier coup
  fait un Pas), Début / Fin.

**Composants demandés.**

1. **Barre de lecture** : premier, reculer, Lecture / Pause, avancer / Pas,
   dernier, plus un indicateur de position ("coup 12 / 40", "en direct").
2. **Historique navigable** : la liste des coups existante (MoveHistory)
   devient cliquable. Il faut y mettre en évidence l'élément sous le curseur
   et afficher les choix de couleur comme des entrées à part ("Blanc choisit
   Noir, places échangées").
3. **Indicateur "vue du passé"** sur ou autour du goban : on doit voir tout de
   suite qu'on ne regarde pas la position actuelle, et comment revenir.
4. **En-tête d'analyse** dans le panneau de recherche (SearchPanel) : il
   indique si les stats sont celles de la recherche en direct ou celles du
   coup N, et explique que la heat map donne des bornes.

**Contrat de props** (fixé, ne pas modifier) :

```ts
import type { Color, OpeningOption, Stats } from '../protocol';

export type PlaybackState = 'playing' | 'paused' | 'searching';

export interface PlaybackBarProps {
  aiOnly: boolean;
  playback: PlaybackState;
  position: number | null;
  length: number;
  canBack: boolean;
  canForward: boolean;
  forwardSteps: boolean;
  onFirst: () => void;
  onBack: () => void;
  onTogglePlay: () => void;
  onForward: () => void;
  onLast: () => void;
}

export type TimelineItem =
  | { kind: 'move'; number: number; idx: number; player: Color; captured: number; byAi: boolean }
  | { kind: 'choice'; actor: Color; choice: OpeningOption; swapped: boolean; byAi: boolean };

export interface MoveHistoryProps {
  items: TimelineItem[];
  cursor: number | null;
  onSelect: (position: number) => void;
}

export interface PastViewProps {
  position: number;
  length: number;
  onGoLive: () => void;
}

export interface AnalysisHeaderProps {
  source: 'live' | 'move' | 'none';
  position: number | null;
  stats: Stats | null;
}
```

- `position` / `cursor` : `null` = présent.
- `forwardSteps` : vrai quand "avancer" déclenchera un Pas de l'IA plutôt
  qu'un déplacement dans l'historique.
- `aiOnly` : faux en partie avec un humain. Lecture / Pause n'existe alors
  pas, seule la navigation reste.

**Contraintes.**

- React 19 + TypeScript, CSS simple avec les tokens existants du tableau de
  bord, sans nouvelle dépendance.
- Pas de logique, pas d'appel réseau, aucune modification de `protocol.ts`.
- Code en anglais. Un commentaire court au-dessus de chaque fonction, aucun
  commentaire dans le corps ni dans le JSX.
- La fenêtre Electron est sans cadre et à trois colonnes. Le goban reste au
  centre et ne doit pas rétrécir.

**Ce que je veux en retour.** Un dossier contenant :

- les composants (`.tsx` + `.css`) ;
- une maquette ou capture des états : Lecture, Pause, recherche en cours, vue
  du passé, partie humain contre IA, choix de couleur dans l'historique ;
- une note d'intégration : où chaque composant se place dans la mise en page,
  ce qu'il remplace, et les tokens ajoutés s'il y en a.

**Questions que je te laisse trancher, en justifiant :**

1. Où placer la barre de lecture : sous le goban, dans l'onglet "Partie", ou
   ailleurs ?
2. Comment signaler la vue du passé sans masquer le plateau ?
3. Comment distinguer un pas en attente (Pas en cours de calcul) d'une
   lecture continue ?
4. Comment montrer qu'un score est une borne : légende, style de la heat map,
   symbole ≤ / ≥ ?
5. Quels états désactivés, et avec quel retour visuel ?
6. Comment afficher un choix de couleur dans l'historique pour qu'on ne le
   prenne pas pour un coup ?
