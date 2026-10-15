import { createContext, useCallback, useContext, useMemo, useState, type ReactNode } from 'react';
import type { Config, Players } from '../protocol';

export interface ViewValue {
  showMoveNumbers: boolean;
  toggleMoveNumbers: () => void;
  seats: Players;
  setSeats: (seats: Players) => void;
  rules: Config;
  setRules: (rules: Config) => void;
  cursor: number | null;
  setCursor: (cursor: number | null) => void;
}

// Regles du sujet, toutes actives, ouverture standard.
const SUBJECT_RULES: Config = { captures: true, doubleThree: true, endgameCapture: true, opening: 'standard' };

const ViewContext = createContext<ViewValue | null>(null);

// Etat propre a l'interface, sans lien avec le moteur : numeros de coups,
// joueurs et regles choisis pour la prochaine partie, partages par les
// commandes et la banniere, et position revue dans l'historique (null :
// partie en direct).
export function ViewProvider({ children }: { children: ReactNode }) {
  const [showMoveNumbers, setShowMoveNumbers] = useState(false);
  const [seats, setSeats] = useState<Players>({ black: 'human', white: 'ai' });
  const [rules, setRules] = useState<Config>(SUBJECT_RULES);
  const [cursor, setCursor] = useState<number | null>(null);
  const toggleMoveNumbers = useCallback(() => setShowMoveNumbers((shown) => !shown), []);
  const value = useMemo(
    () => ({ showMoveNumbers, toggleMoveNumbers, seats, setSeats, rules, setRules, cursor, setCursor }),
    [showMoveNumbers, toggleMoveNumbers, seats, rules, cursor],
  );
  return <ViewContext value={value}>{children}</ViewContext>;
}

// Donne l'etat d'interface ; echoue tot si le provider est absent.
export function useView(): ViewValue {
  const value = useContext(ViewContext);
  if (!value) throw new Error('useView hors de ViewProvider');
  return value;
}
