import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// The roadmap data lives in `public/` rather than in the bundle: the agent that
// maintains it edits JSON on disk, and the viewer polls. Nothing has to rebuild
// for a task to change status.
export default defineConfig({
  // Served from a project page at https://<user>.github.io/Pathfinder/, so every
  // asset URL needs that prefix. Left as '/' for local dev, because a base path
  // that is wrong locally is a blank page nobody can debug.
  base: process.env.GITHUB_ACTIONS ? '/Pathfinder/' : '/',
  plugins: [react()],
  server: { port: 5178, strictPort: true, open: false },
});
