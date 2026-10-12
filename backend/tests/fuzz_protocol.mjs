// Fuzz du protocole WebSocket (J6 : « aucun crash, jamais »).
//
// Lance le binaire, l'attaque par le protocole (JSON invalide, types
// inattendus, indices hors bornes, trames brutes fragmentées ou binaires,
// rafales de commandes, déconnexions en pleine recherche), vérifie après
// chaque scénario que le moteur répond encore, puis l'arrête par SIGTERM et
// exige une sortie propre. Sous `make debug`, toute alerte ASan ou UBSan sur
// stderr fait échouer le test.
//
//   node tests/fuzz_protocol.mjs ../Gomoku

import { spawn } from 'node:child_process';
import { createConnection } from 'node:net';
import { randomBytes } from 'node:crypto';

const binary = process.argv[2];
if (!binary) {
  console.error('usage: node fuzz_protocol.mjs <path to Gomoku>');
  process.exit(2);
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
let port = 0;
let stderr = '';
let failures = 0;

// Démarre le moteur et attend sa ligne GOMOKU_READY.
function startEngine() {
  const firstPort = 20000 + Math.floor(Math.random() * 20000);
  const child = spawn(binary, ['--no-browser', '--port', String(firstPort)], {
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  child.stderr.on('data', (chunk) => {
    stderr += chunk.toString();
  });
  return new Promise((resolve, reject) => {
    let out = '';
    child.stdout.on('data', (chunk) => {
      out += chunk.toString();
      const found = out.match(/GOMOKU_READY port=(\d+)/);
      if (found) {
        port = Number(found[1]);
        resolve(child);
      }
    });
    child.on('exit', (code) => reject(new Error(`engine exited early (${code})`)));
    setTimeout(() => reject(new Error('engine never became ready')), 10000);
  });
}

// Ouvre une socket WebSocket et collecte les messages reçus.
function openSocket() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://127.0.0.1:${port}/ws`);
    const messages = [];
    ws.onmessage = (event) => {
      try {
        messages.push(JSON.parse(event.data));
      } catch {
        messages.push({ type: 'unparsable', raw: String(event.data) });
      }
    };
    ws.onopen = () => resolve({ ws, messages });
    ws.onerror = () => reject(new Error('websocket error'));
    setTimeout(() => reject(new Error('websocket open timeout')), 5000);
  });
}

// Attend qu'un message vérifiant `predicate` arrive dans `messages`.
async function waitFor(messages, predicate, timeoutMs = 5000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (messages.some(predicate)) {
      return true;
    }
    await sleep(10);
  }
  return false;
}

// Le moteur est vivant s'il sert un `state` à une connexion neuve.
async function engineAnswers(child) {
  if (child.exitCode !== null || child.signalCode !== null) {
    return false;
  }
  try {
    const { ws, messages } = await openSocket();
    const ok = await waitFor(messages, (m) => m.type === 'state', 5000);
    ws.close();
    return ok;
  } catch {
    return false;
  }
}

// Ouvre une connexion TCP brute et fait la poignée de main WebSocket à la main.
function rawSocket() {
  return new Promise((resolve, reject) => {
    const socket = createConnection({ host: '127.0.0.1', port }, () => {
      const key = randomBytes(16).toString('base64');
      socket.write(
        'GET /ws HTTP/1.1\r\n' +
          `Host: 127.0.0.1:${port}\r\n` +
          'Upgrade: websocket\r\nConnection: Upgrade\r\n' +
          `Sec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`,
      );
    });
    let header = '';
    const onData = (chunk) => {
      header += chunk.toString('latin1');
      if (header.includes('\r\n\r\n')) {
        socket.off('data', onData);
        socket.on('data', () => {});
        resolve(socket);
      }
    };
    socket.on('data', onData);
    socket.on('error', () => {});
    socket.setTimeout(5000, () => reject(new Error('raw handshake timeout')));
  });
}

// Encode une trame client (masquée, comme l'impose la RFC 6455).
function frame(opcode, payload, fin = true) {
  const data = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  const mask = randomBytes(4);
  let head;
  if (data.length < 126) {
    head = Buffer.from([(fin ? 0x80 : 0) | opcode, 0x80 | data.length]);
  } else if (data.length < 65536) {
    head = Buffer.alloc(4);
    head[0] = (fin ? 0x80 : 0) | opcode;
    head[1] = 0x80 | 126;
    head.writeUInt16BE(data.length, 2);
  } else {
    head = Buffer.alloc(10);
    head[0] = (fin ? 0x80 : 0) | opcode;
    head[1] = 0x80 | 127;
    head.writeBigUInt64BE(BigInt(data.length), 2);
  }
  const masked = Buffer.alloc(data.length);
  for (let i = 0; i < data.length; i += 1) {
    masked[i] = data[i] ^ mask[i & 3];
  }
  return Buffer.concat([head, mask, masked]);
}

const malformed = [
  '',
  '{',
  'null',
  '[]',
  '42',
  '"play"',
  'true',
  '{"type":5}',
  '{"type":null}',
  '{"type":["play"]}',
  '{"type":{"x":1}}',
  '{"type":"nope"}',
  '{"type":"play"}',
  '{"type":"play","idx":-1}',
  '{"type":"play","idx":361}',
  '{"type":"play","idx":1e30}',
  '{"type":"play","idx":-1e30}',
  '{"type":"play","idx":3.7}',
  '{"type":"play","idx":"180"}',
  '{"type":"play","idx":null}',
  '{"type":"play","idx":[180]}',
  '{"type":"play","x":-1,"y":5}',
  '{"type":"play","x":19,"y":19}',
  '{"type":"play","x":"a","y":{}}',
  '{"type":"play","x":1e300,"y":1e300}',
  '{"type":"new-game","config":"yes"}',
  '{"type":"new-game","config":{"captures":"no","doubleThree":3,"opening":42}}',
  '{"type":"new-game","players":{"black":1,"white":null}}',
  '{"type":"new-game","players":[]}',
  '{"type":"limits","maxDepth":-5,"budgetMs":-100,"maxCandidates":-3}',
  '{"type":"limits","maxDepth":"deep","budgetMs":"fast"}',
  '{"type":"limits","maxDepth":1e30,"budgetMs":1e30,"maxCandidates":1e30}',
  '{"type":"limits","maxDepth":10,"budgetMs":450,"maxCandidates":20}',
  '{"type":"weights","weights":"heavy"}',
  '{"type":"weights","weights":{"five":"x","openFour":1e30,"openThree":-1e30}}',
  '{"type":"weights","weights":{"five":2147483647,"openFour":2147483647,"openThree":2147483647,"openTwo":2147483647,"capturedPair":2147483647,"captureThreat":2147483647}}',
  '{"type":"weights","weights":{"five":100000,"openFour":20000,"simpleFour":4000,"openThree":2000,"brokenThree":1200,"simpleThree":300,"openTwo":100,"capturedPair":1500,"captureThreat":400}}',
  '{"type":"undo","extra":[1,2,3]}',
  '{"type":"suggest","type":"stop"}',
  '\u0000\u0001\u0002',
  '{"type":"play","idx":180}'.repeat(1000),
];

// Chaque scénario reçoit le processus et lève une exception en cas d'échec.
const scenarios = [
  [
    'malformed and mistyped commands',
    async () => {
      const { ws, messages } = await openSocket();
      for (const text of malformed) {
        ws.send(text);
      }
      ws.send('{"type":"limits","maxDepth":10,"budgetMs":450,"maxCandidates":20}');
      const ok = await waitFor(messages, (m) => m.type === 'state' && m.event === 'limits', 15000);
      ws.close();
      if (!ok) {
        throw new Error('no answer after the malformed batch');
      }
    },
  ],
  [
    'deeply nested and very large JSON',
    async () => {
      const { ws } = await openSocket();
      ws.send('['.repeat(100000) + ']'.repeat(100000));
      ws.send('{"a":'.repeat(50000) + '1' + '}'.repeat(50000));
      ws.send(JSON.stringify({ type: 'play', idx: 180, pad: 'x'.repeat(2 * 1024 * 1024) }));
      await sleep(500);
      ws.close();
    },
  ],
  [
    'raw frames: fragments, binary, ping, bad opcodes',
    async () => {
      const socket = await rawSocket();
      socket.write(frame(0x1, '{"type":', false));
      socket.write(frame(0x0, '"suggest"', false));
      socket.write(frame(0x0, '}', true));
      socket.write(frame(0x2, randomBytes(64)));
      socket.write(frame(0x9, 'ping'));
      socket.write(frame(0xa, 'pong'));
      socket.write(frame(0x3, 'reserved opcode'));
      socket.write(frame(0x0, 'orphan continuation'));
      socket.write(frame(0x1, randomBytes(200)));
      socket.write(randomBytes(512));
      await sleep(300);
      socket.destroy();
    },
  ],
  [
    'endless fragmented message',
    async () => {
      const socket = await rawSocket();
      socket.write(frame(0x1, '{"type":"', false));
      const chunk = frame(0x0, 'x'.repeat(60000), false);
      for (let i = 0; i < 300 && !socket.destroyed; i += 1) {
        socket.write(chunk);
        if (i % 50 === 0) {
          await sleep(10);
        }
      }
      await sleep(300);
      socket.destroy();
    },
  ],
  [
    'command storm during an AI versus AI game',
    async () => {
      const { ws } = await openSocket();
      ws.send('{"type":"new-game","players":{"black":"ai","white":"ai"}}');
      const kinds = ['suggest', 'stop', 'undo', 'play', 'limits', 'new-game'];
      for (let i = 0; i < 400; i += 1) {
        const type = kinds[i % kinds.length];
        const command = { type };
        if (type === 'play') {
          command.idx = Math.floor(Math.random() * 400) - 20;
        }
        if (type === 'new-game') {
          command.players = { black: i % 2 ? 'ai' : 'human', white: 'ai' };
        }
        ws.send(JSON.stringify(command));
        if (i % 40 === 0) {
          await sleep(20);
        }
      }
      ws.send('{"type":"stop"}');
      await sleep(500);
      ws.close();
    },
  ],
  [
    'disconnect in the middle of a search',
    async () => {
      for (let i = 0; i < 5; i += 1) {
        const { ws } = await openSocket();
        ws.send('{"type":"new-game","players":{"black":"human","white":"ai"}}');
        ws.send('{"type":"play","idx":180}');
        await sleep(50 + i * 40);
        ws.close();
      }
      const socket = await rawSocket();
      socket.write(frame(0x1, '{"type":"suggest"}'));
      await sleep(30);
      socket.destroy();
      await sleep(600);
    },
  ],
  [
    'several clients at once',
    async () => {
      const clients = [];
      for (let i = 0; i < 8; i += 1) {
        clients.push(await openSocket());
      }
      for (const { ws } of clients) {
        ws.send('{"type":"suggest"}');
      }
      await sleep(300);
      for (const { ws } of clients) {
        ws.close();
      }
    },
  ],
];

const child = await startEngine();
console.log(`engine ready on port ${port}`);
for (const [name, run] of scenarios) {
  let error = null;
  try {
    await run(child);
  } catch (caught) {
    error = caught;
  }
  const alive = await engineAnswers(child);
  if (error || !alive) {
    failures += 1;
    console.log(`  FAIL  ${name}${error ? `: ${error.message}` : ''}${alive ? '' : ' (engine down)'}`);
  } else {
    console.log(`  ok    ${name}`);
  }
}

child.kill('SIGTERM');
const started = Date.now();
while (child.exitCode === null && child.signalCode === null && Date.now() - started < 5000) {
  await sleep(20);
}
if (child.exitCode !== 0) {
  failures += 1;
  console.log(`  FAIL  clean shutdown (exit ${child.exitCode}, signal ${child.signalCode})`);
} else {
  console.log(`  ok    clean shutdown in ${Date.now() - started} ms`);
}
if (/AddressSanitizer|runtime error|LeakSanitizer|ThreadSanitizer/.test(stderr)) {
  failures += 1;
  console.log('  FAIL  sanitizer report on stderr:');
  console.log(stderr.split('\n').slice(0, 30).join('\n'));
}

console.log(failures === 0 ? '\nfuzz: all scenarios passed' : `\nfuzz: ${failures} failure(s)`);
process.exit(failures === 0 ? 0 : 1);
