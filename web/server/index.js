import cors from 'cors';
import express from 'express';
import { execFile } from 'node:child_process';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const webRoot = path.resolve(__dirname, '..');
const repoRoot = path.resolve(webRoot, '..');
const cliName = process.platform === 'win32' ? 'echoir.exe' : 'echoir';
const cliPath = process.env.ECHOIR_CLI || path.join(repoRoot, 'build', cliName);
const codeDir = process.env.ECHOIR_CODE_DIR || path.join(webRoot, 'codes');
const serverPort = Number.parseInt(process.env.ECHOIR_WEB_PORT || '8787', 10);

function detectSerialPorts() {
  if (process.platform === 'win32') {
    return Array.from({ length: 32 }, (_value, index) => `COM${index + 1}`);
  }

  const ports = [];
  const seen = new Set();
  const add = (value) => {
    if (!seen.has(value)) {
      seen.add(value);
      ports.push(value);
    }
  };

  const byId = '/dev/serial/by-id';
  if (fs.existsSync(byId)) {
    for (const entry of fs.readdirSync(byId).sort()) {
      add(path.join(byId, entry));
    }
  }

  if (fs.existsSync('/dev')) {
    for (const entry of fs.readdirSync('/dev').sort()) {
      if (entry.startsWith('ttyUSB') || entry.startsWith('ttyACM')) {
        add(path.join('/dev', entry));
      }
    }
  }
  return ports;
}

const detectedSerialPorts = detectSerialPorts();
const defaultSerialPort = process.env.ECHOIR_SERIAL_PORT ||
  detectedSerialPorts[0] ||
  (process.platform === 'win32' ? 'COM3' : '/dev/ttyUSB0');

await fsp.mkdir(codeDir, { recursive: true });

const app = express();
app.use(express.json({ limit: '1mb' }));
if (process.env.NODE_ENV !== 'production') {
  app.use(cors({ origin: true }));
}

function assertCliExists() {
  if (!fs.existsSync(cliPath)) {
    const error = new Error(`echoir CLI not found at ${cliPath}`);
    error.status = 500;
    throw error;
  }
}

function cleanToken(value, name, pattern = /^[\w./:\\-]+$/) {
  const text = String(value ?? '').trim();
  if (!text || !pattern.test(text)) {
    throw new Error(`${name} is invalid`);
  }
  return text;
}

function cleanName(value, fallback = 'learned') {
  const text = String(value ?? '').trim();
  if (!text) {
    return fallback;
  }
  return text.slice(0, 80);
}

function cleanNumber(value, name, min, max) {
  const number = Number.parseInt(String(value), 10);
  if (!Number.isFinite(number) || number < min || number > max) {
    throw new Error(`${name} must be ${min}..${max}`);
  }
  return String(number);
}

function cleanHexByte(value, name) {
  const text = String(value ?? '').trim();
  if (!/^(0x)?[0-9a-fA-F]{1,2}$/.test(text)) {
    throw new Error(`${name} must be one byte of hex`);
  }
  return text.startsWith('0x') || text.startsWith('0X') ? text : text.toUpperCase();
}

function commonArgs(body = {}) {
  const args = [];
  const port = body.port || defaultSerialPort;
  if (port) {
    args.push('--port', cleanToken(port, 'port'));
  }
  if (body.baud) {
    args.push('--baud', cleanNumber(body.baud, 'baud', 1200, 921600));
  }
  if (body.address) {
    args.push('--address', cleanHexByte(body.address, 'address'));
  }
  if (body.profile) {
    const profile = cleanToken(body.profile, 'profile', /^(basic|high)$/);
    args.push('--profile', profile);
  }
  return args;
}

function boolArg(value) {
  return value ? '1' : '0';
}

function pushIfPresent(args, flag, value, name = flag) {
  if (value === undefined || value === null || value === '') {
    return;
  }
  args.push(flag, cleanToken(value, name, /^[\w.+,= -]+$/));
}

function runEchoir(args, timeoutMs = 30000) {
  assertCliExists();
  return new Promise((resolve, reject) => {
    execFile(cliPath, args, {
      cwd: repoRoot,
      timeout: timeoutMs,
      windowsHide: true,
      maxBuffer: 2 * 1024 * 1024
    }, (error, stdout, stderr) => {
      const result = {
        command: ['echoir', ...args].join(' '),
        stdout: stdout.trim(),
        stderr: stderr.trim(),
        exitCode: error && typeof error.code === 'number' ? error.code : 0,
        timedOut: Boolean(error?.killed)
      };
      if (error) {
        const wrapped = new Error(result.stderr || result.stdout || error.message);
        wrapped.status = result.timedOut ? 504 : 400;
        wrapped.payload = result;
        reject(wrapped);
        return;
      }
      resolve(result);
    });
  });
}

function acArgs(body, dryRun) {
  const args = ['ac', ...commonArgs(body)];
  if (body.brandCode) {
    args.push('--brand-code', cleanHexByte(body.brandCode, 'brandCode'));
  } else {
    pushIfPresent(args, '--brand', body.brand || 'gree', 'brand');
  }
  pushIfPresent(args, '--power', body.power || 'on', 'power');
  pushIfPresent(args, '--mode', body.mode || 'cool', 'mode');
  args.push('--temp', cleanNumber(body.temp ?? 24, 'temp', 16, 30));
  pushIfPresent(args, '--fan', body.fan || 'auto', 'fan');
  if (body.waitAck) {
    args.push('--wait-ack');
  }
  if (dryRun) {
    args.push('--dry-run');
  }
  return args;
}

function officialArgs(body, dryRun) {
  const args = ['ac-official', ...commonArgs(body)];
  pushIfPresent(args, '--protocol', body.protocol || 'gree3', 'protocol');
  pushIfPresent(args, '--power', body.power, 'power');
  pushIfPresent(args, '--mode', body.mode, 'mode');
  if (body.temp !== undefined && body.temp !== null && body.temp !== '') {
    args.push('--temp', cleanNumber(body.temp, 'temp', 16, 30));
  }
  pushIfPresent(args, '--fan', body.fan, 'fan');
  if (body.swingIndex !== undefined && body.swingIndex !== null && body.swingIndex !== '') {
    args.push('--swing-index', cleanNumber(body.swingIndex, 'swingIndex', 0, 16));
  }
  if (body.timerIndex !== undefined && body.timerIndex !== null && body.timerIndex !== '') {
    args.push('--timer-index', cleanNumber(body.timerIndex, 'timerIndex', 0, 7));
  }
  pushIfPresent(args, '--fields', body.fields, 'fields');
  if (body.waitAck) {
    args.push('--wait-ack');
  }
  if (dryRun) {
    args.push('--dry-run');
  }
  return args;
}

function safeCodePath(file) {
  const base = path.basename(String(file ?? ''));
  if (!/^[\w.-]+\.json$/.test(base)) {
    throw new Error('code file is invalid');
  }
  return path.join(codeDir, base);
}

function codeFileForName(name) {
  const safe = cleanName(name).replace(/[^\p{L}\p{N}_.-]+/gu, '_').replace(/^_+|_+$/g, '') || 'learned';
  return path.join(codeDir, `${safe.slice(0, 64)}.json`);
}

function route(handler) {
  return async (req, res, next) => {
    try {
      res.json({ ok: true, ...(await handler(req)) });
    } catch (error) {
      next(error);
    }
  };
}

app.get('/api/health', route(async () => {
  const protocols = await runEchoir(['ac-official', '--list-protocols'], 5000);
  return {
    cliPath,
    serialPort: defaultSerialPort,
    detectedPorts: detectedSerialPorts,
    protocols: protocols.stdout.split(',').map((item) => item.trim()).filter(Boolean)
  };
}));

app.get('/api/brands', route(async () => {
  const text = await fsp.readFile(path.join(repoRoot, 'data', 'ac_brands.json'), 'utf8');
  return { brands: JSON.parse(text).brands };
}));

app.post('/api/device/info', route(async (req) => {
  return { result: await runEchoir(['info', ...commonArgs(req.body)], 8000) };
}));

app.post('/api/device/reset', route(async (req) => {
  return { result: await runEchoir(['reset', ...commonArgs(req.body)], 8000) };
}));

app.post('/api/ac/dry-run', route(async (req) => {
  return { result: await runEchoir(acArgs(req.body, true), 8000) };
}));

app.post('/api/ac/send', route(async (req) => {
  return { result: await runEchoir(acArgs(req.body, false), 15000) };
}));

app.post('/api/ac-official/dry-run', route(async (req) => {
  return { result: await runEchoir(officialArgs(req.body, true), 8000) };
}));

app.post('/api/ac-official/send', route(async (req) => {
  return { result: await runEchoir(officialArgs(req.body, false), 15000) };
}));

app.post('/api/internal/learn', route(async (req) => {
  const args = [
    'learn-internal',
    ...commonArgs(req.body),
    '--slot',
    cleanNumber(req.body.slot ?? 0, 'slot', 0, 95),
    '--timeout-ms',
    cleanNumber(req.body.timeoutMs ?? 15000, 'timeoutMs', 1000, 120000)
  ];
  return { result: await runEchoir(args, Number(req.body.timeoutMs ?? 15000) + 5000) };
}));

app.post('/api/internal/send', route(async (req) => {
  const args = [
    'send-internal',
    ...commonArgs(req.body),
    '--slot',
    cleanNumber(req.body.slot ?? 0, 'slot', 0, 95)
  ];
  return { result: await runEchoir(args, 10000) };
}));

app.post('/api/external/learn', route(async (req) => {
  const name = cleanName(req.body.name, 'learned');
  const file = codeFileForName(name);
  const timeoutMs = Number.parseInt(req.body.timeoutMs ?? 15000, 10);
  const args = [
    'learn-external',
    ...commonArgs(req.body),
    '--name',
    name,
    '--out',
    file,
    '--timeout-ms',
    cleanNumber(timeoutMs, 'timeoutMs', 1000, 120000)
  ];
  return { file: path.basename(file), result: await runEchoir(args, timeoutMs + 5000) };
}));

app.post('/api/external/send', route(async (req) => {
  const args = ['send-external', ...commonArgs(req.body), '--in', safeCodePath(req.body.file)];
  return { result: await runEchoir(args, 15000) };
}));

app.post('/api/external/dump', route(async (req) => {
  const args = ['dump', '--in', safeCodePath(req.body.file)];
  return { result: await runEchoir(args, 8000) };
}));

app.get('/api/codes', route(async () => {
  const entries = await fsp.readdir(codeDir, { withFileTypes: true });
  return {
    codes: entries
      .filter((entry) => entry.isFile() && entry.name.endsWith('.json'))
      .map((entry) => entry.name)
      .sort()
  };
}));

if (process.env.NODE_ENV === 'production') {
  app.use(express.static(path.join(webRoot, 'dist')));
  app.get('*', (_req, res) => {
    res.sendFile(path.join(webRoot, 'dist', 'index.html'));
  });
}

app.use((error, _req, res, _next) => {
  res.status(error.status || 500).json({
    ok: false,
    error: error.message,
    result: error.payload
  });
});

app.listen(serverPort, '0.0.0.0', () => {
  console.log(`EchoIR API listening on http://127.0.0.1:${serverPort}`);
  console.log(`Using CLI: ${cliPath}`);
});
