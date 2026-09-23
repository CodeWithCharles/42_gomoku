import { useCallback, useMemo, useRef, useState, type ReactNode } from 'react';
import { CELLS, type Cell, type EngineEvent, type ErrorEvent, type StateEvent } from '../protocol';
import { useEngine } from '../useEngine';
import { onEngineDied } from '../shell';
import { GameContext, type GameValue } from './GameContext';
import { SearchContext, type SearchValue } from './SearchContext';
import { useEffect } from 'react';

type GameState = Omit<GameValue, 'lastError' | 'connection' | 'engineDied' | 'send'>;

const EMPTY_GAME: GameState = {
  board: new Array<Cell>(CELLS).fill(0),
  toMove: 'black',
  status: 'ongoing',
  winReason: '',
  pairs: { black: 0, white: 0 },
  players: { black: 'human', white: 'ai' },
  config: { captures: true, doubleThree: true, endgameCapture: true, opening: 'standard' },
  limits: { maxDepth: 10, budgetMs: 450, maxCandidates: 20 },
  history: [],
};

const IDLE_SEARCH: SearchValue = {
  searching: false,
  depth: 0,
  score: 0,
  best: -1,
  nodes: 0,
  leaves: 0,
  cutoffs: 0,
  ttHits: 0,
  elapsedMs: 0,
  pv: [],
  rootScores: [],
  startedAt: null,
  lastMoveMs: 0,
};

// Extrait la partie « etat de partie » d'un evenement state.
function toGameState(msg: StateEvent): GameState {
  return {
    board: msg.board,
    toMove: msg.toMove,
    status: msg.status,
    winReason: msg.winReason,
    pairs: msg.pairs,
    players: msg.players,
    config: msg.config,
    limits: msg.limits,
    history: msg.history,
  };
}

// Detient la socket unique et alimente deux contextes separes : le plateau ne
// doit pas se repeindre a chaque tick de progression de la recherche.
export function EngineProvider({ children }: { children: ReactNode }) {
  const [game, setGame] = useState<GameState>(EMPTY_GAME);
  const [search, setSearch] = useState<SearchValue>(IDLE_SEARCH);
  const [lastError, setLastError] = useState<ErrorEvent | null>(null);
  const [engineDied, setEngineDied] = useState(false);
  const searchingRef = useRef(false);

  const handleEvent = useCallback((event: EngineEvent) => {
    if (event.type === 'state') {
      searchingRef.current = false;
      setGame(toGameState(event));
      setLastError(null);
      setSearch((prev) => ({
        ...prev,
        searching: false,
        lastMoveMs: prev.startedAt === null ? prev.lastMoveMs : Date.now() - prev.startedAt,
        startedAt: null,
      }));
      return;
    }
    if (event.type === 'progress') {
      const started = searchingRef.current;
      searchingRef.current = true;
      setSearch((prev) => ({
        searching: true,
        depth: event.depth,
        score: event.score,
        best: event.best,
        nodes: event.nodes,
        leaves: event.leaves,
        cutoffs: event.cutoffs,
        ttHits: event.ttHits,
        elapsedMs: event.elapsedMs,
        pv: event.pv,
        rootScores: event.rootScores,
        startedAt: started && prev.startedAt !== null ? prev.startedAt : Date.now() - event.elapsedMs,
        lastMoveMs: prev.lastMoveMs,
      }));
      return;
    }
    setLastError(event);
  }, []);

  const { connection, send } = useEngine(handleEvent);

  useEffect(() => {
    onEngineDied(() => setEngineDied(true));
  }, []);

  const gameValue = useMemo<GameValue>(
    () => ({ ...game, lastError, connection, engineDied, send }),
    [game, lastError, connection, engineDied, send],
  );

  return (
    <GameContext value={gameValue}>
      <SearchContext value={search}>{children}</SearchContext>
    </GameContext>
  );
}
