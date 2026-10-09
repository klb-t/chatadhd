// Existing API/browser tests need loopback servers. No provider network is allowed.
const net = require('node:net');
const { syncBuiltinESMExports } = require('node:module');
const isLocal = h => h === undefined || ['localhost', '127.0.0.1', '::1', '[::1]', '0.0.0.0'].includes(h);
const connect = net.Socket.prototype.connect;
net.Socket.prototype.connect = function (...args) {
  let o = args[0];
  if (Array.isArray(o)) o = o[0];
  const host = o && typeof o === 'object' ? o.host : typeof args[1] === 'string' ? args[1] : undefined;
  if (!isLocal(host)) throw new Error('AUDIT_NON_LOOPBACK_NETWORK_BLOCKED');
  return connect.apply(this, args);
};
const fetch = globalThis.fetch;
globalThis.fetch = (request, init) => {
  const url = new URL(typeof request === 'string' || request instanceof URL ? request : request.url);
  if (!isLocal(url.hostname)) throw new Error('AUDIT_NON_LOOPBACK_NETWORK_BLOCKED');
  return fetch(request, init);
};
syncBuiltinESMExports();
