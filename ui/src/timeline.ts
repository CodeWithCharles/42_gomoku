import { CELLS, type Cell, type ChoiceEntry, type Color, type HistoryEntry, type Stats } from './protocol';
import type { TimelineItem } from './components/playback.types';

// Une action de la partie, coup ou choix de couleur, avec l'analyse enregistree
// quand l'IA l'a jouee, le nombre de coups poses une fois l'action faite, et la
// couleur qui ouvre la variante de cette analyse.
export interface TimelineEntry {
  item: TimelineItem;
  stats: Stats | null;
  moves: number;
  pvFirst: Color;
}

// Couleur au trait apres `moves` pierres posees en alternance depuis Noir.
function sideAfter(moves: number): Color {
  return moves % 2 === 0 ? 'black' : 'white';
}

// Fusionne coups et choix de couleur dans l'ordre ou ils ont eu lieu : un choix
// fait apres k pierres se place entre le k-ieme coup et le suivant.
export function buildTimeline(history: HistoryEntry[], choices: ChoiceEntry[]): TimelineEntry[] {
  const entries: TimelineEntry[] = [];
  let next = 0;
  const pushChoicesAt = (moves: number) => {
    while (next < choices.length && choices[next].afterMove <= moves) {
      const choice = choices[next];
      entries.push({
        item: { kind: 'choice', actor: choice.actor, choice: choice.choice, swapped: choice.swapped, byAi: choice.stats !== undefined },
        stats: choice.stats ?? null,
        moves,
        pvFirst: sideAfter(moves),
      });
      next += 1;
    }
  };
  history.forEach((entry, k) => {
    pushChoicesAt(k);
    entries.push({
      item: { kind: 'move', number: k + 1, idx: entry.idx, player: entry.player, captured: entry.captured.length, byAi: entry.stats !== undefined },
      stats: entry.stats ?? null,
      moves: k + 1,
      pvFirst: entry.player,
    });
  });
  pushChoicesAt(history.length);
  return entries;
}

// Plateau apres les `moves` premiers coups, reconstruit a partir des captures
// annoncees par le moteur : aucune regle n'est rejouee ici.
export function boardAt(history: HistoryEntry[], moves: number): Cell[] {
  const board = new Array<Cell>(CELLS).fill(0);
  history.slice(0, moves).forEach((entry) => {
    board[entry.idx] = entry.player === 'black' ? 1 : 2;
    entry.captured.forEach((idx) => {
      board[idx] = 0;
    });
  });
  return board;
}
