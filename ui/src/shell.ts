// Canal 3 : tout ce que l'URL ne peut pas dire : la mort du moteur et le
// pilotage de la fenetre sans cadre. En navigateur ordinaire, window.gomokuShell
// n'existe pas.

declare global {
  interface Window {
    gomokuShell?: {
      onEngineDied: (cb: (info: { code: number | null; signal: string | null }) => void) => void;
      minimize: () => void;
      toggleMaximize: () => void;
      close: () => void;
      onMaximizedChange: (cb: (maximized: boolean) => void) => () => void;
    };
  }
}

// Indique si la page tourne dans la coque Electron.
export function inElectron(): boolean {
  return typeof window.gomokuShell !== 'undefined';
}

// Enregistre un callback appele quand le moteur meurt ; sans effet hors Electron.
export function onEngineDied(cb: (info: { code: number | null; signal: string | null }) => void): void {
  window.gomokuShell?.onEngineDied(cb);
}

// Reduit la fenetre ; sans effet hors Electron.
export function minimizeWindow(): void {
  window.gomokuShell?.minimize();
}

// Agrandit la fenetre, ou la restaure si elle l'est deja ; sans effet hors Electron.
export function toggleMaximizeWindow(): void {
  window.gomokuShell?.toggleMaximize();
}

// Ferme la fenetre, ce qui quitte l'application ; sans effet hors Electron.
export function closeWindow(): void {
  window.gomokuShell?.close();
}

// Enregistre un callback appele quand la fenetre est agrandie ou restauree, et
// renvoie la fonction de desabonnement ; sans effet hors Electron.
export function onMaximizedChange(cb: (maximized: boolean) => void): () => void {
  return window.gomokuShell?.onMaximizedChange(cb) ?? (() => {});
}
