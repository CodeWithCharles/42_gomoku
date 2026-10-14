import { createContext, useCallback, useContext, useMemo, useState, type ReactNode } from 'react';
import type { Players } from '../protocol';

export interface ViewValue {
  showMoveNumbers: boolean;
  toggleMoveNumbers: () => void;
  seats: Players;
  setSeats: (seats: Players) => void;
}

const ViewContext = createContext<ViewValue | null>(null);

// Etat propre a l'interface, sans lien avec le moteur : numeros de coups et
// joueurs choisis pour la prochaine partie, partages par les commandes et la banniere.
export function ViewProvider({ children }: { children: ReactNode }) {
  const [showMoveNumbers, setShowMoveNumbers] = useState(false);
  const [seats, setSeats] = useState<Players>({ black: 'human', white: 'ai' });
  const toggleMoveNumbers = useCallback(() => setShowMoveNumbers((shown) => !shown), []);
  const value = useMemo(
    () => ({ showMoveNumbers, toggleMoveNumbers, seats, setSeats }),
    [showMoveNumbers, toggleMoveNumbers, seats],
  );
  return <ViewContext value={value}>{children}</ViewContext>;
}

// Donne l'etat d'interface ; echoue tot si le provider est absent.
export function useView(): ViewValue {
  const value = useContext(ViewContext);
  if (!value) throw new Error('useView hors de ViewProvider');
  return value;
}
