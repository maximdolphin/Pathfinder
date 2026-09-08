import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// The roadmap data lives in `public/` rather than in the bundle: the agent that
// maintains it edits JSON on disk, and the viewer polls. Nothing has to rebuild
// for a task to change status.
export default defineConfig({
  plugins: [react()],
  server: { port: 5178, strictPort: true, open: false },
});
