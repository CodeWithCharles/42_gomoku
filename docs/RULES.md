# Règles — spécification et cas limites

Version 42 du Gomoku. Ce document est la référence du **module 1** : chaque
section se termine par les tests à écrire dans `tests/`.

Les noms de fonctions cités (`rules::createsDoubleThree`, `Game::updateStatus`…)
décrivent l'**API visée**, pas du code existant : le dépôt ne contient encore
que cette documentation.

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

Tests à écrire : capture dans les 8 directions ; capture multiple en un coup ;
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

Tests à écrire : croix de quatre pierres → le centre est interdit ; un seul
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

Implémentation : `Game::updateStatus` résout l'alignement de **l'adversaire en
premier** — c'est ce qui fait gagner un alignement en sursis que le coup
adverse n'a pas cassé.

Tests à écrire — **c'est la partie la plus subtile du sujet, et celle que les
correcteurs sondent** : cinq cassable → partie continue ; cinq cassable non cassé → victoire au tour
suivant ; cinq incassable → victoire immédiate ; 4 paires perdues + capture
possible → défaite de celui qui aligne ; paire capturable **hors** de la ligne
ne casse rien.

## 5. Nulle

Plateau plein sans vainqueur. En pratique quasi impossible, mais le code doit
le gérer sans boucler (`GameStatus::Draw`, `WinReason::BoardFull`).

## 6. Bonus — conditions de départ

Prévoir un champ `GameConfig::opening` (`Standard`, `Pro`, `Swap`, `Swap2`)
dès le départ **sans l'implémenter** : ça coûte trois lignes et ça évite de
rouvrir l'API plus tard. À ne traiter que lorsque le mandatory est parfait —
le sujet précise que le bonus n'est pas évalué sinon.
