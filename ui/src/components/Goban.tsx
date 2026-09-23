import { memo } from 'react';
import { SIZE, idxToLabel, idxToXY, xyToIdx } from '../protocol';
import { useGame } from '../engine/GameContext';
import { useSearch } from '../engine/SearchContext';
import './Goban.css';

const STAR_POINTS = [3, 9, 15].flatMap((y) => [3, 9, 15].map((x) => xyToIdx(x, y)));

// Dessine la variante principale et la carte de chaleur des coups racine. Isole
// dans son propre composant : lui seul se repeint a chaque tick de progression.
function SearchOverlay() {
  const { pv, rootScores, best, searching } = useSearch();
  if (!searching) return null;
  const top = rootScores[0]?.score ?? 1;
  const bottom = rootScores[rootScores.length - 1]?.score ?? 0;
  const span = Math.max(1, top - bottom);
  return (
    <g className="goban-overlay">
      {rootScores.map(({ idx, score }) => {
        const { x, y } = idxToXY(idx);
        return (
          <circle
            key={idx}
            cx={x}
            cy={y}
            r={0.42}
            className="goban-heat"
            opacity={0.12 + 0.55 * ((score - bottom) / span)}
          />
        );
      })}
      {pv.map((idx, rank) => {
        const { x, y } = idxToXY(idx);
        return <circle key={`pv-${idx}`} cx={x} cy={y} r={0.2} className="goban-pv" opacity={1 - rank * 0.2} />;
      })}
      {best >= 0 && <circle cx={idxToXY(best).x} cy={idxToXY(best).y} r={0.46} className="goban-best" />}
    </g>
  );
}

// Plateau 19x19 en SVG. Ne consomme que l'etat de partie : les 361 pierres ne
// sont redessinees que lorsqu'un coup est joue.
function GobanBoard() {
  const { board, status, toMove, players, history, send } = useGame();
  const last = history.length > 0 ? history[history.length - 1] : null;
  const myTurn = status === 'ongoing' && players[toMove] === 'human';

  return (
    <svg className="goban" viewBox="-1 -1 20 20" role="grid" aria-label="Plateau de Gomoku">
      <rect x={-1} y={-1} width={20} height={20} className="goban-bg" />
      {Array.from({ length: SIZE }, (_, i) => (
        <g key={`grid-${i}`}>
          <line x1={0} y1={i} x2={SIZE - 1} y2={i} className="goban-line" />
          <line x1={i} y1={0} x2={i} y2={SIZE - 1} className="goban-line" />
        </g>
      ))}
      {STAR_POINTS.map((idx) => {
        const { x, y } = idxToXY(idx);
        return <circle key={`star-${idx}`} cx={x} cy={y} r={0.1} className="goban-star" />;
      })}
      <SearchOverlay />
      {board.map((cell, idx) => {
        if (cell === 0) return null;
        const { x, y } = idxToXY(idx);
        return (
          <circle
            key={idx}
            cx={x}
            cy={y}
            r={0.45}
            className={cell === 1 ? 'goban-stone goban-black' : 'goban-stone goban-white'}
          />
        );
      })}
      {last && (
        <circle cx={idxToXY(last.idx).x} cy={idxToXY(last.idx).y} r={0.16} className="goban-last" />
      )}
      {board.map((cell, idx) => {
        if (cell !== 0 || !myTurn) return null;
        const { x, y } = idxToXY(idx);
        return (
          <circle
            key={`hit-${idx}`}
            cx={x}
            cy={y}
            r={0.5}
            className="goban-hit"
            onClick={() => send({ type: 'play', idx })}
          >
            <title>{idxToLabel(idx)}</title>
          </circle>
        );
      })}
    </svg>
  );
}

export const Goban = memo(GobanBoard);
