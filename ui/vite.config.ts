import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// Le proxy relaie /ws vers le moteur : ws: true est obligatoire, sans quoi
// l'en-tete Upgrade n'est pas transmis et la socket echoue en 400.
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/ws': { target: 'ws://localhost:8642', ws: true },
    },
  },
});
