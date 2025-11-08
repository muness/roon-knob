const express = require('express');
const path = require('path');
const { recordEvent, snapshot } = require('./metrics');

function extractKnob(req) {
  const headerId = req.get('x-knob-id') || req.get('x-device-id');
  const queryId = req.query?.knob_id;
  const bodyId = req.body?.knob_id;
  const id = headerId || queryId || bodyId;
  const version = req.get('x-knob-version') || req.get('x-device-version');
  if (!id && !version) return null;
  return { id, version };
}

function createRoutes({ bridge, metrics }) {
  const router = express.Router();

  router.get('/zones', (req, res) => {
    recordEvent(metrics, 'zones', req, { knob: extractKnob(req) });
    res.json(bridge.getZones());
  });

  router.get('/now_playing', (req, res) => {
    const zoneId = req.query.zone_id;
    const data = bridge.getNowPlaying(zoneId);
    if (!data) {
      recordEvent(metrics, 'now_playing', req, { zone_id: zoneId, status: 'miss', knob: extractKnob(req) });
      return res.status(404).json({ error: 'zone not found or no data yet' });
    }
    recordEvent(metrics, 'now_playing', req, { zone_id: zoneId, knob: extractKnob(req) });
    res.json(data);
  });

  router.post('/control', async (req, res) => {
    const { zone_id, action, value } = req.body || {};
    if (!zone_id || !action) {
      return res.status(400).json({ error: 'zone_id and action required' });
    }
    try {
      await bridge.control(zone_id, action, value);
      recordEvent(metrics, 'control', req, { zone_id, action, value, knob: extractKnob(req) });
      res.json({ status: 'ok' });
    } catch (error) {
      recordEvent(metrics, 'control_error', req, { zone_id, action, value, knob: extractKnob(req) });
      res.status(500).json({ error: error.message || 'control failed' });
    }
  });

  router.get('/now_playing/mock', (_req, res) => {
    res.json({
      line1: 'Mock Track',
      line2: 'Mock Artist',
      is_playing: true,
      volume: 30,
      volume_step: 2,
    });
  });

  router.get('/image', (_req, res) => {
    res.status(204).end();
  });

  router.get('/admin/status.json', (_req, res) => {
    res.json({
      bridge: bridge.getStatus(),
      metrics: snapshot(metrics),
    });
  });

  router.get(['/admin', '/dashboard'], (_req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'admin.html'));
  });

  return router;
}

module.exports = { createRoutes };
