'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');

const web =
  process.env.AETHER_BPP_WEB_ROOT ||
  path.resolve(
    __dirname,
    '../../../../build/emscripten-browser/examples/browser_ping_pong/web',
  );
const port = Number(process.env.AETHER_BPP_PORT || 4173);

const types = {
  '.html': 'text/html',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.wasm': 'application/wasm',
  '.css': 'text/css',
  '.json': 'application/json',
  '.map': 'application/json',
};

const server = http.createServer((req, res) => {
  let p = decodeURIComponent((req.url || '/').split('?')[0]);
  if (p === '/') {
    p = '/index.html';
  }
  const f = path.join(web, p);
  if (!f.startsWith(web)) {
    res.writeHead(403);
    res.end('forbidden');
    return;
  }
  fs.readFile(f, (err, data) => {
    if (err) {
      res.writeHead(404);
      res.end('not found');
      return;
    }
    const ext = path.extname(f);
    res.writeHead(200, {
      'Content-Type': types[ext] || 'application/octet-stream',
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    });
    res.end(data);
  });
});

server.listen(port, '127.0.0.1', () => {
  console.log(`static server ${web} on http://127.0.0.1:${port}`);
});
