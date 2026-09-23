import { useGame } from '../engine/GameContext';
import './StatusBar.css';

const STATUS_LABEL: Record<string, string> = {
  ongoing: 'partie en cours',
  black_wins: 'les noirs gagnent',
  white_wins: 'les blancs gagnent',
  draw: 'partie nulle',
};

// Bandeau d'etat : connexion, trait, captures, statut et derniere erreur.
export function StatusBar() {
  const { connection, engineDied, toMove, players, status, winReason, pairs, lastError } = useGame();

  return (
    <header className="status">
      <div className="status-line">
        <span className={`status-dot status-${connection}`} />
        <span>
          {engineDied
            ? 'moteur arrete'
            : connection === 'open'
              ? 'moteur connecte'
              : connection === 'connecting'
                ? 'connexion...'
                : 'deconnecte, nouvelle tentative'}
        </span>
      </div>
      <div className="status-line">
        <span className={toMove === 'black' ? 'status-stone status-black' : 'status-stone status-white'} />
        <span>
          {STATUS_LABEL[status]}
          {status === 'ongoing' && ` - ${toMove === 'black' ? 'noirs' : 'blancs'} (${players[toMove]})`}
          {winReason && ` - ${winReasonLabel(winReason)}`}
        </span>
      </div>
      <div className="status-line">
        <span>
          paires capturees : noirs {pairs.black} / blancs {pairs.white}
        </span>
      </div>
      {lastError && <p className="status-error">{lastError.code} : {lastError.message}</p>}
    </header>
  );
}
