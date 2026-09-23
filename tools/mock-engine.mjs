#!/usr/bin/env node
// Moteur factice : imite le contrat du binaire Gomoku tant que le C++ n'existe
// pas. Annonce son port sur stdout, sert l'UI en HTTP, parle le protocole de
// docs/PROTOCOL.md sur /ws. Aucune regle du jeu ici : voir src/core/rules.cpp.

import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const SIZE = 19;
const CELLS = SIZE * SIZE;
const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const PING_INTERVAL_MS = 15_000;
const CLIENT_TIMEOUT_MS = 45_000;

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
};

// Lit les options de ligne de commande du moteur reel : --port, --no-browser,
// --ui <chemin>, --parent-watchdog, plus --fault <code> propre au bouchon.
function parseArgs(argv) {
  const opts = {
    port: 8642,
    noBrowser: false,
    ui: 'ui/dist',
    parentWatchdog: false,
    fault: null,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    if (a === '--port') opts.port = Number(argv[++i]);
    else if (a === '--no-browser') opts.noBrowser = true;
    else if (a === '--ui') opts.ui = argv[++i];
    else if (a === '--parent-watchdog') opts.parentWatchdog = true;
    else if (a === '--fault') opts.fault = argv[++i];
  }
  return opts;
}

// Convertit un index lineaire en coordonnees, selon idx = y * 19 + x.
function toXY(idx) {
  return { x: idx % SIZE, y: Math.floor(idx / SIZE) };
}

// Cherche un alignement de cinq pierres passant par idx, pour renseigner status.
function hasFive(board, idx) {
  const me = board[idx];
  if (me === 0) return false;
  const { x, y } = toXY(idx);
  const dirs = [[1, 0], [0, 1], [1, 1], [1, -1]];
  for (const [dx, dy] of dirs) {
    let run = 1;
    for (const sign of [1, -1]) {
      let cx = x + dx * sign;
      let cy = y + dy * sign;
      while (cx >= 0 && cx < SIZE && cy >= 0 && cy < SIZE && board[cy * SIZE + cx] === me) {
        run += 1;
        cx += dx * sign;
        cy += dy * sign;
      }
    }
    if (run >= 5) return true;
  }
  return false;
}

// Cree une partie vierge : plateau vide, noir au trait, reglages par defaut.
function newGame(config, players) {
  return {
    board: new Array(CELLS).fill(0),
    toMove: 'black',
    status: 'ongoing',
    winReason: '',
    pairs: { black: 0, white: 0 },
    players: players ?? { black: 'human', white: 'ai' },
    config: config ?? {
      captures: true,
      doubleThree: true,
      endgameCapture: true,
      opening: 'standard',
    },
    limits: { maxDepth: 10, budgetMs: 450, maxCandidates: 20 },
    history: [],
    lastStats: {
      depth: 0, score: 0, best: -1, nodes: 0, leaves: 0,
      cutoffs: 0, ttHits: 0, elapsedMs: 0, pv: [], rootScores: [],
    },
  };
}

// Serialise l'etat complet en evenement `state` (361 entiers, pas de diff).
function stateEvent(game, event) {
  return {
    type: 'state',
    event,
    board: game.board,
    size: SIZE,
    toMove: game.toMove,
    status: game.status,
    winReason: game.winReason,
    pairs: game.pairs,
    players: game.players,
    config: game.config,
    limits: game.limits,
    history: game.history,
    lastStats: game.lastStats,
  };
}

// Calcule la reponse du handshake RFC 6455 a partir de Sec-WebSocket-Key.
function acceptKey(key) {
  return createHash('sha1').update(key + GUID).digest('base64');
}

// Encode une trame serveur -> client : jamais masquee, jamais fragmentee.
function encodeFrame(opcode, payload) {
  const len = payload.length;
  let header;
  if (len < 126) {
    header = Buffer.alloc(2);
    header[1] = len;
  } else if (len < 65_536) {
    header = Buffer.alloc(4);
    header[1] = 126;
    header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(len), 2);
  }
  header[0] = 0x80 | opcode;
  return Buffer.concat([header, payload]);
}

// Extrait la premiere trame complete du tampon, en demasquant la charge utile.
// Rend null tant que la trame n'est pas entiere.
function decodeFrame(buf) {
  if (buf.length < 2) return null;
  const fin = (buf[0] & 0x80) !== 0;
  const opcode = buf[0] & 0x0f;
  const masked = (buf[1] & 0x80) !== 0;
  let len = buf[1] & 0x7f;
  let offset = 2;
  if (len === 126) {
    if (buf.length < 4) return null;
    len = buf.readUInt16BE(2);
    offset = 4;
  } else if (len === 127) {
    if (buf.length < 10) return null;
    len = Number(buf.readBigUInt64BE(2));
    offset = 10;
  }
  let mask = null;
  if (masked) {
    if (buf.length < offset + 4) return null;
    mask = buf.subarray(offset, offset + 4);
    offset += 4;
  }
  if (buf.length < offset + len) return null;
  const payload = Buffer.from(buf.subarray(offset, offset + len));
  if (mask) {
    for (let i = 0; i < payload.length; i += 1) payload[i] ^= mask[i & 3];
  }
  return { fin, opcode, payload, rest: buf.subarray(offset + len) };
}

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

const opts = parseArgs(process.argv.slice(2));
// Le defaut est ancre sur le depot et non sur le cwd : Electron lance le moteur
// depuis app/, ou ui/dist n'existe pas.
const uiRoot = path.resolve(REPO_ROOT, opts.ui);
let game = newGame();
let client = null;
let search = null;

// Envoie un objet JSON au client courant sous forme de trame texte.
function push(msg) {
  if (!client || client.destroyed) return;
  client.write(encodeFrame(0x1, Buffer.from(JSON.stringify(msg))));
}

// Emet un evenement `error` sans fermer la connexion.
function pushError(code, message) {
  push({ type: 'error', code, message });
}

// Liste les intersections libres, support de la recherche factice.
function freeCells() {
  const out = [];
  for (let i = 0; i < CELLS; i += 1) if (game.board[i] === 0) out.push(i);
  return out;
}

// Interrompt la boucle de progression en cours, s'il y en a une.
function stopSearch() {
  if (!search) return null;
  clearTimeout(search.timer);
  const { best, onDone } = search;
  search = null;
  return { best, onDone };
}

// Deroule une recherche factice : une dizaine de `progress` sur ~400 ms, avec
// profondeur et noeuds croissants, puis appelle onDone avec le meilleur coup.
function runSearch(onDone) {
  stopSearch();
  const candidates = freeCells();
  if (candidates.length === 0) {
    onDone(null);
    return;
  }
  const pool = candidates.slice().sort(() => Math.random() - 0.5).slice(0, 20);
  const started = Date.now();
  const total = 10;
  let depth = 1;
  let nodes = 0;

  const tick = () => {
    nodes += Math.floor(20_000 + Math.random() * 60_000) * depth;
    const rootScores = pool
      .map((idx) => ({ idx, score: Math.round((Math.random() - 0.3) * 2000) }))
      .sort((a, b) => b.score - a.score);
    search.best = rootScores[0].idx;
    push({
      type: 'progress',
      depth,
      score: rootScores[0].score,
      best: search.best,
      nodes,
      leaves: Math.floor(nodes * 0.72),
      cutoffs: Math.floor(nodes * 0.14),
      ttHits: Math.floor(nodes * 0.03),
      elapsedMs: Date.now() - started,
      pv: rootScores.slice(0, 4).map((r) => r.idx),
      rootScores: rootScores.slice(0, 8),
    });
    push(stats);
    const { type, ...rest } = stats;
    game.lastStats = rest;
    if (depth >= total) {
      const best = search.best;
      search = null;
      onDone(best);
      return;
    }
    depth += 1;
    search.timer = setTimeout(tick, 40);
  };

  search = { best: pool[0], timer: setTimeout(tick, 40), onDone };
}

// Pose une pierre pour la couleur au trait et met a jour le statut.
function place(idx, color) {
  game.board[idx] = color === 'black' ? 1 : 2;
  game.history.push({ idx, player: color, captured: [] });
  if (hasFive(game.board, idx)) {
    game.status = color === 'black' ? 'black_wins' : 'white_wins';
    game.winReason = 'five_in_a_row';
  } else if (freeCells().length === 0) {
    game.status = 'draw';
  }
  game.toMove = color === 'black' ? 'white' : 'black';
}

// Fait jouer l'IA factice apres une recherche, si c'est bien a elle.
function maybeAiMove() {
  if (game.status !== 'ongoing') return;
  if (game.players[game.toMove] !== 'ai') return;
  const color = game.toMove;
  runSearch((best) => {
    if (best === null || game.board[best] !== 0) {
      push(stateEvent(game, 'move'));
      return;
    }
    place(best, color);
    push(stateEvent(game, 'ai_move'));
    maybeAiMove();
  });
}

// Traite une commande client et repond en poussant un `state` ou un `error`.
function handleCommand(raw) {
  let cmd;
  try {
    cmd = JSON.parse(raw);
  } catch {
    pushError('unknown', 'JSON invalide');
    return;
  }
  if (opts.fault) {
    pushError(opts.fault, `faute simulee : ${opts.fault}`);
    return;
  }
  switch (cmd?.type) {
    case 'new-game':
      stopSearch();
      game = newGame(cmd.config, cmd.players);
      push(stateEvent(game, 'new_game'));
      maybeAiMove();
      break;

    case 'play': {
      if (game.status !== 'ongoing') {
        pushError('game_over', 'la partie est terminee');
        return;
      }
      const idx = typeof cmd.idx === 'number' ? cmd.idx : cmd.y * SIZE + cmd.x;
      if (!Number.isInteger(idx) || idx < 0 || idx >= CELLS) {
        pushError('out_of_bounds', 'intersection hors plateau');
        return;
      }
      if (game.board[idx] !== 0) {
        pushError('occupied', 'intersection deja occupee');
        return;
      }
      if (game.players[game.toMove] !== 'human') {
        pushError('not_your_turn', "ce n'est pas au joueur humain de jouer");
        return;
      }
      place(idx, game.toMove);
      push(stateEvent(game, 'move'));
      maybeAiMove();
      break;
    }

    case 'suggest':
      runSearch(() => push(stateEvent(game, 'suggestion')));
      break;

    case 'undo': {
      stopSearch();
      while (game.history.length > 0) {
        const last = game.history.pop();
        game.board[last.idx] = 0;
        game.toMove = last.player;
        game.status = 'ongoing';
        game.winReason = '';
        if (game.players[last.player] === 'human') break;
      }
      push(stateEvent(game, 'undo'));
      break;
    }

    case 'limits':
      game.limits = {
        maxDepth: cmd.maxDepth ?? game.limits.maxDepth,
        budgetMs: cmd.budgetMs ?? game.limits.budgetMs,
        maxCandidates: cmd.maxCandidates ?? game.limits.maxCandidates,
      };
      push(stateEvent(game, 'limits'));
      break;

    case 'weights':
      push(stateEvent(game, 'weights'));
      break;

    case 'stop': {
      const pending = stopSearch();
      if (pending?.onDone) pending.onDone(pending.best);
      break;
    }

    default:
      pushError('unknown', `commande inconnue : ${cmd?.type}`);
  }
}

// Sert un fichier statique depuis --ui, avec repli sur index.html.
async function serveStatic(req, res) {
  const url = new URL(req.url, 'http://localhost');
  let target = path.join(uiRoot, path.normalize(url.pathname));
  if (!target.startsWith(uiRoot)) target = uiRoot;
  try {
    const body = await readFile(target);
    res.writeHead(200, { 'content-type': MIME[path.extname(target)] ?? 'application/octet-stream' });
    res.end(body);
  } catch {
    try {
      const body = await readFile(path.join(uiRoot, 'index.html'));
      res.writeHead(200, { 'content-type': MIME['.html'] });
      res.end(body);
    } catch {
      res.writeHead(404, { 'content-type': 'text/plain' });
      res.end(`ui introuvable : ${uiRoot}\n`);
    }
  }
}

const server = createServer(serveStatic);

server.on('upgrade', (req, socket, head) => {
  if (new URL(req.url, 'http://localhost').pathname !== '/ws') {
    socket.destroy();
    return;
  }
  const key = req.headers['sec-websocket-key'];
  if (!key) {
    socket.destroy();
    return;
  }
  socket.write(
    'HTTP/1.1 101 Switching Protocols\r\n' +
      'Upgrade: websocket\r\n' +
      'Connection: Upgrade\r\n' +
      `Sec-WebSocket-Accept: ${acceptKey(key)}\r\n\r\n`,
  );

  if (client && !client.destroyed) {
    const bye = Buffer.alloc(2);
    bye.writeUInt16BE(1001, 0);
    client.write(encodeFrame(0x8, bye));
    client.destroy();
  }
  client = socket;
  socket.setNoDelay(true);

  let buf = head?.length ? Buffer.from(head) : Buffer.alloc(0);
  let lastSeen = Date.now();

  const ping = setInterval(() => {
    if (socket.destroyed) return;
    if (Date.now() - lastSeen > CLIENT_TIMEOUT_MS) {
      socket.destroy();
      return;
    }
    socket.write(encodeFrame(0x9, Buffer.alloc(0)));
  }, PING_INTERVAL_MS);

  socket.on('data', (chunk) => {
    lastSeen = Date.now();
    buf = Buffer.concat([buf, chunk]);
    for (;;) {
      const frame = decodeFrame(buf);
      if (!frame) break;
      buf = frame.rest;
      if (frame.opcode === 0x8) {
        socket.destroy();
        return;
      }
      if (frame.opcode === 0x9) {
        socket.write(encodeFrame(0xa, frame.payload));
        continue;
      }
      if (frame.opcode === 0x1) handleCommand(frame.payload.toString('utf8'));
    }
  });

  socket.on('close', () => {
    clearInterval(ping);
    if (client === socket) client = null;
  });
  socket.on('error', () => socket.destroy());

  push(stateEvent(game, 'connected'));
});

// Annonce le port sur stdout. Enregistre une seule fois : une nouvelle tentative
// de port ne doit pas ajouter un second callback, sous peine de double annonce.
server.once('listening', () => {
  process.stdout.write(`GOMOKU_READY port=${server.address().port}\n`);
  if (!opts.noBrowser) {
    console.error('[moteur] --no-browser absent : le bouchon n ouvre jamais de navigateur');
  }
});

// Ecoute sur le port demande, ou le suivant s'il est occupe.
function listen(port, attempts = 20) {
  server.once('error', (err) => {
    if (err.code === 'EADDRINUSE' && attempts > 0) {
      listen(port + 1, attempts - 1);
      return;
    }
    console.error('[moteur]', err.message);
    process.exit(1);
  });
  server.listen(port, '127.0.0.1');
}

if (opts.parentWatchdog) {
  process.stdin.on('end', () => process.exit(0));
  process.stdin.resume();
}

process.on('SIGTERM', () => process.exit(0));
process.on('SIGINT', () => process.exit(0));

listen(opts.port);
