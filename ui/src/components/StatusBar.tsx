import { useGame } from '../engine/GameContext';
import { FoldTitle, useFolded } from './Fold';
import { openingLabel, winReasonLabel, type Color, type ErrorCode, type PlayerKind } from '../protocol';
import './StatusBar.css';

const PAIRS_TO_WIN = 5;

const ERRORS: Record<ErrorCode, string> = {
  legal: 'Coup légal.',
  out_of_bounds: 'Coup hors du plateau.',
  occupied: 'Intersection déjà occupée.',
  double_three: 'Coup interdit par la règle du double-trois.',
  game_over: 'La partie est terminée.',
  not_your_turn: "Ce n'est pas votre tour.",
  opening_rule: "Coup interdit par la règle d'ouverture.",
  unknown: 'Erreur inconnue du moteur.',
};

const PLAYER_LABEL: Record<PlayerKind, string> = { human: 'humain', ai: 'IA' };

const SIDE_LABEL: Record<Color, string> = { black: 'noirs', white: 'blancs' };

// Libelle et pastille de l'etat de connexion au moteur.
function connectionView(connection: string, engineDied: boolean): { text: string; dot: string } {
  if (engineDied) return { text: 'moteur arrêté', dot: 'dot dot-bad' };
  if (connection === 'open') return { text: 'moteur connecté', dot: 'dot dot-ok' };
  if (connection === 'connecting') return { text: 'connexion...', dot: 'dot dot-wait' };
  return { text: 'déconnecté, nouvelle tentative', dot: 'dot dot-bad' };
}

// Ligne de paires capturees d'une couleur, avec sa jauge en cinq segments.
function PairsRow({ color, pairs }: { color: Color; pairs: number }) {
  return (
    <div className="pairs-row">
      <span className={color === 'black' ? 'mini-stone ms-b' : 'mini-stone ms-w'} />
      <span>{SIDE_LABEL[color]}</span>
      <span className="segs">
        {Array.from({ length: PAIRS_TO_WIN }, (_, k) => (
          <span key={k} className={k < pairs ? (pairs >= PAIRS_TO_WIN ? 'seg seg-win' : 'seg seg-on') : 'seg'} />
        ))}
      </span>
      <span className="pairs-count mono">
        {pairs} / {PAIRS_TO_WIN}
      </span>
    </div>
  );
}

// Panneau d'etat : connexion, trait ou resultat, paires capturees, derniere
// erreur. Plie, il ne garde que la derniere erreur.
export function StatusBar() {
  const [folded, toggleFolded] = useFolded('status');
  const { connection, engineDied, toMove, players, status, winReason, pairs, lastError, opening, config } = useGame();
  const phase = opening.step === 'place'
    ? `ouverture ${openingLabel(config.opening)} - pose des ${SIDE_LABEL[toMove]} par le joueur ${opening.actor === 'black' ? 'Noir' : 'Blanc'} (${PLAYER_LABEL[players[opening.actor]]})`
    : opening.step === 'choose'
      ? `ouverture ${openingLabel(config.opening)} - choix de couleur du joueur ${opening.actor === 'black' ? 'Noir' : 'Blanc'} (${PLAYER_LABEL[players[opening.actor]]})`
      : `partie en cours - ${SIDE_LABEL[toMove]} (${PLAYER_LABEL[players[toMove]]})`;
  const conn = connectionView(connection, engineDied);
  const winner: Color | null = status === 'black_wins' ? 'black' : status === 'white_wins' ? 'white' : null;

  return (
    <section className="panel status" aria-label="État de la partie">
      <FoldTitle title="Partie" folded={folded} onToggle={toggleFolded} />
      {!folded && (
        <>
          <div className="conn">
            <span className={conn.dot} />
            <span>{conn.text}</span>
          </div>
          {status === 'ongoing' ? (
            <div className="turn">
              <span className={toMove === 'black' ? 'mini-stone ms-b' : 'mini-stone ms-w'} />
              <span>{phase}</span>
            </div>
          ) : (
            <div className="turn">
              <span className={winner === 'black' ? 'mini-stone ms-b' : winner === 'white' ? 'mini-stone ms-w' : 'mini-stone ms-none'} />
              <span>{winner ? `les ${SIDE_LABEL[winner]} gagnent` : 'partie nulle'}</span>
              {winReason && <span className="turn-reason">- {winReasonLabel(winReason)}</span>}
            </div>
          )}
          <div className="pairs" aria-label="Paires capturées">
            <PairsRow color="black" pairs={pairs.black} />
            <PairsRow color="white" pairs={pairs.white} />
          </div>
        </>
      )}
      {lastError && (
        <div className="error" role="alert">
          <span className="error-code">{lastError.code}</span>
          <span>{ERRORS[lastError.code] ?? ERRORS.unknown}</span>
        </div>
      )}
    </section>
  );
}
