import { useState } from 'react';
import { winReasonLabel } from '../protocol';
import { useGame } from '../engine/GameContext';
import { useView } from '../view/ViewContext';
import './GameOverBanner.css';

// Carte de fin de partie posee sur le plateau, sans bloquer le reste de
// l'interface. Fermee par "Voir le plateau", elle revient a la fin suivante.
export function GameOverBanner() {
  const { status, winReason, history, pairs, connection, engineDied, send } = useGame();
  const { seats, rules } = useView();
  const [closedFor, setClosedFor] = useState<string | null>(null);
  const ending = `${status}-${history.length}`;

  if (status === 'ongoing' || closedFor === ending) return null;

  const winner = status === 'black_wins' ? 'black' : status === 'white_wins' ? 'white' : null;
  const reason = winReasonLabel(winReason);
  const offline = connection !== 'open' || engineDied;

  return (
    <div className="banner-wrap">
      <section className="banner" role="dialog" aria-modal="false" aria-labelledby="banner-title">
        <p className="banner-kicker">Fin de partie</p>
        <div className="banner-head">
          <span className={winner === 'black' ? 'mini-stone ms-b' : winner === 'white' ? 'mini-stone ms-w' : 'mini-stone ms-none'} />
          <h2 id="banner-title" className="banner-title">
            {winner === 'black' ? 'Les noirs gagnent' : winner === 'white' ? 'Les blancs gagnent' : 'Partie nulle'}
          </h2>
        </div>
        {reason && <p className="banner-reason">{reason.charAt(0).toUpperCase() + reason.slice(1)}</p>}
        <dl className="banner-stats">
          <div className="stat">
            <dt>coups</dt>
            <dd>{history.length}</dd>
          </div>
          <div className="stat">
            <dt>paires noirs</dt>
            <dd>{pairs.black}</dd>
          </div>
          <div className="stat">
            <dt>paires blancs</dt>
            <dd>{pairs.white}</dd>
          </div>
        </dl>
        <div className="banner-actions">
          <button
            className="btn btn-primary"
            type="button"
            title="Nouvelle partie (raccourci N)"
            disabled={offline}
            onClick={() => send({ type: 'new-game', players: seats, config: rules })}
          >
            <span>Nouvelle partie</span>
            <kbd>N</kbd>
          </button>
          <button className="btn btn-center" type="button" onClick={() => setClosedFor(ending)}>
            Voir le plateau
          </button>
        </div>
      </section>
    </div>
  );
}
