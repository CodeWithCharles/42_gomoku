import { EngineProvider } from './engine/EngineProvider';
import { Goban } from './components/Goban';
import { Clock } from './components/Clock';
import { SearchPanel } from './components/SearchPanel';
import { Controls } from './components/Controls';
import { StatusBar } from './components/StatusBar';
import './App.css';

// Assemble l'interface. Les enfants sont statiques : le provider peut se
// rendre a chaque tick de progression sans les entrainer.
export function App() {
  return (
    <EngineProvider>
      <main className="layout">
        <section className="layout-board">
          <Goban />
        </section>
        <aside className="layout-side">
          <StatusBar />
          <Clock />
          <Controls />
          <SearchPanel />
        </aside>
      </main>
    </EngineProvider>
  );
}
