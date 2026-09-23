const { contextBridge, ipcRenderer } = require('electron');

// Canal 3 : surface nommee et etroite. On n'expose jamais ipcRenderer entier,
// sinon n'importe quel script de la page pourrait parler au main process.
contextBridge.exposeInMainWorld('gomokuShell', {
  onEngineDied: (cb) => ipcRenderer.on('engine:died', (_e, info) => cb(info)),
});
