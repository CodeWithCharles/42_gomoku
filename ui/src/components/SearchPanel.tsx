import { idxToLabel, type Color } from '../protocol';
import { useGame } from '../engine/GameContext';
import { useSearch } from '../engine/SearchContext';
import { count, scoreLabel, scoreTone } from '../format';
import { SearchSettings } from './SearchSettings';
import './SearchPanel.css';

const LADDER_MAX = 24;

const SIDE_LABEL: Record<Color, string> = { black: 'noirs', white: 'blancs' };

// Couleur qui joue le coup de rang `rank` d'une variante commencee par `first`.
function pvColor(first: Color, rank: number): Color {
  if (rank % 2 === 0) return first;
  return first === 'black' ? 'white' : 'black';
}

// Labo IA : profondeur, score, statistiques et variante de la recherche, puis
// les reglages de la recherche.
export function SearchPanel() {
  const { searching, depth, score, best, nodes, leaves, cutoffs, ttHits, elapsedMs, pv, pvFirst } = useSearch();
  const { limits, players, toMove, opening } = useGame();
  const searched = depth > 0;
  const nps = elapsedMs > 0 ? Math.round((nodes / elapsedMs) * 1000) : 0;
  const rungs = Math.max(1, Math.min(limits.maxDepth, LADDER_MAX));
  const filled = Math.round((depth / Math.max(1, limits.maxDepth)) * rungs);
  const tone = scoreTone(score);
  const shown = (n: number) => (searched ? count(n) : '-');
  const subtitle = searching
    ? opening.step === 'choose'
      ? "choix de couleur de l'IA"
      : players[toMove] === 'ai'
      ? `coup de l'IA, ${SIDE_LABEL[toMove]}`
      : `suggestion pour les ${SIDE_LABEL[toMove]}`
    : searched
      ? 'dernière recherche terminée'
      : 'aucune recherche';

  return (
    <section className={searching ? 'panel lab lab-live' : 'panel lab'} aria-label="Labo IA">
      <div className="lab-head">
        <div>
          <h2 className="panel-title">Labo IA</h2>
          <p className="lab-title">Recherche</p>
          <span className="lab-sub">{subtitle}</span>
        </div>
        {searching && (
          <span className="live">
            <span className="live-dot" />
            en cours
          </span>
        )}
      </div>

      <div className="depth">
        <div className="depth-top">
          <div className="depth-figure">
            <span className="depth-num">{searched ? depth : '-'}</span>
            <span className="depth-max">/ {limits.maxDepth}</span>
          </div>
          <span className="depth-label">
            profondeur
            <br />
            atteinte / max
          </span>
        </div>
        <div className="ladder" aria-hidden="true">
          {Array.from({ length: rungs }, (_, k) => (
            <span key={k} className={k < filled ? 'rung rung-on' : 'rung'} />
          ))}
        </div>
      </div>

      <div className="score-line">
        <span className="score-key">score</span>
        <span className={tone === 'win' ? 'score-val score-win' : tone === 'loss' ? 'score-val score-loss' : 'score-val'}>
          {searched ? scoreLabel(score) : '-'}
        </span>
      </div>

      <dl className="stats">
        <div className="stat">
          <dt>meilleur coup</dt>
          <dd className="stat-best">{best >= 0 ? idxToLabel(best) : '-'}</dd>
        </div>
        <div className="stat">
          <dt>nœuds/s</dt>
          <dd>{nps > 0 ? count(nps) : '-'}</dd>
        </div>
        <div className="stat">
          <dt>nœuds</dt>
          <dd>{shown(nodes)}</dd>
        </div>
        <div className="stat">
          <dt>feuilles</dt>
          <dd>{shown(leaves)}</dd>
        </div>
        <div className="stat">
          <dt>coupures</dt>
          <dd>{shown(cutoffs)}</dd>
        </div>
        <div className="stat">
          <dt>accès table de transposition</dt>
          <dd>{shown(ttHits)}</dd>
        </div>
      </dl>

      <div className="pv-block">
        <h3 className="panel-title">Variante principale</h3>
        {pv.length > 0 ? (
          <div className="pv">
            {pv.map((idx, rank) => (
              <span key={`${rank}-${idx}`} className="pv-chip">
                <span className={pvColor(pvFirst, rank) === 'black' ? 'mini-stone ms-b' : 'mini-stone ms-w'} />
                {idxToLabel(idx)}
              </span>
            ))}
          </div>
        ) : (
          <p className="pv-none">pas de variante</p>
        )}
      </div>

      <p className="note">Carte de chaleur : estimation, seul le meilleur coup a un score exact.</p>

      <SearchSettings />
    </section>
  );
}
