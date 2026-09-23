// Types du protocole moteur <-> interface.
//
// ATTENTION : ce fichier, backend/src/server/session.cpp et docs/PROTOCOL.md
// doivent rester d'accord. Toute modification du protocole les touche tous les
// trois dans le meme commit.

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
  | 'suggestion'
  | 'undo'
  | 'limits'
  | 'weights';

// Code stable, jamais affiche tel quel : l'UI en derive un libelle.
export type WinReason =
  | ''
  | 'five_in_a_row'
  | 'captures'
  | 'board_full'
  | 'resignation';

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

// Instantane d'une recherche. Emis tel quel pendant la reflexion (progress),
// et conserve dans state.lastStats une fois la recherche terminee.
export interface Stats {
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

export interface StateEvent {
  type: 'state';
  event: StateEventName;
  board: Cell[];
  size: number;
  toMove: Color;
  status: Status;
  winReason: WinReason;
  pairs: Pairs;
  players: Players;
  config: Config;
  limits: Limits;
  history: HistoryEntry[];
  lastStats: Stats;
}

export interface ProgressEvent extends Stats {
  type: 'progress';
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

// Stats vides, pour l'etat initial et entre deux recherches.
export const NO_STATS: Stats = {
  depth: 0, score: 0, best: -1, nodes: 0, leaves: 0,
  cutoffs: 0, ttHits: 0, elapsedMs: 0, pv: [], rootScores: [],
};

// Libelle affichable d'une condition de fin de partie.
export function winReasonLabel(reason: WinReason): string {
  switch (reason) {
    case 'five_in_a_row': return 'cinq alignés';
    case 'captures': return 'dix pierres capturées';
    case 'board_full': return 'plateau plein';
    case 'resignation': return 'abandon';
    default: return '';
  }
}

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