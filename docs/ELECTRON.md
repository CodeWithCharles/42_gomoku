# La coque Electron — communication avec le moteur C++

> Guide du propriétaire de la coque (module 4). Voir ADR-005 pour *pourquoi*
> Electron n'est qu'un enrobage, et `docs/PROTOCOL.md` pour le protocole de jeu.

## Le piège à éviter d'entrée

Il n'y a **pas un** canal de communication, il y en a **trois**, et la faute
classique est de tout faire passer par l'IPC d'Electron.

```
   ┌──────────────── Electron ────────────────┐
   │                                          │
   │  main process          renderer (React)  │
   │       │                       │          │
   │       │  ③ IPC                │          │
   │       │  (contextBridge)      │          │
   │       └───────────────────────┘          │
   │       │                       │          │
   └───────┼───────────────────────┼──────────┘
           │                       │
   ① spawn / stdout / signaux      │ ② WebSocket
      (node:child_process)         │    (new WebSocket('/ws'))
           │                       │
           ▼                       ▼
     ┌─────────────────────────────────┐
     │        Gomoku  (C++)            │
     │   écoute sur 127.0.0.1:PORT     │
     └─────────────────────────────────┘
```

| canal | entre | technologie | ce qui y passe |
|---|---|---|---|
| **①** | main ↔ moteur | `node:child_process` + stdout | démarrage, découverte du port, mort du process |
| **②** | renderer ↔ moteur | `WebSocket` | **tout le jeu** — coups, état, progression de la recherche |
| **③** | main ↔ renderer | `contextBridge` + `ipcRenderer` | uniquement ce que l'URL ne dit pas : « le moteur est mort » |

**Le trafic de jeu ne passe jamais par Electron.** Le renderer parle
directement au moteur, exactement comme un navigateur ordinaire. C'est
ce qui fait que `./Gomoku` + Firefox reste une démonstration valide (ADR-005) et
que l'UI est testable sans lancer Electron.

### Pourquoi le renderer charge `http://` et pas `file://`

Si la fenêtre chargeait `file://…/index.html`, son origine serait `null` :
`location.host` serait vide, et l'UI n'aurait plus aucun moyen de construire
l'URL de la socket sans coder le port en dur.

En chargeant `http://127.0.0.1:PORT/`, la page est servie par le moteur
lui-même : **même origine**, donc `new WebSocket('ws://' + location.host + '/ws')`
fonctionne sans une ligne de configuration. Le code de l'UI est rigoureusement
identique en navigateur et dans Electron.

## Canal ① — main ↔ moteur

### Découverte du port : le moteur annonce, Electron écoute

Le moteur prend le port demandé, ou le suivant s'il est occupé. Electron ne
peut donc pas le deviner. Deux approches :

| approche | verdict |
|---|---|
| Electron choisit un port libre (`net.createServer().listen(0)`) et le passe en `--port` | **à éviter** — entre la fermeture de la socket de test et le `bind()` du moteur, un autre process peut prendre le port. TOCTOU classique |
| **Le moteur imprime son port sur stdout, Electron le lit** | **recommandé** — une seule source de vérité, et le moteur gère lui-même les collisions |

Côté C++, imprimez une ligne **machine-lisible**, distincte des logs humains :

```cpp
// Juste apres un listen() reussi, AVANT d'entrer dans la boucle poll().
std::printf("GOMOKU_READY port=%u\n", server.port());
std::fflush(stdout);
```

> **`fflush` n'est pas optionnel.** Quand stdout est un pipe (et c'est le cas
> sous Electron), la libc passe en buffer par blocs : sans `fflush`, la ligne
> reste coincée dans le tampon et Electron attend indéfiniment un moteur qui
> tourne parfaitement. C'est le bug numéro un de ce montage.

### Démarrage

`spawn` et pas `exec` (on veut streamer stdout), pas `fork` (réservé aux
enfants Node).

```js
// app/main.js
const { app, BrowserWindow, dialog } = require('electron');
const { spawn } = require('node:child_process');
const readline = require('node:readline');
const path = require('node:path');

let engine = null;

function enginePath() {
  // En dev le binaire est a la racine du depot ; une fois empaquete il est
  // copie dans les ressources de l'application.
  return app.isPackaged
    ? path.join(process.resourcesPath, 'Gomoku')
    : path.join(__dirname, '..', 'Gomoku');
}

function startEngine() {
  return new Promise((resolve, reject) => {
    const child = spawn(enginePath(), [
      '--no-browser',        // surtout pas : c'est Electron qui affiche
      '--port', '8642',
      '--parent-watchdog',   // voir "Ne pas laisser d'orphelin"
    ], { stdio: ['pipe', 'pipe', 'pipe'] });

    engine = child;

    const timeout = setTimeout(
      () => reject(new Error("le moteur n'a pas annonce son port en 10 s")),
      10_000,
    );

    // Lecture ligne par ligne : un seul 'data' peut contenir plusieurs lignes,
    // ou une ligne coupee en deux. readline s'en occupe.
    readline.createInterface({ input: child.stdout }).on('line', (line) => {
      const m = /^GOMOKU_READY port=(\d+)$/.exec(line.trim());
      if (m) {
        clearTimeout(timeout);
        resolve(Number(m[1]));
      } else {
        console.log('[moteur]', line);
      }
    });

    child.stderr.on('data', (d) => console.error('[moteur]', String(d).trimEnd()));

    // ENOENT : le binaire n'existe pas (oubli de `make`).
    child.on('error', (err) => { clearTimeout(timeout); reject(err); });

    child.on('exit', (code, signal) => {
      engine = null;
      clearTimeout(timeout);
      for (const w of BrowserWindow.getAllWindows()) {
        w.webContents.send('engine:died', { code, signal });
      }
    });
  });
}
```

### Ouvrir la fenêtre — après l'annonce, jamais avant

```js
app.whenReady().then(async () => {
  try {
    const port = await startEngine();          // ← on attend GOMOKU_READY
    const win = new BrowserWindow({
      width: 1200,
      height: 820,
      backgroundColor: '#15181d',              // evite le flash blanc au demarrage
      webPreferences: {
        preload: path.join(__dirname, 'preload.js'),
        contextIsolation: true,                // le renderer est une page web,
        nodeIntegration: false,                // il n'a aucun besoin de Node
        sandbox: true,
      },
    });
    await win.loadURL(`http://127.0.0.1:${port}/`);
  } catch (err) {
    dialog.showErrorBox(
      'Gomoku',
      `Impossible de demarrer le moteur.\n\n${err.message}\n\n` +
      'Compilez-le d abord avec `make`.',
    );
    app.quit();
  }
});
```

Appeler `loadURL` avant l'annonce donne un `ERR_CONNECTION_REFUSED` ou une
fenêtre blanche : le `bind()` du moteur n'a pas encore eu lieu.

### Ne pas laisser d'orphelin

Un moteur qui survit à la fenêtre garde le port et fait échouer le lancement
suivant. Il faut le couvrir **des deux côtés**.

Côté Electron — fermeture normale :

```js
function stopEngine() {
  if (!engine) return;
  const child = engine;
  engine = null;
  child.stdin.end();                 // ferme le pipe -> reveille le watchdog C++
  child.kill('SIGTERM');
  setTimeout(() => { try { child.kill('SIGKILL'); } catch {} }, 2000).unref();
}

app.on('before-quit', stopEngine);
app.on('window-all-closed', () => app.quit());
process.on('exit', stopEngine);
```

Côté C++ — filet pour le cas où Electron est tué brutalement (`SIGKILL`,
crash) et n'exécute donc aucun `before-quit` :

```cpp
// Linux : le noyau nous envoie SIGTERM des que le parent meurt.
#include <sys/prctl.h>
::prctl(PR_SET_PDEATHSIG, SIGTERM);
// Course : si le parent est mort entre le fork et le prctl, on a ete adopte
// par init et le signal ne viendra jamais. On le detecte apres coup.
if (::getppid() == 1) return 1;
```

Et la variante portable, qui marche aussi si vous passez un jour sur macOS —
c'est le pendant du `child.stdin.end()` ci-dessus :

```cpp
// Fil dedie : quand Electron meurt, son extremite du pipe se ferme et read()
// rend 0. On arrete alors le serveur proprement.
std::thread([&server] {
    char c;
    while (::read(STDIN_FILENO, &c, 1) > 0) { /* on ignore le contenu */ }
    server.stop();
}).detach();
```

> **N'activez ce fil que si `--parent-watchdog` est passé.** Lancé depuis un
> terminal avec `stdin` redirigé depuis `/dev/null`, `read()` rend `0`
> immédiatement et le moteur s'arrêterait à la seconde où vous le démarrez.

## Canal ② — renderer ↔ moteur

Rien de spécifique à Electron : c'est exactement le protocole de
[`docs/PROTOCOL.md`](PROTOCOL.md), en **URL relatives**.

```ts
// ui/src/useEngine.ts — identique en navigateur et dans Electron.
const socket = new WebSocket(`ws://${location.host}/ws`);

socket.onmessage = (e) => {
  const msg = JSON.parse(e.data);
  if (msg.type === 'state')    applyState(msg);
  if (msg.type === 'progress') applyProgress(msg);
  if (msg.type === 'error')    showError(msg);
};

socket.send(JSON.stringify({ type: 'play', idx }));
```

`location.host` est le point clé : en `npm run dev` le proxy Vite relaie `/ws`
vers `:8642`, en production la page est servie par le moteur lui-même.
**Ne codez jamais `ws://localhost:8642` en dur dans l'UI** — le port est choisi
au démarrage, pas à l'écriture du code.

## Canal ③ — main ↔ renderer

Volontairement minuscule. Le port est déjà dans `window.location`, donc l'IPC
ne sert qu'à ce que l'URL ne peut pas exprimer : la mort du moteur.

```js
// app/preload.js
const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('gomokuShell', {
  onEngineDied: (cb) => ipcRenderer.on('engine:died', (_e, info) => cb(info)),
});
```

```ts
// ui/src/shell.ts
declare global {
  interface Window {
    gomokuShell?: { onEngineDied: (cb: (i: unknown) => void) => void };
  }
}
export const inElectron = () => typeof window.gomokuShell !== 'undefined';
```

On expose une API nommée et étroite, **jamais `ipcRenderer` en entier** :
sinon n'importe quel script de la page peut émettre n'importe quel message
vers le main process.

## Posture de sécurité

- Le moteur écoute sur **`INADDR_LOOPBACK`**, jamais `INADDR_ANY` : la partie
  n'est pas exposée au réseau de l'école.
- `contextIsolation: true`, `nodeIntegration: false`, `sandbox: true`.
- Le preload expose une surface nommée, pas `ipcRenderer`.
- *Optionnel* : tout process local peut parler au port. Si vous voulez fermer
  ça, faites imprimer un jeton par le moteur sur la ligne `GOMOKU_READY`,
  passez-le en query string au `loadURL`, et exigez-le en query string sur
  `/ws` (un `WebSocket` de navigateur ne peut pas porter d'en-tête custom).
  Vérifier l'en-tête `Origin` au handshake est l'autre garde-fou, gratuit.
  Hors périmètre du sujet — à ne faire que si tout le reste est parfait.

## Mise en place

```
app/
├── package.json
├── main.js        canal ① + fenetre
└── preload.js     canal ③
```

```json
{
  "name": "gomoku-desktop",
  "private": true,
  "version": "0.1.0",
  "main": "main.js",
  "scripts": { "start": "electron ." },
  "devDependencies": { "electron": "44.4.5" }
}
```

```make
# Le binaire d'abord : la coque ne fait que le lancer.
app: $(NAME)
	@cd app && npm install && npm start

.PHONY: all clean fclean re test bench debug ui app
```

Tant que le moteur C++ n'existe pas, `app` dépend de `ui` et la coque est lancée
sur `tools/mock-engine.mjs` via `npm run start:mock` — voir la section
« Développer sans le moteur C++ » du `README.md`. La variable d'environnement
`GOMOKU_ENGINE` surcharge `enginePath()` pour pointer sur ce moteur factice ;
les deux branches `app.isPackaged` restent intactes en dessous.

> **`app` est aussi un nom de dossier.** Sans `.PHONY`, make voit le répertoire
> `app/`, le considère à jour, et la cible ne s'exécute jamais.

L'empaquetage (`electron-builder` → AppImage/deb) n'est **pas décidé** et n'est
pas nécessaire : `make app` suffit largement pour une soutenance. Si vous le
faites un jour, il faudra copier `Gomoku` dans `extraResources` — c'est ce que
gère la branche `app.isPackaged` de `enginePath()`.

## Dépannage

| symptôme | cause |
|---|---|
| Electron attend puis affiche le timeout, alors que le moteur tourne | `fflush(stdout)` oublié après la ligne `GOMOKU_READY` |
| Fenêtre blanche / `ERR_CONNECTION_REFUSED` | `loadURL` appelé avant l'annonce du port |
| Au 2ᵉ lancement, port différent puis échec | moteur orphelin du lancement précédent — `stopEngine` non branché |
| Le moteur s'arrête aussitôt démarré | watchdog stdin actif alors que `stdin` est `/dev/null`, sans le garde `--parent-watchdog` |
| `make app` ne fait rien | cible homonyme du dossier `app/`, `.PHONY` manquant |
| `Error: spawn ENOENT` | binaire absent : `make` avant `make app` |
| La socket échoue en `400` en dev | proxy Vite sans `ws: true` — l'`Upgrade` n'est pas relayé (voir `docs/PROTOCOL.md`) |
| La socket se rouvre en boucle | le moteur ferme la connexion — il doit la garder ouverte, répondre aux pings par un pong et envoyer les siens toutes les 15 s |
| Le moteur lit du charabia dans les commandes | démasquage XOR oublié : toute trame client → serveur est masquée (RFC 6455) |
