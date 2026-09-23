import { useState } from 'react';
import { useGame } from '../engine/GameContext';
import { useSearch } from '../engine/SearchContext';
import './Controls.css';

// Boutons de partie et reglages de la recherche.
export function Controls() {
  const { send, limits, history, status, connection } = useGame();
  const { searching } = useSearch();
  const [draft, setDraft] = useState(limits);
  const offline = connection !== 'open';

  return (
    <section className="controls">
      <div className="controls-row">
        <button type="button" onClick={() => send({ type: 'new-game' })} disabled={offline}>
          Nouvelle partie
        </button>
        <button
          type="button"
          onClick={() => send({ type: 'undo' })}
          disabled={offline || history.length === 0}
        >
          Annuler
        </button>
        <button
          type="button"
          onClick={() => send({ type: 'suggest' })}
          disabled={offline || searching || status !== 'ongoing'}
        >
          Suggerer
        </button>
        <button type="button" onClick={() => send({ type: 'stop' })} disabled={offline || !searching}>
          Stop
        </button>
      </div>
      <div className="controls-row controls-limits">
        <label>
          profondeur max
          <input
            type="number"
            min={1}
            max={20}
            value={draft.maxDepth}
            onChange={(e) => setDraft({ ...draft, maxDepth: Number(e.target.value) })}
          />
        </label>
        <label>
          budget (ms)
          <input
            type="number"
            min={50}
            max={10000}
            step={50}
            value={draft.budgetMs}
            onChange={(e) => setDraft({ ...draft, budgetMs: Number(e.target.value) })}
          />
        </label>
        <label>
          candidats
          <input
            type="number"
            min={1}
            max={100}
            value={draft.maxCandidates}
            onChange={(e) => setDraft({ ...draft, maxCandidates: Number(e.target.value) })}
          />
        </label>
        <button type="button" onClick={() => send({ type: 'limits', ...draft })} disabled={offline}>
          Appliquer
        </button>
      </div>
    </section>
  );
}
