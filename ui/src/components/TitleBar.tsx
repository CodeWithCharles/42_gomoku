import { useEffect, useState } from 'react';
import { closeWindow, inElectron, minimizeWindow, onMaximizedChange, toggleMaximizeWindow } from '../shell';
import './TitleBar.css';

// Barre de titre de la fenetre Electron sans cadre : zone de glissement et
// boutons reduire, agrandir ou restaurer, fermer. Absente dans un navigateur.
export function TitleBar() {
  const [maximized, setMaximized] = useState(false);

  useEffect(() => onMaximizedChange(setMaximized), []);

  if (!inElectron()) return null;
  const maxLabel = maximized ? 'Restaurer' : 'Agrandir';

  return (
    <header className="titlebar">
      <div className="tb-drag">
        <svg className="tb-logo" viewBox="0 0 16 16" aria-hidden="true">
          <circle cx="5.2" cy="8" r="3.6" className="tb-logo-b" />
          <circle cx="10.8" cy="8" r="3.6" className="tb-logo-w" />
        </svg>
        <span className="tb-title">Gomoku</span>
      </div>
      <div className="tb-btns">
        <button className="tb-btn" type="button" aria-label="Réduire" title="Réduire" onClick={minimizeWindow}>
          <svg viewBox="0 0 10 10" aria-hidden="true">
            <path d="M0.5 5.5h9" className="tb-ico" />
          </svg>
        </button>
        <button className="tb-btn" type="button" aria-label={maxLabel} title={maxLabel} onClick={toggleMaximizeWindow}>
          {maximized ? (
            <svg viewBox="0 0 10 10" aria-hidden="true">
              <path d="M2.5 2.5V0.5h7v7h-2" className="tb-ico" />
              <rect x="0.5" y="2.5" width="7" height="7" className="tb-ico" />
            </svg>
          ) : (
            <svg viewBox="0 0 10 10" aria-hidden="true">
              <rect x="0.5" y="0.5" width="9" height="9" className="tb-ico" />
            </svg>
          )}
        </button>
        <button className="tb-btn tb-close" type="button" aria-label="Fermer" title="Fermer" onClick={closeWindow}>
          <svg viewBox="0 0 10 10" aria-hidden="true">
            <path d="M0.5 0.5l9 9M9.5 0.5l-9 9" className="tb-ico" />
          </svg>
        </button>
      </div>
    </header>
  );
}
