import { memo, useEffect, useState } from 'react';
import { SIZE, idxToLabel, idxToXY, type Cell, type Color } from '../protocol';
import { useGame } from '../engine/GameContext';
import { useSearch } from '../engine/SearchContext';
import { useView } from '../view/ViewContext';
import { moveNumbers } from '../history';
import { scoreLabel } from '../format';
import './Goban.css';

const VIEW = 1040;
const SPACING = 50;
const MARGIN = 70;
const STONE_R = 23;
const HEAT_CAP = 20_000;
const FADE_MS = 260;
const COLUMNS = 'ABCDEFGHJKLMNOPQRST';
const LINES = Array.from({ length: SIZE }, (_, k) => k);
const STAR_POINTS = [3, 9, 15].flatMap((y) => [3, 9, 15].map((x) => ({ x, y })));

// Position en unites SVG d'une ligne ou d'une colonne du plateau.
function px(v: number): number {
  return MARGIN + v * SPACING;
}

// Centre en unites SVG d'une intersection.
function center(idx: number): { cx: number; cy: number } {
  const { x, y } = idxToXY(idx);
  return { cx: px(x), cy: px(y) };
}

// Degrade de pierre a utiliser pour une couleur.
function stoneFill(color: Color): string {
  return color === 'black' ? 'url(#stoneB)' : 'url(#stoneW)';
}

// Valeur de case occupee par une couleur.
function cellOf(color: Color): Cell {
  return color === 'black' ? 1 : 2;
}

// Fond du plateau : degrades, halo, grille, hoshis et coordonnees. Ne depend
// de rien, il n'est dessine qu'une fois.
const Backdrop = memo(function Backdrop() {
  return (
    <>
      <defs>
        <radialGradient id="boardBg" cx="0.5" cy="0.45" r="0.75">
          <stop offset="0" style={{ stopColor: 'var(--board-1)' }} />
          <stop offset="1" style={{ stopColor: 'var(--board-2)' }} />
        </radialGradient>
        <filter id="haze" x="-30%" y="-30%" width="160%" height="160%">
          <feGaussianBlur stdDeviation="90" />
        </filter>
        <clipPath id="boardClip">
          <rect x="0" y="0" width={VIEW} height={VIEW} rx="14" />
        </clipPath>
        <radialGradient id="stoneB" cx="0.4" cy="0.36" r="0.68" fx="0.33" fy="0.27">
          <stop offset="0" style={{ stopColor: 'var(--stone-b-1)' }} />
          <stop offset="0.45" style={{ stopColor: 'var(--stone-b-2)' }} />
          <stop offset="1" style={{ stopColor: 'var(--stone-b-3)' }} />
        </radialGradient>
        <radialGradient id="stoneW" cx="0.42" cy="0.38" r="0.7" fx="0.34" fy="0.28">
          <stop offset="0" style={{ stopColor: 'var(--stone-w-1)' }} />
          <stop offset="0.58" style={{ stopColor: 'var(--stone-w-2)' }} />
          <stop offset="1" style={{ stopColor: 'var(--stone-w-3)' }} />
        </radialGradient>
        <radialGradient id="nacre" cx="0.36" cy="0.3" r="0.45">
          <stop offset="0" style={{ stopColor: 'var(--stone-nacre)' }} />
          <stop offset="1" style={{ stopColor: 'var(--stone-nacre-clear)' }} />
        </radialGradient>
        <filter id="soft" x="-50%" y="-50%" width="200%" height="200%">
          <feGaussianBlur stdDeviation="6" />
        </filter>
      </defs>
      <rect x="0" y="0" width={VIEW} height={VIEW} rx="14" fill="url(#boardBg)" />
      <g clipPath="url(#boardClip)" className="no-hit">
        <ellipse cx="520" cy="470" rx="340" ry="300" filter="url(#haze)" className="board-halo" />
      </g>
      <rect x="1" y="1" width={VIEW - 2} height={VIEW - 2} rx="13" className="board-border" />
      <g className="no-hit">
        {LINES.map((k) => {
          const cls = k === 0 || k === SIZE - 1 ? 'gl-edge' : 'gl';
          return (
            <g key={k}>
              <line x1={px(k)} y1={px(0)} x2={px(k)} y2={px(SIZE - 1)} className={cls} />
              <line x1={px(0)} y1={px(k)} x2={px(SIZE - 1)} y2={px(k)} className={cls} />
            </g>
          );
        })}
        {STAR_POINTS.map(({ x, y }) => (
          <circle key={`${x}-${y}`} cx={px(x)} cy={px(y)} r="6" className="hoshi" />
        ))}
        {LINES.map((k) => (
          <g key={k}>
            <text x={px(k)} y="33" className="coord">{COLUMNS[k]}</text>
            <text x={px(k)} y="1007" className="coord">{COLUMNS[k]}</text>
            <text x="32" y={px(k)} className="coord">{SIZE - k}</text>
            <text x="1008" y={px(k)} className="coord">{SIZE - k}</text>
          </g>
        ))}
      </g>
    </>
  );
});

// Carte de chaleur des coups racine, sous les pierres. Les scores racine ne sont
// ni tries ni exacts (majorants hors meilleur coup) : l'echelle part de leur
// min/max, scores decisifs bornes, et reste une estimation.
function HeatLayer({ board }: { board: Cell[] }) {
  const { searching, rootScores } = useSearch();
  if (!searching) return null;
  const free = rootScores.filter(({ idx }) => board[idx] === 0);
  if (free.length === 0) return null;
  const bounded = free.map(({ score }) => Math.max(-HEAT_CAP, Math.min(HEAT_CAP, score)));
  const top = Math.max(...bounded);
  const bottom = Math.min(...bounded);
  return (
    <g className="no-hit">
      {free.map(({ idx }, k) => {
        const { cx, cy } = center(idx);
        const t = top === bottom ? 1 : (bounded[k] - bottom) / (top - bottom);
        return (
          <rect key={idx} x={cx - 21} y={cy - 21} width="42" height="42" rx="9" className="heat" opacity={0.1 + 0.5 * t} />
        );
      })}
    </g>
  );
}

// Variante principale et anneau du meilleur coup, au-dessus des pierres. Seul
// ce calque et la carte de chaleur se repeignent a chaque tick de progression.
function AnalysisLayer({ board }: { board: Cell[] }) {
  const { searching, pv, best } = useSearch();
  if (!searching) return null;
  return (
    <g className="no-hit">
      {pv.map((idx, rank) => {
        if (board[idx] !== 0) return null;
        const { cx, cy } = center(idx);
        return (
          <circle
            key={`pv-${rank}-${idx}`}
            cx={cx}
            cy={cy}
            r={9 - rank * 0.4}
            className="pv-dot"
            opacity={Math.max(0.2, 1 - rank * 0.09)}
          />
        );
      })}
      {best >= 0 && board[best] === 0 && (
        <>
          <circle {...center(best)} r="26" className="best-halo" />
          <circle {...center(best)} r="26" className="best-ring" />
        </>
      )}
    </g>
  );
}

// Garde un instant les pierres que le dernier coup vient de capturer, pour
// animer leur disparition.
function useFadingStones(history: { player: Color; captured: number[] }[]): { idx: number; player: Color }[] {
  const [fading, setFading] = useState<{ idx: number; player: Color }[]>([]);
  const [seen, setSeen] = useState(history);

  if (seen !== history) {
    setSeen(history);
    const last = history[history.length - 1];
    const grew = history.length === seen.length + 1;
    if (grew && last && last.captured.length > 0) {
      const foe: Color = last.player === 'black' ? 'white' : 'black';
      setFading(last.captured.map((idx) => ({ idx, player: foe })));
    } else if (fading.length > 0) {
      setFading([]);
    }
  }

  useEffect(() => {
    if (fading.length === 0) return;
    const timer = setTimeout(() => setFading([]), FADE_MS);
    return () => clearTimeout(timer);
  }, [fading]);

  return fading;
}

// Plateau 19x19 en SVG. Ne consomme que l'etat de partie : les pierres ne sont
// redessinees que lorsqu'un coup est joue ou que la souris change de case.
function GobanBoard() {
  const { board, status, toMove, players, history, suggestion, connection, engineDied, send } = useGame();
  const { showMoveNumbers } = useView();
  const [hover, setHover] = useState<number | null>(null);
  const fading = useFadingStones(history);
  const canPlay = connection === 'open' && !engineDied && status === 'ongoing' && players[toMove] === 'human';
  const last = history.length > 0 ? history[history.length - 1] : null;
  const lastShown = last !== null && board[last.idx] === cellOf(last.player);
  const sugg = suggestion !== null && board[suggestion.idx] === 0 ? suggestion : null;
  const numbers = showMoveNumbers ? moveNumbers(history) : null;
  const showGhost = canPlay && hover !== null && board[hover] === 0 && sugg?.idx !== hover;

  return (
    <svg
      className="goban"
      viewBox={`0 0 ${VIEW} ${VIEW}`}
      role="grid"
      aria-label="Plateau de Gomoku 19 x 19"
      onMouseLeave={() => setHover(null)}
    >
      <Backdrop />
      <HeatLayer board={board} />

      <g className="no-hit" filter="url(#soft)">
        {board.map((cell, idx) => {
          if (cell === 0) return null;
          const { cx, cy } = center(idx);
          return <circle key={idx} cx={cx} cy={cy + 2} r={STONE_R} className="shadow" />;
        })}
      </g>

      <g className="no-hit">
        {board.map((cell, idx) => {
          if (cell === 0) return null;
          const { cx, cy } = center(idx);
          return cell === 1 ? (
            <g key={idx} className="stone">
              <circle cx={cx} cy={cy} r={STONE_R} fill="url(#stoneB)" />
              <circle cx={cx} cy={cy} r="22.2" className="stone-b-rim" />
            </g>
          ) : (
            <g key={idx} className="stone">
              <circle cx={cx} cy={cy} r={STONE_R} fill="url(#stoneW)" />
              <circle cx={cx} cy={cy} r={STONE_R} fill="url(#nacre)" />
              <circle cx={cx} cy={cy} r="22.6" className="stone-w-edge" />
            </g>
          );
        })}
        {fading.map(({ idx, player }) =>
          board[idx] === 0 ? (
            <circle key={`fade-${idx}`} {...center(idx)} r={STONE_R} fill={stoneFill(player)} className="stone-out" />
          ) : null,
        )}

        {lastShown && (
          <circle
            {...center(last.idx)}
            r={showMoveNumbers ? 19 : 7}
            className={`${showMoveNumbers ? 'last-ring' : 'last-dot'} ${last.player === 'black' ? 'last-b' : 'last-w'}`}
          />
        )}
        {numbers &&
          [...numbers].map(([idx, n]) => {
            if (board[idx] === 0) return null;
            const { cx, cy } = center(idx);
            return (
              <text
                key={`num-${idx}`}
                x={cx}
                y={cy + 1}
                className={`num ${board[idx] === 1 ? 'num-b' : 'num-w'} ${n >= 100 ? 'num-xs' : 'num-sm'}`}
              >
                {n}
              </text>
            );
          })}

        <AnalysisLayer board={board} />

        {sugg && (
          <>
            <circle {...center(sugg.idx)} r={STONE_R} fill={stoneFill(toMove)} className="ghost" />
            <circle {...center(sugg.idx)} r="28" className="sugg-ring" />
          </>
        )}
        {showGhost && <circle {...center(hover)} r={STONE_R} fill={stoneFill(toMove)} className="ghost" />}
      </g>

      <g>
        {board.map((cell, idx) => {
          const { cx, cy } = center(idx);
          const playable = canPlay && cell === 0;
          const title = sugg?.idx === idx ? `${idxToLabel(idx)} - suggestion, score ${scoreLabel(sugg.score)}` : idxToLabel(idx);
          return (
            <rect
              key={idx}
              x={cx - 25}
              y={cy - 25}
              width="50"
              height="50"
              className={playable ? 'hit hit-play' : 'hit'}
              onMouseEnter={() => setHover(idx)}
              onClick={playable ? () => send({ type: 'play', idx }) : undefined}
            >
              <title>{title}</title>
            </rect>
          );
        })}
      </g>

      {sugg && hover === sugg.idx && <SuggestionScore idx={sugg.idx} score={sugg.score} />}
    </svg>
  );
}

// Bulle du score de la suggestion, placee au-dessus de la pierre fantome, ou
// en dessous pres du bord haut.
function SuggestionScore({ idx, score }: { idx: number; score: number }) {
  const { cx, cy } = center(idx);
  const text = `score ${scoreLabel(score)}`;
  const width = Math.round(text.length * 11.6 + 28);
  const top = cy - 62 < 4 ? cy + 32 : cy - 62;
  const left = Math.min(VIEW - 4 - width, Math.max(4, cx - width / 2));
  return (
    <g className="no-hit">
      <rect x={left} y={top} width={width} height="34" rx="6" className="sugg-box" />
      <text x={left + width / 2} y={top + 17} className="sugg-text">
        {text}
      </text>
    </g>
  );
}

export const Goban = memo(GobanBoard);
