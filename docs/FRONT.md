# Interface et coque : ce qui reste à faire

> Liste de travail pour `ui/`, `app/` et `tools/mock-engine.mjs`, établie le
> **12 octobre 2026** après les évolutions du moteur (banque de temps, thread de
> partie, IA contre IA, arrêt immédiat). Le moteur n'attend rien de plus pour
> les points 1 à 9 : tout passe par le protocole actuel. Les points 10 à 12
> demandent une décision commune parce qu'ils touchent le protocole.
>
> Rappels : toute modification de `ui/src` s'accompagne de `make ui` et d'un
> commit de `ui/dist` **dans le même commit** (ADR-006). Toute modification du
> protocole touche `ui/src/protocol.ts`, `backend/src/server/session.cpp` et
> `docs/PROTOCOL.md` ensemble.

## Priorité 1 — avant la soutenance

### 1. Le chronomètre doit montrer la moyenne, pas seulement le dernier coup

**Pourquoi.** Le sujet exige « moins d'une demi-seconde **en moyenne** », et
sans chronomètre le projet n'est pas validé. Le moteur gère désormais une
banque de temps (PROTOCOL.md, « Budget de temps ») : un coup calme peut durer
jusqu'à **0,9 s** quand les coups précédents ont été rapides, et la moyenne
reste sous 0,45 s. Aujourd'hui, [Clock.tsx](../ui/src/components/Clock.tsx)
compare chaque coup à `BUDGET_MS = 500` écrit en dur et passe en rouge
au-delà. Un correcteur verra donc du rouge sur des coups parfaitement
légitimes.

**Quoi.**
- afficher **la moyenne de la partie** à côté du dernier coup, et colorer
  selon la moyenne ;
- calculer cette moyenne à partir de `lastStats.elapsedMs`, mesuré par le
  moteur, à chaque `state` d'événement `ai_move` ; la remettre à zéro sur
  `new_game` ;
- prendre la référence dans `limits.budgetMs` (la moyenne visée) au lieu de
  la constante ;
- pour le dernier coup, préférer aussi `lastStats.elapsedMs` à la mesure
  `Date.now()` du navigateur, qui inclut le transit.

**Critère.** Une partie entière contre l'IA : la moyenne affichée reste sous
0,5 s, et un coup isolé à 0,8 s n'apparaît pas comme une faute.

### 2. Pouvoir lancer une partie IA contre IA

**Pourquoi.** WORKFLOW.md exige « une partie IA vs IA jusqu'au bout » avant la
soutenance. Le moteur la joue (une pause de 300 ms entre deux coups pour
qu'on puisse la suivre), mais [Controls.tsx](../ui/src/components/Controls.tsx)
envoie `{ type: 'new-game' }` sans `players`. Il n'y a donc aucun moyen de la
lancer depuis l'interface.

**Quoi.** Au lancement d'une partie, un choix humain/IA pour chaque couleur,
envoyé dans `players`. Les options de `config` (captures, double-trois,
capture en fin de partie, ouverture) peuvent suivre le même chemin.

**Attention.** `new-game` **sans** `players` garde les places de la partie
précédente. Après une partie IA contre IA, « Nouvelle partie » en relance une.
Le plus simple est de toujours envoyer `players`.

**Critère.** Lancer IA contre IA depuis l'interface, la regarder jusqu'à la
fin, puis relancer une partie humain contre IA.

### 3. Le bouton Stop doit être actif dès que l'IA a le trait

**Pourquoi.** Le Stop fonctionne enfin côté moteur. Avant, il n'était même
pas lu pendant la recherche : il est maintenant traité immédiatement. Mais le
bouton n'est actif que si `searching`, c'est-à-dire après le premier
`progress`. En IA contre IA, il est donc désactivé pendant la pause de 300 ms
entre deux coups, soit une bonne partie du temps.

**Quoi.** Activer le bouton quand `status === 'ongoing'` et que
`players[toMove] === 'ai'`.

**Ce que fait `stop` côté moteur** (PROTOCOL.md, « Arrêt ») : l'IA joue le
meilleur coup trouvé jusque-là, quelques millisecondes plus tard. En IA contre
IA, la partie est ensuite **suspendue**, et seul `new-game` la relance (voir
point 10). Un `play` humain lève la suspension.

**Critère.** En IA contre IA, un clic sur Stop arrête la partie au coup près.

### 4. Reconstruire `ui/dist` et sortir les `tsbuildinfo` du dépôt

Déjà signalé dans HANDOFF.md :

- **`cd ui && npm run build`**, puis commiter `ui/dist`. Les corrections de
  source (`vite-env.d.ts`, `GameContext.ts`, `tsconfig.node.json`,
  `StatusBar.tsx`) sont faites ; à vérifier que le bundle commité en tient
  compte.
- **`ui/tsconfig.app.tsbuildinfo` et `ui/tsconfig.node.tsbuildinfo` sont
  commités** alors que ce sont des fichiers générés. Lancer
  `git rm --cached ui/*.tsbuildinfo` et ajouter `ui/*.tsbuildinfo` au
  `.gitignore`.

### 5. Basculer la coque Electron sur le vrai moteur

**Pourquoi.** `make app` lance toujours la coque sur `tools/mock-engine.mjs`
(`app: ui` puis `npm run start:mock` dans le `Makefile` racine). La
soutenance doit pouvoir se faire sur `./Gomoku`.

**Quoi.**
- `Makefile` racine : `app` dépend de `all` (et de `ui`), puis lance
  `npm start` au lieu de `npm run start:mock`. La recette est dans
  [ELECTRON.md](ELECTRON.md) ; `start:mock` reste disponible pour travailler
  l'interface.
- [app/main.js](../app/main.js) `stopEngine` : passer le `SIGKILL` de secours
  de 2 s à **4 s**. Avec des sockets ouvertes, le moteur met 1 à 2 s à
  s'arrêter proprement, et le `SIGKILL` tombe parfois en plein arrêt.

**Critère.**
- `make app` ouvre la fenêtre sur `./Gomoku`, et une partie se joue ;
- fermer la fenêtre **pendant que l'IA réfléchit**, puis `pgrep -x Gomoku` ne
  renvoie rien. N'utilisez pas `pgrep -f`, qui s'attrape lui-même.

## Priorité 2 — affichage juste

### 6. Panneau de recherche : expliquer les scores et les faibles profondeurs

**Pourquoi.** Environ un quart des coups d'une partie s'arrêtent avant la
profondeur 10, le plus souvent à 1 ou 2. Le moteur a **prouvé** un gain ou une
perte, et continuer à chercher ne servirait à rien. Affiché tel quel, « profondeur 2 » laisse croire à un
moteur paresseux, alors que le sujet exige 10 niveaux.

**Quoi.** Interpréter `score` dans
[SearchPanel.tsx](../ui/src/components/SearchPanel.tsx) :

| `score` | signification | à afficher |
|---|---|---|
| ≥ 999 000 | gain forcé, en `1 000 000 − score` demi-coups | « gain forcé en N » |
| ≤ −999 000 | perte forcée | « perte forcée en N » |
| ± 500 000 | position décisive selon l'éval (un quatre au trait…) | « décisif » |
| autre | évaluation ordinaire | la valeur |

Afficher aussi la profondeur visée (`limits.maxDepth`) à côté de la
profondeur atteinte.

### 7. Carte de chaleur : ne pas supposer `rootScores` trié

**Pourquoi.** [Goban.tsx](../ui/src/components/Goban.tsx) prend
`rootScores[0]` comme meilleur score et le dernier comme pire. Le moteur les
envoie **dans l'ordre où il les a cherchés**, pas triés. De plus, avec la PVS,
seul le meilleur coup a une valeur exacte : les autres sont des **majorants**
(« pas mieux que »). Les scores de gain forcé (±999 000) écrasent aussi toute
l'échelle de couleurs.

**Quoi.** Calculer le minimum et le maximum sur toute la liste, borner les
scores de mat avant la normalisation, et présenter la carte comme une
estimation. Au plus 20 coups racine y figurent, à cause du faisceau.

### 8. Réglages : libellés, bornes, synchronisation

Dans [Controls.tsx](../ui/src/components/Controls.tsx) :

- **libellé** : « budget (ms) » devient « budget moyen (ms) » ;
- **bornes** : le moteur **ignore sans erreur** une valeur hors bornes
  (`maxDepth` ∈ [1, 64], `budgetMs` ∈ [10, 60 000]). Le formulaire doit donc
  borner lui-même, sinon l'utilisateur croit avoir appliqué une valeur qui n'a
  rien changé ;
- **synchronisation** : `draft` est initialisé une seule fois depuis `limits`
  (`useState(limits)`). Il ne suit ni la reconnexion ni une réponse du moteur
  qui aurait ignoré une valeur. Le resynchroniser à chaque `state`
  d'événement `limits` ou `connected` ;
- **« candidats »** : le moteur **ignore actuellement `maxCandidates`** dans la
  commande `limits` (`Session::setLimits` ne le lit pas). C'est un écart entre
  le moteur et PROTOCOL.md, à trancher (voir point 12) : soit le moteur le
  prend en compte, soit le champ disparaît de l'interface. D'ici là, il est
  trompeur.

### 9. Annuler en IA contre IA

`undo` revient « à une position où c'est à un humain de jouer ». En IA contre
IA, ça n'existe pas : le moteur retire un ou deux coups, puis la partie reste
**suspendue**. Le plus clair est de désactiver « Annuler » quand les deux
places sont tenues par l'IA.

## À décider ensemble — touche le protocole

### 10. Reprendre une partie IA contre IA après Stop

**Fait** avec le mode pas à pas ([STEP.md](STEP.md)) : `pause`, `resume`,
`step` et `paused` dans `state`.

Aujourd'hui, seul `new-game` relance une partie suspendue. Le `state` ne dit
même pas qu'elle l'est, donc l'interface ne peut pas l'afficher.

Proposition :
- une commande `{ type: 'resume' }`, qui lève la suspension et relance l'IA
  si elle a le trait ;
- un champ booléen `paused` dans `state`.

C'est environ 15 lignes dans `session.cpp`, une ligne dans `protocol.ts`, une
section dans PROTOCOL.md, plus un bouton « Reprendre ».

### 11. Mettre le moteur factice à jour

[tools/mock-engine.mjs](../tools/mock-engine.mjs) diverge du vrai moteur :

- son `stop` joue le coup mais **ne suspend pas** une partie IA contre IA ;
- il n'a pas de pause entre deux coups IA ;
- il accepte n'importe quelle valeur dans `limits`, y compris
  `maxCandidates` ;
- sa recherche dure toujours ~400 ms, alors que le vrai moteur va de 0 à
  900 ms.

À aligner, ou à annoncer clairement : « en cas de doute, `./Gomoku` fait
foi ».

### 12. Ce que le moteur pourrait changer pour l'interface

Pour mémoire. Ce sont des décisions côté moteur, à prendre si l'interface en
a besoin :

- **`maxCandidates`** : le lire dans `limits` (une ligne dans
  `Session::setLimits`), ou le retirer du protocole ;
- **plusieurs onglets** : le moteur ne répond qu'au client qui a envoyé la
  commande (`Server::broadcast` existe mais n'est pas utilisé). Deux onglets
  ouverts se désynchronisent sans le dire. Le protocole annonce « un seul
  client », mais un `broadcast` des `state` réglerait la question ;
- **profondeur des coups forcés** : le moteur pourrait continuer jusqu'à
  `maxDepth` même après avoir prouvé un gain, pour que le panneau affiche
  toujours 10. Ça ne coûte presque rien, mais c'est cosmétique : le point 6
  est préférable.

## Vérification finale

Sur `./Gomoku`, sans Electron puis avec :

- [ ] une partie humain contre IA complète : chronomètre, moyenne sous 0,5 s,
  panneau de recherche cohérent ;
- [ ] une partie IA contre IA lancée depuis l'interface, suivie jusqu'au bout ;
- [ ] Stop en pleine réflexion, en humain contre IA puis en IA contre IA ;
- [ ] deux onglets : le second s'affiche tout de suite, même pendant une
  partie IA contre IA ;
- [ ] fenêtre Electron fermée en pleine réflexion : pas de `Gomoku` orphelin ;
- [ ] `ui/dist` reconstruit et commité avec les sources.
