import { useState } from 'react';
import './Fold.css';

const STORAGE_PREFIX = 'gomoku.fold.';

// Lit l'etat plie retenu pour une carte ; `initial` si rien n'est retenu ou si
// le stockage est refuse.
function readFolded(id: string, initial: boolean): boolean {
  try {
    const stored = localStorage.getItem(STORAGE_PREFIX + id);
    return stored === null ? initial : stored === '1';
  } catch {
    return initial;
  }
}

// Etat plie d'une carte, `initial` au premier lancement, puis retenu d'un
// lancement a l'autre quand le stockage le permet.
export function useFolded(id: string, initial = false): [boolean, () => void] {
  const [folded, setFolded] = useState(() => readFolded(id, initial));
  const toggle = () => {
    const next = !folded;
    setFolded(next);
    try {
      localStorage.setItem(STORAGE_PREFIX + id, next ? '1' : '0');
    } catch {
      return;
    }
  };
  return [folded, toggle];
}

// Titre de carte cliquable qui plie ou deplie le contenu qui le suit.
export function FoldTitle({ title, folded, onToggle, className }: { title: string; folded: boolean; onToggle: () => void; className?: string }) {
  return (
    <h2 className={className ? `panel-title ${className}` : 'panel-title'}>
      <button type="button" className="fold" aria-expanded={!folded} onClick={onToggle} title={folded ? 'Déplier' : 'Replier'}>
        <svg className="fold-chevron" viewBox="0 0 10 10" aria-hidden="true">
          <path d="M3 2l4 3-4 3" />
        </svg>
        {title}
      </button>
    </h2>
  );
}
