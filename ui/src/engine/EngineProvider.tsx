import { useCallback, useMemo, useRef, useState, type ReactNode } from 'react';
import { CELLS, type Cell, type Color, type EngineEvent, type ErrorEvent, type StateEvent } from '../protocol';
import { useEngine } from '../useEngine';
import { onEngineDied } from '../shell';
import { GameContext, type GameValue, type Suggestion } from './GameContext';
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
  choices: [],
  paused: false,
  opening: { step: 'play', actor: 'black', options: [] },
  limitsSync: 0,
  suggestion: null,
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
  pvFirst: 'black',
  rootScores: [],
  startedAt: null,
  lastMoveMs: 0,
  aiMoves: 0,
  aiTotalMs: 0,
};

// Garde le coup propose par le dernier suggest jusqu'a ce que le plateau change ;
// un simple changement de reglages ne l'efface pas.
function suggestionAfterState(prev: Suggestion | null, msg: StateEvent): Suggestion | null {
  if (msg.event === 'suggestion') {
    return msg.lastStats.best >= 0 ? { idx: msg.lastStats.best, score: msg.lastStats.score } : null;
  }
  if (msg.event === 'limits' || msg.event === 'weights') return prev;
  return null;
}

// Extrait la partie « etat de partie » d'un evenement state ; limitsSync avance
// quand le moteur annonce ses limites, pour resynchroniser les reglages.
function toGameState(prev: GameState, msg: StateEvent): GameState {
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
    choices: msg.choices ?? [],
    paused: msg.paused ?? false,
    opening: msg.opening ?? { step: 'play', actor: msg.toMove, options: [] },
    limitsSync: msg.event === 'limits' || msg.event === 'connected' ? prev.limitsSync + 1 : prev.limitsSync,
    suggestion: suggestionAfterState(prev.suggestion, msg),
  };
}

// Met a jour les temps de recherche apres un state : duree mesuree par le
// moteur pour le dernier coup, cumul des coups IA remis a zero en debut de partie.
function timeAfterState(prev: SearchValue, msg: StateEvent): SearchValue {
  const fresh = msg.event === 'new_game' || msg.event === 'connected';
  const searched = msg.event === 'ai_move' || msg.event === 'suggestion';
  const aiMoves = fresh ? 0 : prev.aiMoves;
  const aiTotalMs = fresh ? 0 : prev.aiTotalMs;
  return {
    ...prev,
    searching: false,
    startedAt: null,
    lastMoveMs: fresh ? 0 : searched ? msg.lastStats.elapsedMs : prev.lastMoveMs,
    aiMoves: msg.event === 'ai_move' ? aiMoves + 1 : aiMoves,
    aiTotalMs: msg.event === 'ai_move' ? aiTotalMs + msg.lastStats.elapsedMs : aiTotalMs,
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
  const toMoveRef = useRef<Color>('black');

  const handleEvent = useCallback((event: EngineEvent) => {
    if (event.type === 'state') {
      searchingRef.current = false;
      toMoveRef.current = event.toMove;
      setGame((prev) => toGameState(prev, event));
      setLastError(null);
      setSearch((prev) => timeAfterState(prev, event));
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
        pvFirst: started ? prev.pvFirst : toMoveRef.current,
        rootScores: event.rootScores,
        startedAt: started && prev.startedAt !== null ? prev.startedAt : Date.now() - event.elapsedMs,
        lastMoveMs: prev.lastMoveMs,
        aiMoves: prev.aiMoves,
        aiTotalMs: prev.aiTotalMs,
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
