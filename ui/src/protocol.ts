// Types du protocole moteur <-> interface.
//
// ATTENTION : ce fichier, src/server/session.cpp et docs/PROTOCOL.md doivent
// rester d'accord. Toute modification du protocole les touche tous les trois
// dans le meme commit.

export const SIZE = 19;
export const CELLS = SIZE * SIZE;

export type Color = 'black' | 'white';
export type Occupant = Color | 'none';
export type Cell = 0 | 1 | 2;
export type PlayerKind = 'human' | 'ai';
export type Status = 'ongoing' | 'black_wins' | 'white_wins' | 'draw';
export type StateEventName =
  | 'connected'
  | 'new_game'
  | 'move'
  | 'ai_move'
  | 'undo'
  | 'limits'
  | 'weights';
export type ErrorCode =
  | 'legal'
  | 'out_of_bounds'
  | 'occupied'
  | 'double_three'
  | 'game_over'
  | 'not_your_turn'
  | 'unknown';

export interface Config {
  captures: boolean;
  doubleThree: boolean;
  endgameCapture: boolean;
  opening: string;
}

export interface Limits {
  maxDepth: number;
  budgetMs: number;
  maxCandidates: number;
}

export interface Players {
  black: PlayerKind;
  white: PlayerKind;
}

export interface Pairs {
  black: number;
  white: number;
}

export interface HistoryEntry {
  idx: number;
  player: Color;
  captured: number[];
}

export interface RootScore {
  idx: number;
  score: number;
}

export interface StateEvent {
  type: 'state';
  event: StateEventName;
  board: Cell[];
  size: number;
  toMove: Color;
  status: Status;
  winReason: string;
  pairs: Pairs;
  players: Players;
  config: Config;
  limits: Limits;
  history: HistoryEntry[];
  lastStats: Record<string, unknown>;
}

export interface ProgressEvent {
  type: 'progress';
  depth: number;
  score: number;
  best: number;
  nodes: number;
  leaves: number;
  cutoffs: number;
  ttHits: number;
  elapsedMs: number;
  pv: number[];
  rootScores: RootScore[];
}

export interface ErrorEvent {
  type: 'error';
  message: string;
  code: ErrorCode;
}

export type EngineEvent = StateEvent | ProgressEvent | ErrorEvent;

export type Command =
  | { type: 'new-game'; config?: Config; players?: Players }
  | { type: 'play'; idx: number }
  | { type: 'suggest' }
  | { type: 'undo' }
  | { type: 'limits'; maxDepth: number; budgetMs: number; maxCandidates: number }
  | { type: 'weights'; weights: Record<string, number> }
  | { type: 'stop' };

// Convertit un index lineaire en coordonnees de plateau.
export function idxToXY(idx: number): { x: number; y: number } {
  return { x: idx % SIZE, y: Math.floor(idx / SIZE) };
}

// Convertit des coordonnees de plateau en index lineaire.
export function xyToIdx(x: number, y: number): number {
  return y * SIZE + x;
}

// Nomme une intersection a la maniere des diagrammes de go : A1, T19.
export function idxToLabel(idx: number): string {
  const { x, y } = idxToXY(idx);
  return `${'ABCDEFGHJKLMNOPQRST'[x]}${SIZE - y}`;
}
