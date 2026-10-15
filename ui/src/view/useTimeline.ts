import { useMemo } from 'react';
import { useGame } from '../engine/GameContext';
import { buildTimeline, type TimelineEntry } from '../timeline';
import { useView } from './ViewContext';

export interface TimelineView {
  timeline: TimelineEntry[];
  length: number;
  position: number | null;
  aiOnly: boolean;
  running: boolean;
  moves: number;
  recorded: TimelineEntry | null;
  goTo: (position: number) => void;
  goLive: () => void;
}

// Chronologie de la partie et position revue. La position vaut null en direct ;
// un curseur que la partie ne contient plus (annulation, nouvelle partie) compte
// comme le direct. Quitter le direct pendant une lecture IA contre IA met en pause.
export function useTimeline(): TimelineView {
  const { history, choices, players, status, paused, send } = useGame();
  const { cursor, setCursor } = useView();
  const timeline = useMemo(() => buildTimeline(history, choices), [history, choices]);
  const length = timeline.length;
  const position = cursor !== null && cursor < length ? cursor : null;
  const aiOnly = players.black === 'ai' && players.white === 'ai';
  const running = aiOnly && !paused && status === 'ongoing';
  const shownEnd = position ?? length;
  const recorded = shownEnd > 0 ? timeline[shownEnd - 1] : null;

  const goTo = (target: number) => {
    if (target >= length) {
      setCursor(null);
      return;
    }
    if (running) send({ type: 'pause' });
    setCursor(Math.max(0, target));
  };

  return {
    timeline,
    length,
    position,
    aiOnly,
    running,
    moves: recorded ? recorded.moves : 0,
    recorded,
    goTo,
    goLive: () => setCursor(null),
  };
}
