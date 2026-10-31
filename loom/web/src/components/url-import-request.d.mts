export type UrlImportMode = "page" | "share";
export function urlImportDescriptor(url: string, mode: UrlImportMode): string | null;
export function urlImportFile(url: string, mode: UrlImportMode): File | null;
