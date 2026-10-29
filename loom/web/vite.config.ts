import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// loom-server serves loom/web/dist statically and answers /api/* itself,
// so in dev we proxy /api to it (default host:port match server/src/main.cpp).
export default defineConfig({
  base: "./",
  plugins: [react()],
  server: {
    proxy: {
      "/api": {
        target: process.env.LOOM_SERVER_URL ?? "http://127.0.0.1:8787",
        changeOrigin: true,
      },
    },
  },
  // PH29 P1: the SQLite-WASM worker is an ES module (the library finds sqlite3.wasm through import.meta.url); the
  // package is served from the app's own files (no CDN) and is not pre-bundled in dev (its README's advice).
  worker: { format: "es" },
  optimizeDeps: { exclude: ["@sqlite.org/sqlite-wasm"] },
  build: {
    outDir: "dist",
    sourcemap: true,
  },
});
