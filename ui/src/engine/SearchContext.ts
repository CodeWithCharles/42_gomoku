import { createContext, useContext } from 'react';
import type { RootScore } from '../protocol';

export interface SearchValue {
  searching: boolean;
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
  startedAt: number | null;
  lastMoveMs: number;
}

export const SearchContext = createContext<SearchValue | null>(null);

// Donne l'etat de la recherche ; echoue tot si le provider est absent.
export function useSearch(): SearchValue {
  const value = useContext(SearchContext);
  if (!value) throw new Error('useSearch hors de EngineProvider');
  return value;
}
