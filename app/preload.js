const { contextBridge, ipcRenderer } = require('electron');

// Canal 3 : surface nommee et etroite. On n'expose jamais ipcRenderer entier,
// sinon n'importe quel script de la page pourrait parler au main process.
contextBridge.exposeInMainWorld('gomokuShell', {
  onEngineDied: (cb) => ipcRenderer.on('engine:died', (_e, info) => cb(info)),
  minimize: () => ipcRenderer.send('window:minimize'),
  toggleMaximize: () => ipcRenderer.send('window:toggle-maximize'),
  close: () => ipcRenderer.send('window:close'),
  onMaximizedChange: (cb) => {
    const listener = (_e, maximized) => cb(maximized);
    ipcRenderer.on('window:maximized', listener);
    return () => ipcRenderer.removeListener('window:maximized', listener);
  },
});
