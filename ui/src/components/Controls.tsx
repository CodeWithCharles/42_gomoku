import type { Color, PlayerKind } from '../protocol';
import { useGame } from '../engine/GameContext';
import { useSearch } from '../engine/SearchContext';
import { useView } from '../view/ViewContext';
import { useShortcuts } from '../useShortcuts';
import './Controls.css';

// Choix humain/IA d'une couleur, en boutons segmentes.
function SeatPicker({ color, value, onChange }: { color: Color; value: PlayerKind; onChange: (kind: PlayerKind) => void }) {
  const name = color === 'black' ? 'Noir' : 'Blanc';
  return (
    <div className="player-row">
      <span className="player-name">
        <span className={color === 'black' ? 'mini-stone ms-b' : 'mini-stone ms-w'} />
        {name}
      </span>
      <div className="segmented" role="group" aria-label={name}>
        <button className="seg-btn" type="button" aria-pressed={value === 'human'} onClick={() => onChange('human')}>
          humain
        </button>
        <button className="seg-btn" type="button" aria-pressed={value === 'ai'} onClick={() => onChange('ai')}>
          IA
        </button>
      </div>
    </div>
  );
}

// Bouton d'action qui affiche son raccourci clavier.
function ActionButton({ label, shortcut, title, primary, disabled, onClick }: {
  label: string;
  shortcut: string;
  title: string;
  primary?: boolean;
  disabled: boolean;
  onClick: () => void;
}) {
  return (
    <button
      className={primary ? 'btn btn-primary' : 'btn'}
      type="button"
      title={`${title} (raccourci ${shortcut})`}
      disabled={disabled}
      onClick={onClick}
    >
      <span>{label}</span>
      <kbd>{shortcut}</kbd>
    </button>
  );
}

// Choix des joueurs et boutons de partie, avec leurs raccourcis clavier :
// N, U, H, Echap, et M pour les numeros de coups.
export function Controls() {
  const { send, history, status, toMove, players, connection, engineDied } = useGame();
  const { searching } = useSearch();
  const { toggleMoveNumbers, seats, setSeats } = useView();
  const offline = connection !== 'open' || engineDied;
  const aiToMove = status === 'ongoing' && players[toMove] === 'ai';
  const aiOnly = players.black === 'ai' && players.white === 'ai';
  const canUndo = !offline && !aiOnly && history.length > 0;
  const canSuggest = !offline && !searching && status === 'ongoing';
  const canStop = !offline && (searching || aiToMove);
  const draftDiffers = seats.black !== players.black || seats.white !== players.white;
  const newGame = () => send({ type: 'new-game', players: seats });
  const undo = () => send({ type: 'undo' });
  const suggest = () => send({ type: 'suggest' });
  const stop = () => send({ type: 'stop' });

  useShortcuts([
    { key: 'n', enabled: !offline, run: newGame },
    { key: 'u', enabled: canUndo, run: undo },
    { key: 'h', enabled: canSuggest, run: suggest },
    { key: 'Escape', enabled: canStop, run: stop },
    { key: 'm', enabled: true, run: toggleMoveNumbers },
  ]);

  return (
    <section className="panel controls" aria-label="Commandes">
      <h2 className="panel-title">Joueurs</h2>
      <div className="players">
        <SeatPicker color="black" value={seats.black} onChange={(black) => setSeats({ ...seats, black })} />
        <SeatPicker color="white" value={seats.white} onChange={(white) => setSeats({ ...seats, white })} />
      </div>
      {draftDiffers && <p className="draft-note">Appliqué au lancement de la prochaine partie.</p>}
      <div className="actions">
        <ActionButton label="Nouvelle partie" shortcut="N" title="Nouvelle partie" primary disabled={offline} onClick={newGame} />
        <ActionButton label="Annuler" shortcut="U" title="Annuler le dernier coup" disabled={!canUndo} onClick={undo} />
        <ActionButton label="Suggérer" shortcut="H" title="Suggérer un coup" disabled={!canSuggest} onClick={suggest} />
        <ActionButton label="Stop" shortcut="Échap" title="Arrêter la recherche" disabled={!canStop} onClick={stop} />
      </div>
    </section>
  );
}
