// Upload through the existing authenticated importFile action. The descriptor
// is a contract file, never page content obtained by a browser-side proxy.
export function urlImportDescriptor(url, mode) {
  try {
    if (/[\x00-\x20\x7f\\]/.test(url)) return null;
    const value = new URL(url);
    if (!["http:", "https:"].includes(value.protocol) || value.username || value.password) return null;
    return JSON.stringify({ schema: "loom.url_import_request/1", url, mode });
  } catch { return null; }
}
export function urlImportFile(url, mode) {
  const descriptor = urlImportDescriptor(url, mode);
  return descriptor === null ? null : new File([descriptor], "source.loomurl", { type: "application/json" });
}
