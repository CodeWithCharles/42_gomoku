import { openingLabel, type Color, type Config, type OpeningOption, type OpeningRule, type PlayerKind } from '../protocol';
import { useGame } from '../engine/GameContext';
import { useSearch } from '../engine/SearchContext';
import { useView } from '../view/ViewContext';
import { useShortcuts } from '../useShortcuts';
import { FoldTitle, useFolded } from './Fold';
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

const OPENINGS: OpeningRule[] = ['standard', 'pro', 'longpro', 'swap', 'swap2'];

const OPENING_HINT: Record<OpeningRule, string> = {
  standard: 'Noir commence, sans contrainte.',
  pro: 'Noir ouvre au centre ; sa 2e pierre à 3 intersections au moins.',
  longpro: 'Noir ouvre au centre ; sa 2e pierre à 4 intersections au moins.',
  swap: 'Noir pose 3 pierres, Blanc choisit sa couleur.',
  swap2: 'Noir pose 3 pierres ; Blanc choisit sa couleur ou en pose 2 de plus.',
};

const OPTION_LABEL: Record<OpeningOption, string> = {
  black: 'Jouer Noir',
  white: 'Jouer Blanc',
  place2: 'Poser 2 pierres',
};

// Regles de la prochaine partie : ouverture et regles du sujet desactivables.
function RulesPicker({ rules, onChange }: { rules: Config; onChange: (rules: Config) => void }) {
  const toggles: { key: 'captures' | 'doubleThree' | 'endgameCapture'; label: string }[] = [
    { key: 'captures', label: 'Captures' },
    { key: 'doubleThree', label: 'Double-trois interdit' },
    { key: 'endgameCapture', label: 'Capture en fin de partie' },
  ];
  return (
    <div className="rules">
      <label className="field rules-opening" htmlFor="rules-opening">
        Ouverture
        <select
          id="rules-opening"
          value={rules.opening}
          onChange={(e) => onChange({ ...rules, opening: e.target.value as OpeningRule })}
        >
          {OPENINGS.map((rule) => (
            <option key={rule} value={rule}>{openingLabel(rule)}</option>
          ))}
        </select>
      </label>
      <p className="rules-hint">{OPENING_HINT[rules.opening]}</p>
      {toggles.map(({ key, label }) => (
        <label key={key} className="switch rules-switch">
          <input
            id={`rules-${key}`}
            type="checkbox"
            checked={rules[key]}
            disabled={key === 'endgameCapture' && !rules.captures}
            onChange={() => onChange({ ...rules, [key]: !rules[key] })}
          />
          {label}
        </label>
      ))}
    </div>
  );
}

// Choix de couleur demande par Swap et Swap2, quand il revient a un humain.
function OpeningChoice({ actor, options, onChoose }: { actor: Color; options: OpeningOption[]; onChoose: (option: OpeningOption) => void }) {
  return (
    <div className="opening-choice" role="group" aria-label="Choix de couleur">
      <p>
        <span className={actor === 'black' ? 'mini-stone ms-b' : 'mini-stone ms-w'} />
        {actor === 'black' ? 'Le joueur Noir' : 'Le joueur Blanc'} choisit sa couleur.
      </p>
      <div className="opening-buttons">
        {options.map((option) => (
          <button key={option} className="btn" type="button" onClick={() => onChoose(option)}>
            {OPTION_LABEL[option]}
          </button>
        ))}
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

// Choix des joueurs, boutons de partie et regles, avec leurs raccourcis
// clavier : N, U, H, Echap, et M pour les numeros de coups. Les sections
// Joueurs et Regles se replient ; le choix de couleur reste toujours visible.
export function Controls() {
  const { send, history, status, players, opening, connection, engineDied } = useGame();
  const { searching } = useSearch();
  const { showMoveNumbers, toggleMoveNumbers, seats, setSeats, rules, setRules } = useView();
  const [playersFolded, togglePlayersFolded] = useFolded('players');
  const [rulesFolded, toggleRulesFolded] = useFolded('rules', true);
  const offline = connection !== 'open' || engineDied;
  const aiToMove = status === 'ongoing' && players[opening.actor] === 'ai';
  const humanChoice = !offline && status === 'ongoing' && opening.step === 'choose' && players[opening.actor] === 'human';
  const aiOnly = players.black === 'ai' && players.white === 'ai';
  const canUndo = !offline && !aiOnly && history.length > 0;
  const canSuggest = !offline && !searching && status === 'ongoing';
  const canStop = !offline && (searching || aiToMove);
  const draftDiffers = seats.black !== players.black || seats.white !== players.white;
  const newGame = () => send({ type: 'new-game', players: seats, config: rules });
  const choose = (color: OpeningOption) => send({ type: 'choose', color });
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
      <FoldTitle title="Joueurs" folded={playersFolded} onToggle={togglePlayersFolded} />
      {!playersFolded && (
        <>
          <div className="players">
            <SeatPicker color="black" value={seats.black} onChange={(black) => setSeats({ ...seats, black })} />
            <SeatPicker color="white" value={seats.white} onChange={(white) => setSeats({ ...seats, white })} />
          </div>
          {draftDiffers && <p className="draft-note">Appliqué au lancement de la prochaine partie.</p>}
        </>
      )}
      {humanChoice && <OpeningChoice actor={opening.actor} options={opening.options} onChoose={choose} />}
      {!playersFolded && (
        <>
          <div className="actions">
            <ActionButton label="Nouvelle partie" shortcut="N" title="Nouvelle partie" primary disabled={offline} onClick={newGame} />
            <ActionButton label="Annuler" shortcut="U" title="Annuler le dernier coup" disabled={!canUndo} onClick={undo} />
            <ActionButton label="Suggérer" shortcut="H" title="Suggérer un coup" disabled={!canSuggest} onClick={suggest} />
            <ActionButton label="Stop" shortcut="Échap" title="Arrêter la recherche" disabled={!canStop} onClick={stop} />
          </div>
          <label className="switch rules-switch" title="Afficher les numéros de coups (raccourci M)">
            <input type="checkbox" checked={showMoveNumbers} onChange={toggleMoveNumbers} />
            Numéros de coups<kbd>M</kbd>
          </label>
        </>
      )}
      <FoldTitle title="Règles" folded={rulesFolded} onToggle={toggleRulesFolded} className="rules-title" />
      {!rulesFolded && <RulesPicker rules={rules} onChange={setRules} />}
    </section>
  );
}
