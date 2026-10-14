import type { HistoryEntry } from './protocol';

// Associe a chaque intersection le numero (a partir de 1) du dernier coup qui
// y a ete joue. Une pierre capturee garde son numero : croiser avec le plateau.
export function moveNumbers(history: HistoryEntry[]): Map<number, number> {
  const numbers = new Map<number, number>();
  history.forEach((entry, i) => numbers.set(entry.idx, i + 1));
  return numbers;
}
