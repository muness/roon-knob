const express = require('express');
const morgan = require('morgan');
const cors = require('cors');
const path = require('path');
const { execSync } = require('child_process');
const { advertise } = require('./mdns');
const { createRoutes } = require('./routes');
const { createRoonBridge } = require('./bridge');
const { createMetricsTracker } = require('./metrics');
const { createLogger } = require('./logger');
const { createKnobsStore } = require('./knobs');
// Version: use APP_VERSION env var (set by CI), fall back to 'dev' for local development
// package.json version is stale in repo; CI injects correct version during builds
const VERSION = process.env.APP_VERSION || 'dev';

function getGitSha() {
  try {
    return execSync('git rev-parse --short HEAD', { encoding: 'utf8', cwd: __dirname, stdio: ['pipe', 'pipe', 'ignore'] }).trim();
  } catch {
    return 'unknown';
  }
}

function startServer() {
  const PORT = parseInt(process.env.PORT || '8088', 10);
  const MDNS_NAME = process.env.MDNS_NAME || 'Roon Knob Bridge';
  const LOG_LEVEL = process.env.LOG_LEVEL || 'info';
  // Roon discovery service port (not the HTTP port)
  const ROON_SERVICE_PORT = parseInt(process.env.ROON_SERVICE_PORT || '9330', 10);
  const MDNS_BASE = process.env.MDNS_BASE || `http://${require('os').hostname()}:${PORT}`;
  const GIT_SHA = getGitSha();

  const metrics = createMetricsTracker();
  const log = createLogger('Sidecar');
  log.info('Starting server', { version: VERSION, git_sha: GIT_SHA });
  const app = express();
  app.use(express.json());
  app.use(cors());
  app.use(morgan(LOG_LEVEL === 'debug' ? 'dev' : 'tiny'));

  const bridge = createRoonBridge({ service_port: ROON_SERVICE_PORT, display_name: MDNS_NAME, base_url: MDNS_BASE, log: createLogger('Bridge') });
  bridge.start();

  const knobs = createKnobsStore({ logger: createLogger('Knobs') });

  app.use(createRoutes({ bridge, metrics, logger: createLogger('HTTP'), knobs }));

  app.get('/status', (_req, res) => {
    res.json({ status: 'ok', version: VERSION, git_sha: GIT_SHA });
  });

  // Redirect / to /admin on the bridge (before static so index.html doesn't override)
  app.get('/', (_req, res) => {
    res.redirect('/admin');
  });

  app.use(express.static(path.join(__dirname, 'public')));

  app.use((err, req, res, _next) => {
    log.error('Unhandled request error', { path: req.path, err });
    res.status(500).json({ error: 'internal_error' });
  });

  const server = app.listen(PORT, () => {
    log.info(`Listening on ${PORT}`, { roon_service_port: ROON_SERVICE_PORT, base: MDNS_BASE });
    try {
      advertise(PORT, {
        name: MDNS_NAME,
        base: MDNS_BASE,
        txt: { api: '1' },
      }, log);
    } catch (error) {
      log.error('mDNS advertise failed', { error });
    }
    metrics.mdns = {
      name: MDNS_NAME,
      port: PORT,
      base: MDNS_BASE,
      advertisedAt: Date.now(),
    };
    metrics.git_sha = GIT_SHA;
    metrics.version = VERSION;
  });

  server.on('error', (err) => {
    if (err.code === 'EADDRINUSE') {
      log.error('Port already in use. Stop the other process or set PORT.', { port: PORT });
    } else {
      log.error('HTTP server error', { err });
    }
    process.exit(1);
  });

  process.on('unhandledRejection', (err) => {
    log.error('Unhandled promise rejection', { err });
  });

  process.on('uncaughtException', (err) => {
    log.error('Uncaught exception', { err });
  });

  return { server, bridge };
}

module.exports = { startServer };
