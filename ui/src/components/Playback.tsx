import { useEffect, useRef } from 'react';
import { useGame } from '../engine/GameContext';
import { useSearch } from '../engine/SearchContext';
import { useView } from '../view/ViewContext';
import { useTimeline } from '../view/useTimeline';
import { useShortcuts } from '../useShortcuts';
import { MoveHistory } from './MoveHistory';
import { PlaybackBar } from './PlaybackBar';
import type { PlaybackState } from './playback.types';

// Barre de lecture branchee sur le moteur, avec ses raccourcis : Espace pour
// Lecture / Pause, fleches, Debut et Fin. Revient au direct quand la partie
// raccourcit (annulation, nouvelle partie).
export function PlaybackControls() {
  const { paused, status, connection, engineDied, send } = useGame();
  const { searching } = useSearch();
  const { setCursor } = useView();
  const { length, position, aiOnly, running, goTo, goLive } = useTimeline();
  const lengthRef = useRef(length);
  const offline = connection !== 'open' || engineDied;
  const playback: PlaybackState = searching && !running ? 'searching' : running ? 'playing' : 'paused';
  const forwardSteps = position === null && aiOnly && paused && status === 'ongoing';
  const canBack = (position ?? length) > 0;
  const canForward = position !== null || (forwardSteps && !offline);

  useEffect(() => {
    if (length < lengthRef.current) setCursor(null);
    lengthRef.current = length;
  }, [length, setCursor]);

  const back = () => goTo((position ?? length) - 1);
  const forward = () => {
    if (position !== null) goTo(position + 1);
    else if (forwardSteps && !searching) send({ type: 'step' });
  };
  const first = () => goTo(0);
  const togglePlay = () => {
    if (running) {
      send({ type: 'pause' });
      return;
    }
    goLive();
    send({ type: 'resume' });
  };

  useShortcuts([
    { key: ' ', enabled: aiOnly && !offline, run: togglePlay },
    { key: 'ArrowLeft', enabled: canBack, run: back },
    { key: 'ArrowRight', enabled: canForward, run: forward },
    { key: 'Home', enabled: canBack, run: first },
    { key: 'End', enabled: position !== null, run: goLive },
  ]);

  return (
    <PlaybackBar
      aiOnly={aiOnly}
      playback={playback}
      position={position}
      length={length}
      canBack={canBack}
      canForward={canForward}
      forwardSteps={forwardSteps}
      onFirst={first}
      onBack={back}
      onTogglePlay={togglePlay}
      onForward={forward}
      onLast={goLive}
    />
  );
}

// Historique navigable : un clic revoit la position atteinte apres cette action.
export function HistoryTimeline() {
  const { timeline, position, goTo } = useTimeline();
  const items = timeline.map((entry) => entry.item);
  return <MoveHistory items={items} cursor={position} onSelect={goTo} />;
}
