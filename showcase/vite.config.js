import { defineConfig } from "vite";

// Relative Pfade: laeuft unter jedem Unterpfad (GitHub Pages: /ESPHomeRemote/showcase/) und lokal.
export default defineConfig({
  base: "./",
  build: {
    outDir: "dist",
    assetsInlineLimit: 0,
    chunkSizeWarningLimit: 900,
  },
  server: { host: "127.0.0.1", port: 5180 },
});
