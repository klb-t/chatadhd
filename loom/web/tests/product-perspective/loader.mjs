import { createServer } from 'vite';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { createServer as createHttpServer } from 'node:http';
export async function createModuleLoader() {
  const root=fileURLToPath(new URL('../../',import.meta.url));
  // Supply an unbound HMR transport: compile modules without opening a port.
  const transport=createHttpServer();
  const server=await createServer({root,configFile:false,appType:'custom',logLevel:'error',server:{middlewareMode:true,hmr:{server:transport}}});
  return {load:relative=>server.ssrLoadModule(path.resolve(root,relative)),close:()=>server.close()};
}
