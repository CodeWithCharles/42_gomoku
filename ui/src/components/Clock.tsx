import { useEffect, useState } from 'react';
import { useGame } from '../engine/GameContext';
import { useSearch } from '../engine/SearchContext';
import { secs } from '../format';
import './Clock.css';

const GAUGE_SPAN = 1.5;

// Chronometre exige par le sujet. Pendant une recherche, il est lisse
// localement par requestAnimationFrame entre deux evenements progress, qui
// n'arrivent qu'a chaque iteration de l'approfondissement. La couleur suit la
// moyenne des coups IA de la partie, comparee au budget moyen du moteur.
export function Clock() {
  const { searching, startedAt, elapsedMs, lastMoveMs, aiMoves, aiTotalMs } = useSearch();
  const { limits } = useGame();
  const [now, setNow] = useState(() => Date.now());

  useEffect(() => {
    if (!searching || startedAt === null) return;
    let frame = 0;
    const tick = () => {
      setNow(Date.now());
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [searching, startedAt]);

  const live = searching && startedAt !== null ? Math.max(elapsedMs, now - startedAt) : lastMoveMs;
  const budget = limits.budgetMs;
  const average = aiMoves > 0 ? aiTotalMs / aiMoves : 0;
  const over = aiMoves > 0 && average > budget;
  const fill = budget > 0 ? Math.min(1, average / (budget * GAUGE_SPAN)) * 100 : 0;

  return (
    <section className={over ? 'panel clock clock-over' : 'panel clock'} aria-live="polite" aria-label="Chronomètre de l'IA">
      <div className="clock-head">
        <h2 className="panel-title">{searching ? 'recherche' : 'dernier coup'}</h2>
        {searching && (
          <span className="clock-live">
            <span className="live-dot" />
            IA
          </span>
        )}
      </div>
      <div className="clock-value">
        {secs(live)}
        <span className="clock-unit">s</span>
      </div>
      <div className="clock-row">
        <span>moyenne</span>
        <span className={over ? 'avg avg-over' : 'avg'}>{aiMoves > 0 ? `${secs(average)} s` : '-'}</span>
      </div>
      <div className="gauge" role="img" aria-label={`moyenne ${secs(average)} s pour un budget de ${secs(budget)} s`}>
        <div className={over ? 'gauge-fill gauge-over' : 'gauge-fill'} style={{ width: `${fill.toFixed(1)}%` }} />
        <div className="gauge-mark" />
      </div>
      <div className="clock-foot">
        {aiMoves} {aiMoves > 1 ? 'coups IA' : 'coup IA'}, budget moyen {secs(budget)} s
      </div>
    </section>
  );
}
