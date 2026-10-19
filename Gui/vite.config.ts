import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// The UI is embedded in the plugin as a zip and served from JUCE's resource
// provider (https://juce.backend/). Everything must resolve relative to the
// page root, and asset file names are fixed (not hashed) so the zip layout is
// deterministic for the CMake step that packages it.
export default defineConfig({
  base: './',
  plugins: [react()],
  build: {
    outDir: 'dist',
    assetsDir: 'assets',
    emptyOutDir: true,
    sourcemap: false,
    rollupOptions: {
      output: {
        entryFileNames: 'assets/app.js',
        chunkFileNames: 'assets/[name].js',
        // Only the entry stylesheet exists; pin its name so the zip layout is
        // always dist/index.html + assets/app.js + assets/app.css.
        assetFileNames: (assetInfo) =>
          assetInfo.names?.some((n) => n.endsWith('.css')) ? 'assets/app.css' : 'assets/[name][extname]',
      },
    },
  },
  server: {
    port: 5173,
    strictPort: true,
  },
});
