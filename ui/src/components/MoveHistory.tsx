import { useEffect, useRef } from 'react';
import { idxToLabel } from '../protocol';
import { useGame } from '../engine/GameContext';
import { useView } from '../view/ViewContext';
import './MoveHistory.css';

// Historique des coups en lecture seule, qui defile jusqu'au dernier coup, et
// interrupteur des numeros de coups sur le plateau.
export function MoveHistory() {
  const { history } = useGame();
  const { showMoveNumbers, toggleMoveNumbers } = useView();
  const listRef = useRef<HTMLOListElement>(null);

  useEffect(() => {
    const list = listRef.current;
    if (list) list.scrollTop = list.scrollHeight;
  }, [history.length]);

  return (
    <section className="panel history" aria-label="Historique des coups">
      <div className="history-head">
        <h2 className="panel-title">
          Historique<span className="history-count">{history.length}</span>
        </h2>
        <label className="switch" title="Afficher les numéros de coups (raccourci M)">
          <input type="checkbox" checked={showMoveNumbers} onChange={toggleMoveNumbers} />
          Numéros<kbd>M</kbd>
        </label>
      </div>
      {history.length === 0 && <p className="empty">Aucun coup joué.</p>}
      <ol className="history-list" ref={listRef}>
        {history.map((entry, k) => (
          <li key={k} className={k === history.length - 1 ? 'hrow hrow-last' : 'hrow'}>
            <span className="hnum">{k + 1}</span>
            <span className={entry.player === 'black' ? 'mini-stone ms-b' : 'mini-stone ms-w'} />
            <span>{idxToLabel(entry.idx)}</span>
            {entry.captured.length > 0 && (
              <span className="hcap" title={`${entry.captured.length} pierres capturées par ce coup`}>
                {entry.captured.length} prises
              </span>
            )}
          </li>
        ))}
      </ol>
    </section>
  );
}
