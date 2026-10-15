import type { PastViewProps } from './playback.types';
import './PastView.css';

// Calque a la taille du goban : cadre ambre et badge sur le bord haut, sans
// couvrir la grille.
export function PastView({ position, length, onGoLive }: PastViewProps) {
  return (
    <div className="past-view">
      <div className="past-view__frame" aria-hidden="true" />
      <div className="past-view__badge" role="status">
        <span className="past-view__dot" aria-hidden="true" />
        <span className="past-view__title">Vue du passé</span>
        <span className="past-view__pos">
          position {position} / {length}
        </span>
        <button
          type="button"
          className="past-view__live"
          onClick={onGoLive}
          title="Revenir au direct (Fin, ou clic sur le plateau)"
        >
          Direct <kbd>Fin</kbd>
        </button>
      </div>
    </div>
  );
}
