import { useState, type FormEvent } from 'react';
import type { Limits } from '../protocol';
import { useGame } from '../engine/GameContext';
import './SearchSettings.css';

const DEPTH_RANGE = { min: 1, max: 64 };
const BUDGET_RANGE = { min: 10, max: 60000 };

// Ramene une saisie dans les bornes acceptees par le moteur.
function clamp(value: number, range: { min: number; max: number }): number {
  if (!Number.isFinite(value)) return range.min;
  return Math.min(range.max, Math.max(range.min, Math.round(value)));
}

// Formulaire des limites ; remonte a chaque annonce de limites du moteur
// (cle limitsSync) pour toujours partir des valeurs reellement appliquees.
function LimitsForm({ limits, offline, apply }: { limits: Limits; offline: boolean; apply: (limits: Limits) => void }) {
  const [draft, setDraft] = useState(limits);

  const submit = (e: FormEvent) => {
    e.preventDefault();
    const bounded = {
      ...draft,
      maxDepth: clamp(draft.maxDepth, DEPTH_RANGE),
      budgetMs: clamp(draft.budgetMs, BUDGET_RANGE),
    };
    setDraft(bounded);
    apply(bounded);
  };

  return (
    <form className="settings" onSubmit={submit}>
      <h3 className="panel-title">Réglages de la recherche</h3>
      <label className="field">
        profondeur max
        <input
          type="number"
          min={DEPTH_RANGE.min}
          max={DEPTH_RANGE.max}
          step={1}
          value={draft.maxDepth}
          disabled={offline}
          onChange={(e) => setDraft({ ...draft, maxDepth: Number(e.target.value) })}
        />
      </label>
      <label className="field">
        budget moyen (ms)
        <input
          type="number"
          min={BUDGET_RANGE.min}
          max={BUDGET_RANGE.max}
          step={10}
          value={draft.budgetMs}
          disabled={offline}
          onChange={(e) => setDraft({ ...draft, budgetMs: Number(e.target.value) })}
        />
      </label>
      <button className="btn btn-center" type="submit" disabled={offline}>
        Appliquer
      </button>
    </form>
  );
}

// Reglages de la recherche envoyes au moteur par la commande limits.
export function SearchSettings() {
  const { send, limits, limitsSync, connection, engineDied } = useGame();
  return (
    <LimitsForm
      key={limitsSync}
      limits={limits}
      offline={connection !== 'open' || engineDied}
      apply={(next) => send({ type: 'limits', ...next })}
    />
  );
}
