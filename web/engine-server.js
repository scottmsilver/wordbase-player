const http = require('http');
const { spawn } = require('child_process');
const path = require('path');

const PORT = process.env.ENGINE_PORT || 3001;
const ENGINE_PATH = path.resolve(__dirname, '../build/wordbase-server');
const DICT_PATH = path.resolve(__dirname, '../src/twl06_with_wordbase_additions.txt');

console.log('Starting engine:', ENGINE_PATH);
console.log('Dictionary:', DICT_PATH);

const engine = spawn(ENGINE_PATH, [DICT_PATH], {
  stdio: ['pipe', 'pipe', 'inherit'],
});

engine.on('error', (err) => {
  console.error('Failed to start engine:', err.message);
  process.exit(1);
});

engine.on('exit', (code) => {
  console.error('Engine exited with code', code);
  process.exit(1);
});

// Line-buffered stdout reader
let stdoutBuffer = '';
let pendingResolve = null;

engine.stdout.on('data', (data) => {
  stdoutBuffer += data.toString();
  const lines = stdoutBuffer.split('\n');
  stdoutBuffer = lines.pop(); // keep incomplete line in buffer
  for (const line of lines) {
    if (!line.trim()) continue;
    try {
      const parsed = JSON.parse(line);
      if (parsed.status === 'ready') {
        console.log('Engine ready');
        startServer();
        continue;
      }
      if (pendingResolve) {
        const resolve = pendingResolve;
        pendingResolve = null;
        resolve(parsed);
      }
    } catch (e) {
      console.error('Failed to parse engine output:', line);
    }
  }
});

function sendRequest(request) {
  return new Promise((resolve, reject) => {
    if (pendingResolve) {
      reject(new Error('Engine busy'));
      return;
    }
    pendingResolve = resolve;
    const timeout = setTimeout(() => {
      if (pendingResolve === resolve) {
        pendingResolve = null;
        reject(new Error('Engine timeout'));
      }
    }, 30000);
    const origResolve = resolve;
    pendingResolve = (result) => {
      clearTimeout(timeout);
      origResolve(result);
    };
    engine.stdin.write(JSON.stringify(request) + '\n');
  });
}

function startServer() {
  const server = http.createServer(async (req, res) => {
    // CORS
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
      res.writeHead(204);
      res.end();
      return;
    }

    if (req.method === 'POST' && req.url === '/api/move') {
      let body = '';
      req.on('data', (chunk) => { body += chunk; });
      req.on('end', async () => {
        try {
          const request = JSON.parse(body);
          const result = await sendRequest(request);
          res.writeHead(200, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify(result));
        } catch (e) {
          res.writeHead(500, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ error: e.message }));
        }
      });
    } else {
      res.writeHead(404);
      res.end('Not Found');
    }
  });

  server.listen(PORT, () => {
    console.log(`Engine HTTP server listening on port ${PORT}`);
  });
}
