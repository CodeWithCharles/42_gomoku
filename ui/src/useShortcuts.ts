import { useEffect, useRef } from 'react';

export interface Shortcut {
  key: string;
  enabled: boolean;
  run: () => void;
}

// Indique si la touche vise un champ de saisie, ou les raccourcis doivent se taire.
function typingIn(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false;
  return target.isContentEditable || ['INPUT', 'SELECT', 'TEXTAREA'].includes(target.tagName);
}

// Ecoute le clavier sur toute la page et declenche le raccourci dont la touche
// (KeyboardEvent.key, sans tenir compte de la casse) correspond, s'il est actif.
// Ignore les combinaisons avec Ctrl, Alt ou Meta et la repetition automatique.
export function useShortcuts(shortcuts: Shortcut[]): void {
  const shortcutsRef = useRef(shortcuts);
  shortcutsRef.current = shortcuts;

  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.repeat || e.ctrlKey || e.altKey || e.metaKey || typingIn(e.target)) return;
      const key = e.key.toLowerCase();
      const match = shortcutsRef.current.find((s) => s.key.toLowerCase() === key);
      if (!match || !match.enabled) return;
      e.preventDefault();
      match.run();
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);
}
