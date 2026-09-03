const http = require('http');
const fs = require('fs');
const path = require('path');

const root = process.argv[2] || process.cwd();
const port = Number(process.argv[3] || 4173);

const types = {
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.wasm': 'application/wasm',
  '.css': 'text/css',
  '.html': 'text/html',
  '.json': 'application/json',
  '.map': 'application/json',
};

http
  .createServer((req, res) => {
    let urlPath = decodeURIComponent((req.url || '/').split('?')[0]);
    if (urlPath === '/') {
      urlPath = '/index.html';
    }
    const file = path.join(root, urlPath);
    fs.readFile(file, (err, data) => {
      if (err) {
        res.writeHead(404);
        res.end('not found');
        return;
      }
      const ext = path.extname(file);
      res.writeHead(200, {
        'Content-Type': types[ext] || 'application/octet-stream',
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
      });
      res.end(data);
    });
  })
  .listen(port, () => {
    console.log(`serving ${root} on http://127.0.0.1:${port}`);
  });
