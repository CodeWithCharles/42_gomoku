import { idxToLabel } from '../protocol';
import { useSearch } from '../engine/SearchContext';
import './SearchPanel.css';

// Met en forme un entier avec des espaces fines comme separateur de milliers.
function count(n: number): string {
  return n.toLocaleString('fr-FR');
}

// Panneau de debug de la recherche, alimente par les evenements progress.
export function SearchPanel() {
  const { depth, score, nodes, leaves, cutoffs, ttHits, elapsedMs, pv, searching } = useSearch();
  const nps = elapsedMs > 0 ? Math.round((nodes / elapsedMs) * 1000) : 0;

  return (
    <section className="search">
      <h2>Recherche {searching && <span className="search-live">en cours</span>}</h2>
      <dl>
        <dt>profondeur</dt>
        <dd>{depth}</dd>
        <dt>score</dt>
        <dd>{score}</dd>
        <dt>noeuds</dt>
        <dd>{count(nodes)}</dd>
        <dt>noeuds/s</dt>
        <dd>{count(nps)}</dd>
        <dt>feuilles</dt>
        <dd>{count(leaves)}</dd>
        <dt>coupures</dt>
        <dd>{count(cutoffs)}</dd>
        <dt>table de transposition</dt>
        <dd>{count(ttHits)}</dd>
      </dl>
      <p className="search-pv">{pv.length > 0 ? pv.map(idxToLabel).join(' ') : 'pas de variante'}</p>
    </section>
  );
}
