import { useEffect, useRef } from 'react';
import type { MoveHistoryProps, TimelineItem } from './playback.types';
import { idxToLabel } from '../protocol';
import { colorName, optionLabel, stoneModifier } from './timelineLabels';
import './MoveHistory.css';

interface RowProps {
  item: TimelineItem;
  position: number;
  underCursor: boolean;
  current: boolean;
  onSelect: (position: number) => void;
}

// Classes d'une ligne : la ligne sous le curseur l'emporte sur la derniere
// ligne du direct.
function rowClass(kind: string, underCursor: boolean, current: boolean): string {
  const state = underCursor ? 'move-history__row--cursor' : current ? 'move-history__row--current' : '';
  return `move-history__row move-history__row--${kind} ${state}`;
}

// Phrase d'un choix de couleur, par exemple "Blanc choisit Noir, places
// echangees".
function choiceText(item: Extract<TimelineItem, { kind: 'choice' }>): string {
  const base = `${colorName(item.actor)} choisit ${optionLabel(item.choice)}`;
  return item.swapped ? `${base}, places échangées` : base;
}

// Pictogramme d'echange pour les choix de couleur.
function IconSwap() {
  return (
    <svg viewBox="0 0 16 16" className="move-history__swap" aria-hidden="true">
      <circle cx="5" cy="5" r="3.2" className="move-history__swap-black" />
      <circle cx="11" cy="11" r="3.2" className="move-history__swap-white" />
      <path d="M10 3.5h3v3M6 12.5H3v-3" className="move-history__swap-arrow" />
    </svg>
  );
}

// Une entree de la chronologie : coup (numero, pierre, coordonnee) ou choix
// de couleur, d'aspect distinct.
function HistoryRow({ item, position, underCursor, current, onSelect }: RowProps) {
  if (item.kind === 'choice') {
    return (
      <li>
        <button
          type="button"
          className={rowClass('choice', underCursor, current)}
          aria-current={underCursor ? 'step' : undefined}
          data-active={underCursor || undefined}
          onClick={() => onSelect(position)}
        >
          <IconSwap />
          <span className="move-history__choice">
            <span className="move-history__kicker">ouverture</span>
            <span>{choiceText(item)}</span>
          </span>
          {item.byAi && <span className="move-history__ai">IA</span>}
        </button>
      </li>
    );
  }
  const capturedLabel = `${item.captured} pierres capturées par ce coup`;
  return (
    <li>
      <button
        type="button"
        className={rowClass('move', underCursor, current)}
        aria-current={underCursor ? 'step' : undefined}
        data-active={underCursor || undefined}
        onClick={() => onSelect(position)}
      >
        <span className="move-history__num">{item.number}</span>
        <span className={`move-history__stone move-history__stone--${stoneModifier(item.player)}`} aria-label={colorName(item.player)} />
        <span className="move-history__coord">{idxToLabel(item.idx)}</span>
        {item.byAi ? <span className="move-history__ai">IA</span> : <span />}
        {item.captured > 0 ? (
          <span className="move-history__cap" title={capturedLabel} aria-label={capturedLabel}>
            +{item.captured}
          </span>
        ) : (
          <span />
        )}
      </button>
    </li>
  );
}

// Chronologie cliquable en lecture seule ; la position p est l'etat apres les
// p premieres entrees, null le direct.
export function MoveHistory({ items, cursor, onSelect }: MoveHistoryProps) {
  const listRef = useRef<HTMLOListElement>(null);
  const moves = items.filter((item) => item.kind === 'move').length;

  useEffect(() => {
    const list = listRef.current;
    if (!list) return;
    const active = list.querySelector<HTMLElement>('[data-active="true"]');
    if (!active) {
      if (cursor === null) list.scrollTop = list.scrollHeight;
      return;
    }
    const frame = list.getBoundingClientRect();
    const row = active.getBoundingClientRect();
    if (row.top < frame.top) list.scrollTop -= frame.top - row.top;
    else if (row.bottom > frame.bottom) list.scrollTop += row.bottom - frame.bottom;
  }, [cursor, items.length]);

  return (
    <section className="move-history" aria-label="Historique des coups">
      <header className="move-history__head">
        <h2 className="move-history__title">
          Historique<span className="move-history__count">{moves}</span>
        </h2>
        <span className="move-history__hint">cliquer pour revoir</span>
      </header>
      {items.length === 0 ? (
        <p className="move-history__empty">Aucun coup joué.</p>
      ) : (
        <ol className="move-history__list" ref={listRef}>
          <li>
            <button
              type="button"
              className={rowClass('start', cursor === 0, false)}
              aria-current={cursor === 0 ? 'step' : undefined}
              data-active={cursor === 0 || undefined}
              onClick={() => onSelect(0)}
            >
              <span className="move-history__num">0</span>
              <span className="move-history__start">position initiale</span>
            </button>
          </li>
          {items.map((item, index) => (
            <HistoryRow
              key={index}
              item={item}
              position={index + 1}
              underCursor={cursor === index + 1}
              current={cursor === null && index === items.length - 1}
              onSelect={onSelect}
            />
          ))}
        </ol>
      )}
    </section>
  );
}
