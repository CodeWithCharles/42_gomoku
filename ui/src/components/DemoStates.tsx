import { useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { CELLS, SIZE, xyToIdx, type Cell, type Command, type HistoryEntry, type Limits, type Players } from '../protocol';
import { GameContext, useGame, type GameValue } from '../engine/GameContext';
import { SearchContext, useSearch, type SearchValue } from '../engine/SearchContext';
import { useView } from '../view/ViewContext';
import { inElectron, onMaximizedChange, toggleMaximizeWindow } from '../shell';
import './DemoStates.css';

const DEMO_ENABLED = new URLSearchParams(window.location.search).has('demo');

const SLOW_LIMITS = { maxDepth: 64, budgetMs: 5000 };

const COLUMNS = 'ABCDEFGHJKLMNOPQRST';

const MID = ['K10', 'L11', 'L10', 'M12', 'N13', 'K11', 'M10', 'N10', 'J10', 'H10', 'K9', 'K8', 'L9', 'M8', 'K10', 'L12', 'N11', 'O12', 'L8', 'J11', 'M11', 'N12'];
const FIVE = MID.concat(['H16', 'C4', 'J16', 'E4', 'K16', 'G4', 'L16', 'O4', 'M16']);
const CAPS = ['A1', 'B1', 'A3', 'C1', 'D1', 'B3', 'A5', 'C3', 'D3', 'B5', 'A7', 'C5', 'D5', 'B7', 'A9', 'C7', 'D7', 'B9', 'Q16', 'C9', 'D9'];
const DOUBLE_THREE = MID.concat(['C15', 'R4', 'D15', 'R6', 'E17', 'R8', 'E16', 'R10']);

const HUMANS: Players = { black: 'human', white: 'human' };
const VS_AI: Players = { black: 'human', white: 'ai' };
const AIS: Players = { black: 'ai', white: 'ai' };

interface Overlay {
  game?: Partial<GameValue>;
  search?: Partial<SearchValue>;
}

interface Scenario {
  id: string;
  label: string;
  players?: Players;
  moves?: string[];
  after?: Command[];
  overlay?: Overlay;
  numbers?: boolean;
  slowAi?: boolean;
}

// Convertit une coordonnee de diagramme ("K10") en index lineaire.
function idx(label: string): number {
  return xyToIdx(COLUMNS.indexOf(label[0]), SIZE - Number(label.slice(1)));
}

// Plateau plein sans alignement ni capture, que le moteur ne peut pas atteindre
// en partie : seul cet etat est simule de bout en bout.
function fullBoard(): Pick<GameValue, 'board' | 'history'> {
  const board = new Array<Cell>(CELLS).fill(0);
  const blacks: number[] = [];
  const whites: number[] = [];
  for (let i = 0; i < CELLS; i++) {
    const x = i % SIZE;
    const y = Math.floor(i / SIZE);
    if (((x >> 1) + y) % 2 === 0) blacks.push(i);
    else whites.push(i);
  }
  const history: HistoryEntry[] = [];
  blacks.forEach((i, k) => {
    board[i] = 1;
    history.push({ idx: i, player: 'black', captured: [] });
    if (k < whites.length) {
      board[whites[k]] = 2;
      history.push({ idx: whites[k], player: 'white', captured: [] });
    }
  });
  return { board, history };
}

const SCENARIOS: Scenario[] = [
  { id: 'connecting', label: 'Connexion en cours', overlay: { game: { connection: 'connecting' } } },
  { id: 'closed', label: 'Déconnecté, nouvelle tentative', players: HUMANS, moves: MID, overlay: { game: { connection: 'closed' } } },
  { id: 'died', label: 'Moteur arrêté (Electron)', players: HUMANS, moves: MID, overlay: { game: { connection: 'closed', engineDied: true } } },
  { id: 'empty', label: 'Plateau vide, humain au trait', players: VS_AI, moves: [] },
  { id: 'mid', label: 'Milieu de partie, aperçu au survol', players: HUMANS, moves: MID },
  { id: 'thinking', label: "L'IA réfléchit", players: VS_AI, moves: ['K10'], slowAi: true },
  { id: 'hint', label: 'Suggestion affichée', players: HUMANS, moves: MID, after: [{ type: 'suggest' }] },
  { id: 'double_three', label: 'Erreur double_three', players: HUMANS, moves: DOUBLE_THREE, after: [{ type: 'play', idx: idx('E15') }] },
  {
    id: 'overbudget',
    label: 'Moyenne au-dessus du budget',
    players: HUMANS,
    moves: MID,
    overlay: { search: { aiMoves: 11, aiTotalMs: 5962, lastMoveMs: 731 } },
  },
  { id: 'numbers', label: 'Numéros de coups, avec capture', players: HUMANS, moves: MID, numbers: true },
  { id: 'five', label: 'Victoire par cinq alignés', players: HUMANS, moves: FIVE },
  { id: 'captures', label: 'Victoire par captures', players: HUMANS, moves: CAPS },
  {
    id: 'draw',
    label: 'Partie nulle, plateau plein',
    overlay: { game: { ...fullBoard(), status: 'draw', winReason: 'board_full', players: AIS, pairs: { black: 0, white: 0 }, suggestion: null } },
  },
  { id: 'aivsai', label: 'IA contre IA en cours', players: AIS, moves: [] },
];

interface Pending {
  before: HistoryEntry[];
  moves: number[];
  overlay: Overlay | null;
}

// Indique si l'historique publie par le moteur est exactement la sequence rejouee.
function replayed(history: HistoryEntry[], moves: number[]): boolean {
  return history.length === moves.length && history.every((entry, k) => entry.idx === moves[k]);
}

// Fausse coque Electron installee dans un navigateur pour afficher la barre de titre.
function fakeShell(): NonNullable<Window['gomokuShell']> {
  let maximized = false;
  const listeners = new Set<(maximized: boolean) => void>();
  return {
    onEngineDied: () => {},
    minimize: () => {},
    close: () => {},
    toggleMaximize: () => {
      maximized = !maximized;
      listeners.forEach((cb) => cb(maximized));
    },
    onMaximizedChange: (cb) => {
      listeners.add(cb);
      return () => {
        listeners.delete(cb);
      };
    },
  };
}

// Selecteur d'etats de demonstration, actif seulement avec ?demo dans l'URL.
// Retirer ce composant et son montage dans App suffit a le supprimer.
export function DemoStates({ children }: { children: ReactNode }) {
  if (!DEMO_ENABLED) return children;
  return <DemoPanel>{children}</DemoPanel>;
}

// Rejoue chaque etat sur le vrai moteur (nouvelle partie puis coups), et ne
// force par-dessus que ce que le moteur ne sait pas produire sur commande :
// connexion, mort du moteur, moyenne hors budget, plateau plein.
function DemoPanel({ children }: { children: ReactNode }) {
  const game = useGame();
  const search = useSearch();
  const { showMoveNumbers, toggleMoveNumbers, setSeats } = useView();
  const [open, setOpen] = useState(true);
  const [current, setCurrent] = useState('');
  const [overlay, setOverlay] = useState<{ values: Overlay; anchor: HistoryEntry[] } | null>(null);
  const [realElectron] = useState(inElectron);
  const [shell, setShell] = useState<'electron' | 'browser'>(realElectron ? 'electron' : 'browser');
  const [shellKey, setShellKey] = useState(0);
  const [maximized, setMaximized] = useState(false);
  const pendingRef = useRef<Pending | null>(null);
  const restoreRef = useRef<Limits | null>(null);

  useEffect(() => onMaximizedChange(setMaximized), [shellKey]);

  useEffect(() => {
    if (overlay && game.history !== overlay.anchor) setOverlay(null);
    const pending = pendingRef.current;
    if (pending && game.history !== pending.before && replayed(game.history, pending.moves)) {
      pendingRef.current = null;
      if (pending.overlay) setOverlay({ values: pending.overlay, anchor: game.history });
    }
    const restore = restoreRef.current;
    if (restore && !pendingRef.current && game.history.length >= 2) {
      restoreRef.current = null;
      game.send({ type: 'limits', ...restore });
    }
  }, [game.history]);

  const pick = (id: string) => {
    const scenario = SCENARIOS.find((s) => s.id === id);
    if (!scenario) return;
    setCurrent(id);
    setOverlay(null);
    pendingRef.current = null;
    if (restoreRef.current) {
      game.send({ type: 'limits', ...restoreRef.current });
      restoreRef.current = null;
    }
    if (showMoveNumbers !== Boolean(scenario.numbers)) toggleMoveNumbers();
    if (!scenario.players) {
      if (scenario.overlay) setOverlay({ values: scenario.overlay, anchor: game.history });
      return;
    }
    setSeats(scenario.players);
    if (scenario.slowAi) {
      restoreRef.current = game.limits;
      game.send({ type: 'limits', ...game.limits, ...SLOW_LIMITS });
    }
    const moves = (scenario.moves ?? []).map(idx);
    pendingRef.current = { before: game.history, moves, overlay: scenario.overlay ?? null };
    game.send({ type: 'new-game', players: scenario.players });
    moves.forEach((move) => game.send({ type: 'play', idx: move }));
    scenario.after?.forEach((cmd) => game.send(cmd));
  };

  const step = (delta: number) => {
    const at = SCENARIOS.findIndex((s) => s.id === current);
    const next = (at + delta + SCENARIOS.length) % SCENARIOS.length;
    pick(SCENARIOS[at < 0 && delta < 0 ? SCENARIOS.length - 1 : next].id);
  };

  const showShell = (mode: 'electron' | 'browser') => {
    setShell(mode);
    if (realElectron) return;
    if (mode === 'electron') window.gomokuShell = fakeShell();
    else delete window.gomokuShell;
    setMaximized(false);
    setShellKey((k) => k + 1);
  };

  const gameValue = useMemo(() => (overlay?.values.game ? { ...game, ...overlay.values.game } : game), [game, overlay]);
  const searchValue = useMemo(
    () => (overlay?.values.search ? { ...search, ...overlay.values.search } : search),
    [search, overlay],
  );

  return (
    <GameContext value={gameValue}>
      <SearchContext value={searchValue}>
        <div key={shellKey} className={realElectron && shell === 'browser' ? 'demo-root demo-browser' : 'demo-root'}>
          {children}
        </div>
        <aside className="demo" aria-label="Sélecteur d'états, démonstration">
          <button className="demo-toggle" type="button" aria-expanded={open} onClick={() => setOpen(!open)}>
            <span>Démo : sélecteur d'états</span>
            <svg viewBox="0 0 10 10" className={open ? 'demo-chev demo-chev-open' : 'demo-chev'} aria-hidden="true">
              <path d="M1 3.5l4 3 4-3" />
            </svg>
          </button>
          {open && (
            <div className="demo-body">
              <span className="demo-label">État rejoué sur le moteur</span>
              <div className="demo-pick">
                <button className="demo-step" type="button" aria-label="État précédent" onClick={() => step(-1)}>
                  <svg viewBox="0 0 10 10" aria-hidden="true">
                    <path d="M6.5 1l-3 4 3 4" />
                  </svg>
                </button>
                <select aria-label="État forcé" value={current} onChange={(e) => pick(e.target.value)}>
                  <option value="" disabled>
                    Choisir un état
                  </option>
                  {SCENARIOS.map((s, k) => (
                    <option key={s.id} value={s.id}>
                      {k + 1}. {s.label}
                    </option>
                  ))}
                </select>
                <button className="demo-step" type="button" aria-label="État suivant" onClick={() => step(1)}>
                  <svg viewBox="0 0 10 10" aria-hidden="true">
                    <path d="M3.5 1l3 4-3 4" />
                  </svg>
                </button>
              </div>
              <span className="demo-label">15. Affichage</span>
              <div className="segmented" role="group" aria-label="Coque">
                <button className="seg-btn" type="button" aria-pressed={shell === 'electron'} onClick={() => showShell('electron')}>
                  Electron
                </button>
                <button className="seg-btn" type="button" aria-pressed={shell === 'browser'} onClick={() => showShell('browser')}>
                  Navigateur
                </button>
              </div>
              <label className="switch">
                <input type="checkbox" checked={maximized} disabled={shell === 'browser'} onChange={toggleMaximizeWindow} />
                Fenêtre agrandie
              </label>
            </div>
          )}
        </aside>
      </SearchContext>
    </GameContext>
  );
}
