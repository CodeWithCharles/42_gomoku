const { app, BrowserWindow, dialog } = require('electron');
const { spawn } = require('node:child_process');
const readline = require('node:readline');
const path = require('node:path');

let engine = null;

// Donne le chemin du moteur. GOMOKU_ENGINE permet de pointer sur le moteur
// factice tant que le binaire C++ n'existe pas.
function enginePath() {
  if (process.env.GOMOKU_ENGINE) {
    return path.resolve(__dirname, process.env.GOMOKU_ENGINE);
  }
  return app.isPackaged
    ? path.join(process.resourcesPath, 'Gomoku')
    : path.join(__dirname, '..', 'Gomoku');
}

// Canal 1 : lance le moteur et resout sur le port qu'il annonce sur stdout.
function startEngine() {
  return new Promise((resolve, reject) => {
    const child = spawn(
      enginePath(),
      ['--no-browser', '--port', '8642', '--parent-watchdog'],
      { stdio: ['pipe', 'pipe', 'pipe'] },
    );

    engine = child;

    const timeout = setTimeout(
      () => reject(new Error("le moteur n'a pas annonce son port en 10 s")),
      10_000,
    );

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

    child.on('error', (err) => {
      clearTimeout(timeout);
      reject(err);
    });

    child.on('exit', (code, signal) => {
      engine = null;
      clearTimeout(timeout);
      for (const w of BrowserWindow.getAllWindows()) {
        w.webContents.send('engine:died', { code, signal });
      }
    });
  });
}

// Arrete le moteur sans laisser d'orphelin : fermeture du pipe, puis SIGTERM,
// puis SIGKILL en dernier recours.
function stopEngine() {
  if (!engine) return;
  const child = engine;
  engine = null;
  child.stdin.end();
  child.kill('SIGTERM');
  setTimeout(() => {
    try {
      child.kill('SIGKILL');
    } catch {}
  }, 2000).unref();
}

app.whenReady().then(async () => {
  try {
    const port = await startEngine();
    const win = new BrowserWindow({
      width: 1200,
      height: 820,
      backgroundColor: '#15181d',
      webPreferences: {
        preload: path.join(__dirname, 'preload.js'),
        contextIsolation: true,
        nodeIntegration: false,
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

app.on('before-quit', stopEngine);
app.on('window-all-closed', () => app.quit());
process.on('exit', stopEngine);
