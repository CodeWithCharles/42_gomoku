// Canal 3 : tout ce que l'URL ne peut pas dire, c'est-a-dire uniquement la mort
// du moteur. En navigateur ordinaire, window.gomokuShell n'existe pas.

declare global {
  interface Window {
    gomokuShell?: {
      onEngineDied: (cb: (info: { code: number | null; signal: string | null }) => void) => void;
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
