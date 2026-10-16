# Règles — spécification et cas limites

Version 42 du Gomoku. Ce document est la référence du **module 1** : chaque
section se termine par les cas que couvre `backend/tests/` (`test_rules.cpp`,
`test_game.cpp`).

Les noms de fonctions cités (`rules::createsDoubleThree`, `Game::updateStatus`…)
sont ceux de `backend/include/gomoku/rules.hpp` et `game.hpp`, implémentés
dans `backend/src/core/`.

## 1. Base

- Goban **19×19**, pas de limite de pierres.
- Noir commence.
- **5 alignées ou plus** gagnent (le sujet tranche explicitement « 5 or more »).

## 2. Capture

Poser une pierre qui encadre exactement **deux** pierres adverses les retire :

```
  X O O .        →  X . . X      (X joue à droite)
```

Points durs :

- **Exactement deux.** `X O O O X` ne capture rien.
- **On peut se poser dans une capture.** Si Blanc joue entre deux Noirs pour
  former `N B B N`, rien n'est capturé : c'est le joueur *encadrant* qui doit
  poser la dernière pierre.
- Mais la paire reste **vulnérable plus tard** : si un des Noirs encadrants
  disparaît puis est rejoué, la capture a bien lieu (annexe du sujet).
- Un seul coup peut capturer **jusqu'à 4 paires** (croisement de 4 axes) — d'où
  `PlayedMove::captured` dimensionné à 8.
- Les intersections libérées redeviennent jouables normalement.
- **10 pierres capturées (5 paires) = victoire.**

Tests : capture dans les 8 directions ; capture multiple en un coup ;
capture au bord du plateau ; `X O O O X` ne capture rien ; se poser entre deux
adverses ne se fait pas capturer ; la même paire devient capturable après qu'un
encadrant a été repris ; victoire à la 5ᵉ paire ; `unmakeMove` restaure bien les
pierres capturées **et** le compteur.

## 3. Double-trois interdit

Un **trois-libre** est un alignement de trois qui, s'il n'est pas bloqué
immédiatement, donne un **quatre ouvert** (quatre avec ses deux extrémités
libres). Les deux formes comptent :

```
  . X X X .          (contigu)
  . X X . X .        (troué)
```

Jouer un coup qui crée **deux** trois-libres est interdit.

Points durs :

- **Créer un double-trois par capture est autorisé.** Le sujet le dit
  explicitement. `Game::check()` teste donc la capture **avant** le
  double-trois et sort tôt si le coup capture.
- Un trois dont une extrémité est bloquée par une pierre adverse ou par le bord
  n'est pas libre — il ne compte pas.
- **Interprétation retenue : non récursive.** Pour savoir si un trois est
  libre, on simule le coup qui en ferait un quatre ouvert sans vérifier que ce
  coup imaginaire serait lui-même légal. Le renju « pur » définit cela
  récursivement ; c'est hors périmètre et à assumer en soutenance.

Implémentation : `rules::createsDoubleThree` pose la pierre, compte au plus un
trois-libre par axe (4 axes), et restaure le plateau.

Tests : croix de quatre pierres → le centre est interdit ; un seul
trois → autorisé ; trois troué (`. X X . X .`) ; trois bloqué d'un côté →
autorisé ; double-trois au bord ; **double-trois par capture → autorisé** ;
double-trois sur deux diagonales.

## 4. Endgame capture

Un alignement de 5 ne gagne pas automatiquement :

1. Si l'adversaire peut **casser la ligne** en capturant une paire qui en fait
   partie, la partie continue. L'alignement est « en sursis » : s'il tient
   encore après le coup adverse, son auteur gagne.
2. Si l'auteur de l'alignement a **déjà perdu 4 paires** et que l'adversaire
   peut en capturer une cinquième, c'est **l'adversaire qui gagne**, même si
   cette capture ne casse pas la ligne.
3. Si aucun de ces cas n'est possible, la partie s'arrête immédiatement.

La règle ne s'applique que si les **captures sont activées** : sans captures,
aucune ligne n'est cassable et tout cinq gagne immédiatement.

Implémentation : `Game::updateStatus` résout l'alignement de **l'adversaire en
premier** — c'est ce qui fait gagner un alignement en sursis que le coup
adverse n'a pas cassé.

Tests — **c'est la partie la plus subtile du sujet, et celle que les
correcteurs sondent** : cinq cassable → partie continue ; cinq cassable non cassé → victoire au tour
suivant ; cinq incassable → victoire immédiate ; 4 paires perdues + capture
possible → défaite de celui qui aligne ; paire capturable **hors** de la ligne
ne casse rien.

## 5. Nulle

Plateau plein sans vainqueur. En pratique quasi impossible, mais le code doit
le gérer sans boucler (`GameStatus::Draw`, `WinReason::BoardFull`).

## 6. Bonus — conditions de départ

Choisies au lancement d'une partie (`config.opening`, voir PROTOCOL.md). Les
règles de placement sont dans `Game::check`, l'enchaînement des étapes dans
`Game::openingStep`, `Game::actor` et `Game::choose`.

- **Standard** : Noir commence, sans contrainte.
- **Pro** : la première pierre noire va au **centre** (K10). La deuxième pierre
  noire, troisième coup de la partie, doit être à **3 intersections au moins**
  du centre (distance de Tchebychev ≥ 3, donc hors du carré 5 × 5 central).
  Les coups blancs sont libres.
- **Long Pro** : comme Pro, mais à **4 intersections au moins** (hors du carré
  7 × 7). Si aucun coup légal n'existe dans le voisinage habituel, la
  recherche élargit à tout le plateau.
- **Swap** : le premier joueur (place Noire) pose les **3 premières pierres**,
  noire, blanche, noire. Le second joueur (place Blanche) **choisit sa
  couleur** ; prendre Noir échange les places. Blanc joue ensuite le 4ᵉ coup.
- **Swap2** : même début, mais le second joueur a une troisième option :
  **poser 2 pierres de plus** (une blanche, une noire) et rendre le choix de
  couleur au premier joueur. Blanc joue ensuite le 6ᵉ coup.

Points durs :

- Pendant un choix, tout coup est refusé (`opening_rule`).
- Les places sont nommées par la couleur qu'elles tenaient au départ : tant que
  personne n'a choisi, la place Noire est le premier joueur. `Game` ne connaît
  que les couleurs ; c'est `Session` qui échange les joueurs humain et IA quand
  `Game::colorsSwapped` change.
- `undo` qui repasse avant le coup précédant un choix l'annule, et les places
  reviennent comme avant.
- Comme pour le double-trois, la recherche ne vérifie les règles d'ouverture
  qu'à la racine : l'IA ne propose jamais un coup qui les viole.

Tests : `pro_opening_starts_on_the_centre_and_keeps_away`,
`long_pro_opening_needs_four_intersections`,
`swap_lets_the_second_player_take_black`, `swap2_can_hand_the_choice_back`,
`undo_cancels_an_opening_choice`, `standard_opening_never_asks_for_a_choice`.
