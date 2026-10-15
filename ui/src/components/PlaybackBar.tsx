import type { PlaybackBarProps } from './playback.types';
import './PlaybackBar.css';

type Tone = 'live' | 'step' | 'idle' | 'past';

interface Status {
  tone: Tone;
  title: string;
  detail: string;
}

// Ligne d'etat : ou se trouve le curseur et ce que fait l'IA.
function statusOf(props: PlaybackBarProps): Status {
  const { aiOnly, playback, position, length } = props;
  if (position !== null) return { tone: 'past', title: `position ${position} / ${length}`, detail: 'lecture seule' };
  if (!aiOnly) {
    return playback === 'searching'
      ? { tone: 'step', title: 'en direct', detail: "l'IA réfléchit" }
      : { tone: 'idle', title: 'en direct', detail: 'navigation seule' };
  }
  if (playback === 'playing') return { tone: 'live', title: 'en direct', detail: 'lecture continue' };
  if (playback === 'searching') return { tone: 'step', title: 'en direct', detail: "l'IA finit son coup" };
  return { tone: 'idle', title: 'en direct', detail: 'en pause' };
}

// Icone : debut de partie.
function IconFirst() {
  return (
    <svg viewBox="0 0 16 16" aria-hidden="true">
      <path d="M3 3v10" className="playback__stroke" />
      <path d="M14 3 9 8l5 5zM9 3 4 8l5 5z" className="playback__fill-shape" />
    </svg>
  );
}

// Icone : reculer d'une action.
function IconBack() {
  return (
    <svg viewBox="0 0 16 16" aria-hidden="true">
      <path d="M11.5 3 5 8l6.5 5z" className="playback__fill-shape" />
    </svg>
  );
}

// Icone : lecture.
function IconPlay() {
  return (
    <svg viewBox="0 0 16 16" aria-hidden="true">
      <path d="M4.5 2.5 13 8l-8.5 5.5z" className="playback__fill-shape" />
    </svg>
  );
}

// Icone : pause.
function IconPause() {
  return (
    <svg viewBox="0 0 16 16" aria-hidden="true">
      <path d="M4 3h3v10H4zM9 3h3v10H9z" className="playback__fill-shape" />
    </svg>
  );
}

// Icone : avancer d'une action dans l'historique.
function IconForward() {
  return (
    <svg viewBox="0 0 16 16" aria-hidden="true">
      <path d="M4.5 3 11 8l-6.5 5z" className="playback__fill-shape" />
    </svg>
  );
}

// Icone : un seul pas de l'IA (triangle et barre).
function IconStep() {
  return (
    <svg viewBox="0 0 16 16" aria-hidden="true">
      <path d="M3 3 9.5 8 3 13z" className="playback__fill-shape" />
      <path d="M12.5 3v10" className="playback__stroke" />
    </svg>
  );
}

// Icone : retour au direct.
function IconLast() {
  return (
    <svg viewBox="0 0 16 16" aria-hidden="true">
      <path d="M13 3v10" className="playback__stroke" />
      <path d="M2 3l5 5-5 5zM7 3l5 5-5 5z" className="playback__fill-shape" />
    </svg>
  );
}

// Commandes de lecture IA contre IA et de navigation en lecture seule ;
// Lecture / Pause n'existe que si aiOnly.
export function PlaybackBar(props: PlaybackBarProps) {
  const { aiOnly, playback, position, length, canBack, canForward, forwardSteps } = props;
  const status = statusOf(props);
  const playing = playback === 'playing';
  const stepPending = forwardSteps && playback === 'searching';
  const progress = position === null ? 100 : length > 0 ? (position / length) * 100 : 0;
  const playLabel = playing ? 'Pause' : position !== null ? 'Revenir au direct et lancer la lecture' : 'Lecture';
  const forwardLabel = forwardSteps ? "Pas : l'IA joue une seule action" : "Avancer d'une action";
  const forwardClass = [
    'playback__btn',
    forwardSteps ? 'playback__btn--step' : '',
    stepPending ? 'playback__btn--busy' : '',
  ].join(' ');

  return (
    <div className="playback" role="toolbar" aria-label="Lecture de la partie" data-tone={status.tone}>
      <div className="playback__buttons">
        <button type="button" className="playback__btn" disabled={!canBack} onClick={props.onFirst} title="Début de partie (Début)" aria-label="Début de partie">
          <IconFirst />
        </button>
        <button type="button" className="playback__btn" disabled={!canBack} onClick={props.onBack} title="Reculer d'une action (←)" aria-label="Reculer d'une action">
          <IconBack />
        </button>
        {aiOnly && (
          <button
            type="button"
            className="playback__btn playback__btn--play"
            aria-pressed={playing}
            onClick={props.onTogglePlay}
            title={`${playLabel} (Espace)`}
            aria-label={playLabel}
          >
            {playing ? <IconPause /> : <IconPlay />}
          </button>
        )}
        <button
          type="button"
          className={forwardClass}
          disabled={!canForward}
          aria-busy={stepPending}
          onClick={props.onForward}
          title={`${forwardLabel} (→)`}
          aria-label={forwardLabel}
        >
          {stepPending ? <span className="playback__spinner" aria-hidden="true" /> : forwardSteps ? <IconStep /> : <IconForward />}
          {forwardSteps && <span>Pas</span>}
        </button>
        <button type="button" className="playback__btn" disabled={position === null} onClick={props.onLast} title="Revenir au direct (Fin)" aria-label="Revenir au direct">
          <IconLast />
        </button>
      </div>

      <div className="playback__status" aria-live="polite">
        {status.tone === 'step' ? <span className="playback__spinner playback__spinner--small" aria-hidden="true" /> : <span className="playback__dot" aria-hidden="true" />}
        <span className="playback__title">{status.title}</span>
        <span className="playback__detail">{status.detail}</span>
      </div>

      <div className="playback__track" aria-hidden="true">
        <div className="playback__progress" style={{ width: `${progress}%` }} />
      </div>
    </div>
  );
}
