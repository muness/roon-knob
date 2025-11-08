const express = require('express');
const morgan = require('morgan');
const cors = require('cors');
const { advertise } = require('./mdns');
const { createRoutes } = require('./routes');
const { createRoonBridge } = require('./bridge');
const { createMetricsTracker } = require('./metrics');

function startServer() {
  const PORT = parseInt(process.env.PORT || '8088', 10);
  const MDNS_NAME = process.env.MDNS_NAME || 'Roon Knob Bridge';
  const LOG_LEVEL = process.env.LOG_LEVEL || 'info';
  const SERVICE_PORT = parseInt(process.env.ROON_SERVICE_PORT || '9330', 10);

  const metrics = createMetricsTracker();
  const app = express();
  app.use(express.json());
  app.use(cors());
  app.use(morgan(LOG_LEVEL === 'debug' ? 'dev' : 'tiny'));

  const bridge = createRoonBridge({ service_port: SERVICE_PORT, display_name: MDNS_NAME });
  bridge.start();

  app.use(createRoutes({ bridge, metrics }));

  app.get('/status', (_req, res) => {
    res.json({ status: 'ok', version: '0.1.0' });
  });

  const server = app.listen(PORT, () => {
    console.log(`[sidecar] HTTP listening on ${PORT}`);
    const adv = advertise(PORT, {
      name: MDNS_NAME,
      base: `http://${require('os').hostname()}:${PORT}`,
      txt: { api: '1' },
    });
    metrics.mdns = {
      name: MDNS_NAME,
      port: PORT,
      base: `http://${require('os').hostname()}:${PORT}`,
      advertisedAt: Date.now(),
    };
  });

  return { server, bridge };
}

module.exports = { startServer };
