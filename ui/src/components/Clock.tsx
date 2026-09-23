import { useEffect, useState } from 'react';
import { useSearch } from '../engine/SearchContext';
import './Clock.css';

const BUDGET_MS = 500;

// Formate une duree en millisecondes avec deux decimales de seconde.
function format(ms: number): string {
  return `${(ms / 1000).toFixed(2)} s`;
}

// Chronometre exige par le sujet. Pendant une recherche, il est lisse
// localement par requestAnimationFrame entre deux evenements progress, qui
// n'arrivent qu'a chaque iteration de l'approfondissement.
export function Clock() {
  const { searching, startedAt, elapsedMs, lastMoveMs } = useSearch();
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
  const over = live > BUDGET_MS;

  return (
    <section className="clock" aria-live="polite">
      <span className="clock-label">{searching ? 'recherche' : 'dernier coup'}</span>
      <span className={over ? 'clock-value clock-over' : 'clock-value'}>{format(live)}</span>
      <span className="clock-budget">budget {format(BUDGET_MS)}</span>
      <div className="clock-gauge">
        <div className="clock-fill" style={{ width: `${Math.min(100, (live / BUDGET_MS) * 100)}%` }} />
      </div>
    </section>
  );
}
