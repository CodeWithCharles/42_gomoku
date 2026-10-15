import { createContext, useContext } from 'react';
import type { Cell, Color, Command, Config, ErrorEvent, HistoryEntry, Limits, OpeningState, Pairs, Players, Status, WinReason } from '../protocol';
import type { ConnectionState } from '../useEngine';

export interface Suggestion {
  idx: number;
  score: number;
}

export interface GameValue {
  board: Cell[];
  toMove: Color;
  status: Status;
  winReason: WinReason;
  pairs: Pairs;
  players: Players;
  config: Config;
  limits: Limits;
  history: HistoryEntry[];
  opening: OpeningState;
  limitsSync: number;
  suggestion: Suggestion | null;
  lastError: ErrorEvent | null;
  connection: ConnectionState;
  engineDied: boolean;
  send: (cmd: Command) => void;
}

export const GameContext = createContext<GameValue | null>(null);

// Donne l'etat de partie ; echoue tot si le provider est absent.
export function useGame(): GameValue {
  const value = useContext(GameContext);
  if (!value) throw new Error('useGame hors de EngineProvider');
  return value;
}
