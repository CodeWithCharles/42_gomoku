import type { AnalysisHeaderProps } from './playback.types';
import './AnalysisHeader.css';

interface SourceText {
  label: string;
  detail: string;
}

// Libelle de la pastille et explication de l'origine des statistiques.
function describeSource({ source, position, stats }: AnalysisHeaderProps): SourceText {
  if (source === 'live') {
    return { label: 'En direct', detail: stats ? 'position actuelle, dernière recherche de l\'IA' : 'en attente de la première itération' };
  }
  if (source === 'move') {
    const label = position === null ? 'Position passée' : `Position ${position}`;
    return { label, detail: stats ? "analyse de l'IA au moment de jouer ce coup" : 'aucune analyse enregistrée pour ce coup' };
  }
  return { label: 'Aucune analyse', detail: 'pas de recherche pour cette position' };
}

// En-tete du panneau de recherche : statistiques en direct ou enregistrees, et
// legende qui distingue les bornes du score exact.
export function AnalysisHeader(props: AnalysisHeaderProps) {
  const text = describeSource(props);
  const showLegend = props.source !== 'none' && props.stats !== null;

  return (
    <header className="analysis-header" data-source={props.source}>
      <div className="analysis-header__row">
        <span className="analysis-header__chip">
          <span className="analysis-header__dot" aria-hidden="true" />
          {text.label}
        </span>
        <span className="analysis-header__detail">{text.detail}</span>
      </div>
      {showLegend && (
        <ul className="analysis-header__legend" aria-label="Lecture de la carte de chaleur">
          <li className="analysis-header__item">
            <span className="analysis-header__swatch analysis-header__swatch--bound" aria-hidden="true" />
            <span>
              <strong>≤ borne</strong> : carte de chaleur, le score réel peut être plus bas
            </span>
          </li>
          <li className="analysis-header__item">
            <span className="analysis-header__swatch analysis-header__swatch--exact" aria-hidden="true" />
            <span>
              <strong>= exact</strong> : meilleur coup seulement
            </span>
          </li>
        </ul>
      )}
    </header>
  );
}
