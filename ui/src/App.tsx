import { useState } from 'react';
import { EngineProvider } from './engine/EngineProvider';
import { ViewProvider } from './view/ViewContext';
import { DemoStates } from './components/DemoStates';
import { TitleBar } from './components/TitleBar';
import { Goban } from './components/Goban';
import { GameOverBanner } from './components/GameOverBanner';
import { Clock } from './components/Clock';
import { StatusBar } from './components/StatusBar';
import { Controls } from './components/Controls';
import { HistoryTimeline, PlaybackControls } from './components/Playback';
import { SearchPanel } from './components/SearchPanel';
import './App.css';

type Tab = 'game' | 'lab';

// Mise en page : plateau au centre, colonne "Partie" et colonne "Labo IA" ;
// sous 1280 px, les deux colonnes passent en onglets sous le chronometre.
function Layout() {
  const [tab, setTab] = useState<Tab>('game');

  return (
    <div className="app">
      <TitleBar />
      <main className="layout">
        <section className="board-col" aria-label="Plateau">
          <Goban />
          <GameOverBanner />
        </section>
        <div className="side">
          <Clock />
          <div className="tabs" role="tablist" aria-label="Panneaux">
            <button className="tab" type="button" role="tab" aria-selected={tab === 'game'} onClick={() => setTab('game')}>
              Partie
            </button>
            <button className="tab" type="button" role="tab" aria-selected={tab === 'lab'} onClick={() => setTab('lab')}>
              Labo IA
            </button>
          </div>
          <div className={tab === 'game' ? 'tab-group' : 'tab-group tab-off'}>
            <StatusBar />
            <Controls />
            <PlaybackControls />
            <HistoryTimeline />
          </div>
          <div className={tab === 'lab' ? 'tab-group' : 'tab-group tab-off'}>
            <SearchPanel />
          </div>
        </div>
      </main>
    </div>
  );
}

// Assemble l'interface. Les enfants sont statiques : le provider peut se
// rendre a chaque tick de progression sans les entrainer.
export function App() {
  return (
    <EngineProvider>
      <ViewProvider>
        <DemoStates>
          <Layout />
        </DemoStates>
      </ViewProvider>
    </EngineProvider>
  );
}
