const express = require('express');
const path = require('path');
const fs = require('fs');
const sharp = require('sharp');
const { recordEvent, snapshot } = require('./metrics');

// Firmware directory for OTA updates
const FIRMWARE_DIR = path.join(__dirname, 'firmware');

function extractKnob(req) {
  const headerId = req.get('x-knob-id') || req.get('x-device-id');
  const queryId = req.query?.knob_id;
  const bodyId = req.body?.knob_id;
  const id = headerId || queryId || bodyId;
  const version = req.get('x-knob-version') || req.get('x-device-version');
  if (!id && !version) return null;
  return { id, version };
}

function createRoutes({ bridge, metrics, logger, knobs }) {
  const router = express.Router();

  // Knob config endpoints
  router.get('/knobs', (req, res) => {
    logger?.debug('Knobs list requested', { ip: req.ip });
    res.json({ knobs: knobs.listKnobs() });
  });

  router.get('/config/:knob_id', (req, res) => {
    const knobId = req.params.knob_id;
    const version = req.get('x-knob-version');
    logger?.debug('Config requested', { knobId, version, ip: req.ip });

    const knob = knobs.getOrCreateKnob(knobId, version);
    if (!knob) {
      return res.status(400).json({ error: 'knob_id required' });
    }

    res.json({
      config: {
        knob_id: knobId,
        name: knob.name,
        ...knob.config,
      },
      config_sha: knob.config_sha,
    });
  });

  router.put('/config/:knob_id', (req, res) => {
    const knobId = req.params.knob_id;
    const updates = req.body || {};
    logger?.info('Config update', { knobId, updates, ip: req.ip });

    const knob = knobs.updateKnobConfig(knobId, updates);
    if (!knob) {
      return res.status(400).json({ error: 'knob_id required' });
    }

    res.json({
      config: {
        knob_id: knobId,
        name: knob.name,
        ...knob.config,
      },
      config_sha: knob.config_sha,
    });
  });

  router.get('/zones', (req, res) => {
    const knob = extractKnob(req);
    recordEvent(metrics, 'zones', req, { knob });
    logger?.debug('Zones requested', { ip: req.ip, knob_id: knob?.id });

    const allZones = bridge.getZones();
    let zones = allZones;

    // Apply zone filtering if knob has config
    if (knob?.id) {
      const knobData = knobs.getKnob(knob.id);
      if (knobData?.config?.zones) {
        const { mode, zone_ids } = knobData.config.zones;
        if (mode === 'include' && zone_ids.length > 0) {
          zones = allZones.filter(z => zone_ids.includes(z.zone_id));
        } else if (mode === 'exclude' && zone_ids.length > 0) {
          zones = allZones.filter(z => !zone_ids.includes(z.zone_id));
        }
        // mode === 'all' -> no filtering
      }
    }

    // Fallback to all zones if filtering results in empty list
    if (zones.length === 0 && allZones.length > 0) {
      logger?.debug('Zone filtering resulted in empty list, falling back to all zones', { knob_id: knob?.id });
      zones = allZones;
    }

    res.json(zones);
  });

  router.get('/now_playing', (req, res) => {
    const zoneId = req.query.zone_id;
    const knob = extractKnob(req);

    if (!zoneId) {
      return res.status(400).json({ error: 'zone_id required', zones: bridge.getZones() });
    }
    const data = bridge.getNowPlaying(zoneId);
    if (!data) {
      recordEvent(metrics, 'now_playing', req, { zone_id: zoneId, status: 'miss', knob });
      logger?.warn('now_playing miss', { zoneId, ip: req.ip });
      return res.status(404).json({ error: 'zone not found', zones: bridge.getZones() });
    }
    recordEvent(metrics, 'now_playing', req, { zone_id: zoneId, knob });
    logger?.debug('now_playing served', { zoneId, ip: req.ip });

    const image_url = `/now_playing/image?zone_id=${encodeURIComponent(zoneId)}`;

    // Include config_sha for change detection
    const config_sha = knob?.id ? knobs.getKnob(knob.id)?.config_sha : null;

    res.json({ ...data, image_url, zones: bridge.getZones(), config_sha });
  });

  router.get('/now_playing/image', async (req, res) => {
    const zoneId = req.query.zone_id;
    if (!zoneId) {
      return res.status(400).json({ error: 'zone_id required' });
    }
    const data = bridge.getNowPlaying(zoneId);
    if (!data) {
      return res.status(404).json({ error: 'zone not found' });
    }
    recordEvent(metrics, 'now_playing_image', req, { zone_id: zoneId, knob: extractKnob(req) });
    logger?.debug('now_playing image requested', { zoneId, ip: req.ip });
    const { scale, width, height, format } = req.query || {};
    try {
      if (data.image_key && bridge.getImage) {
        // Check if RGB565 format is requested
        if (format === 'rgb565') {
          // Fetch JPEG from Roon API
          const { contentType, body } = await bridge.getImage(data.image_key, { scale, width, height, format: 'image/jpeg' });

          // Convert JPEG to RGB565
          const targetWidth = parseInt(width) || 360;
          const targetHeight = parseInt(height) || 360;

          // Use sharp to decode and convert to raw RGB565
          const rgb565Buffer = await sharp(body)
            .resize(targetWidth, targetHeight, { fit: 'cover' })
            .raw()
            .toBuffer({ resolveWithObject: true });

          // Convert RGB (24-bit) to RGB565 (16-bit)
          const rgb888 = rgb565Buffer.data;
          const rgb565 = Buffer.alloc((targetWidth * targetHeight * 2));

          for (let i = 0; i < rgb888.length; i += 3) {
            const r = rgb888[i] >> 3;       // 8-bit to 5-bit
            const g = rgb888[i + 1] >> 2;   // 8-bit to 6-bit
            const b = rgb888[i + 2] >> 3;   // 8-bit to 5-bit

            // Pack into 16-bit RGB565 format
            const rgb565Pixel = (r << 11) | (g << 5) | b;
            const pixelIndex = (i / 3) * 2;

            // Write as little-endian (ESP32 expects little-endian)
            rgb565[pixelIndex] = rgb565Pixel & 0xFF;
            rgb565[pixelIndex + 1] = (rgb565Pixel >> 8) & 0xFF;
          }

          logger?.info('Converted image to RGB565', {
            width: targetWidth,
            height: targetHeight,
            size: rgb565.length
          });

          res.set('Content-Type', 'application/octet-stream');
          res.set('X-Image-Width', targetWidth.toString());
          res.set('X-Image-Height', targetHeight.toString());
          res.set('X-Image-Format', 'rgb565');
          return res.send(rgb565);
        } else {
          // Return original format (JPEG or other) - resize if width/height specified
          const { contentType, body } = await bridge.getImage(data.image_key, { scale, width, height, format });

          // If width/height specified and it's an image, resize it using Sharp
          if ((width || height) && contentType && contentType.startsWith('image/')) {
            const targetWidth = parseInt(width) || parseInt(height) || 360;
            const targetHeight = parseInt(height) || parseInt(width) || 360;

            const resizedBody = await sharp(body)
              .resize(targetWidth, targetHeight, { fit: 'cover' })
              .jpeg({ quality: 80, progressive: false, mozjpeg: false })  // Force baseline JPEG for ESP32
              .toBuffer();

            logger?.info('Resized JPEG image', {
              originalSize: body.length,
              resizedSize: resizedBody.length,
              width: targetWidth,
              height: targetHeight
            });

            res.set('Content-Type', 'image/jpeg');
            return res.send(resizedBody);
          }

          res.set('Content-Type', contentType || 'application/octet-stream');
          return res.send(body);
        }
      }
    } catch (error) {
      logger?.warn('now_playing image proxy failed; falling back to placeholder', { zoneId, error });
    }
    // Fall back to placeholder SVG
    const imagePath = path.join(__dirname, 'public', 'assets', 'now_playing.svg');
    res.set('Content-Type', 'image/svg+xml');
    res.sendFile(imagePath, (err) => {
      if (err && !res.headersSent) {
        logger?.error('Failed to send now_playing placeholder', { err });
        res.status(500).json({ error: 'image delivery failed' });
      }
    });
  });

  router.post('/control', async (req, res) => {
    const { zone_id, action, value } = req.body || {};
    if (!zone_id || !action) {
      logger?.warn('control missing params', { zone_id, action, ip: req.ip });
      return res.status(400).json({ error: 'zone_id and action required' });
    }
    try {
      logger?.info('control', { zone_id, action, value, ip: req.ip });
      await bridge.control(zone_id, action, value);
      recordEvent(metrics, 'control', req, { zone_id, action, value, knob: extractKnob(req) });
      res.json({ status: 'ok' });
    } catch (error) {
      logger?.error('control failed', { zone_id, action, value, ip: req.ip, error });
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

  // OTA Firmware endpoints
  router.get('/firmware/version', (req, res) => {
    const knob = extractKnob(req);
    logger?.info('Firmware version check', { knob, ip: req.ip });

    // Look for firmware file: firmware/roon_knob.bin or firmware/roon_knob_<version>.bin
    if (!fs.existsSync(FIRMWARE_DIR)) {
      logger?.debug('Firmware directory not found', { dir: FIRMWARE_DIR });
      return res.status(404).json({ error: 'No firmware available' });
    }

    const files = fs.readdirSync(FIRMWARE_DIR).filter(f => f.endsWith('.bin'));
    if (files.length === 0) {
      logger?.debug('No firmware files found');
      return res.status(404).json({ error: 'No firmware available' });
    }

    // Look for version.json or extract from filename
    const versionFile = path.join(FIRMWARE_DIR, 'version.json');
    let version = null;
    let firmwareFile = null;

    if (fs.existsSync(versionFile)) {
      try {
        const versionData = JSON.parse(fs.readFileSync(versionFile, 'utf8'));
        version = versionData.version;
        firmwareFile = versionData.file || 'roon_knob.bin';
      } catch (e) {
        logger?.warn('Failed to parse version.json', { error: e.message });
      }
    }

    // Fallback: use first .bin file and try to extract version from filename
    if (!firmwareFile) {
      firmwareFile = files[0];
      // Try to extract version from filename like roon_knob_1.2.0.bin
      const match = firmwareFile.match(/roon_knob[_-]?v?(\d+\.\d+\.\d+)\.bin/i);
      if (match) {
        version = match[1];
      }
    }

    if (!version) {
      logger?.warn('Could not determine firmware version');
      return res.status(404).json({ error: 'No firmware version available' });
    }

    const firmwarePath = path.join(FIRMWARE_DIR, firmwareFile);
    if (!fs.existsSync(firmwarePath)) {
      logger?.warn('Firmware file not found', { file: firmwarePath });
      return res.status(404).json({ error: 'Firmware file not found' });
    }

    const stats = fs.statSync(firmwarePath);
    logger?.info('Firmware available', { version, size: stats.size, file: firmwareFile });

    res.json({
      version,
      size: stats.size,
      file: firmwareFile
    });
  });

  router.get('/firmware/download', (req, res) => {
    const knob = extractKnob(req);
    logger?.info('Firmware download requested', { knob, ip: req.ip });

    if (!fs.existsSync(FIRMWARE_DIR)) {
      return res.status(404).json({ error: 'No firmware available' });
    }

    // Determine which file to serve
    let firmwareFile = 'roon_knob.bin';
    const versionFile = path.join(FIRMWARE_DIR, 'version.json');

    if (fs.existsSync(versionFile)) {
      try {
        const versionData = JSON.parse(fs.readFileSync(versionFile, 'utf8'));
        firmwareFile = versionData.file || firmwareFile;
      } catch (e) {
        logger?.warn('Failed to parse version.json', { error: e.message });
      }
    }

    const firmwarePath = path.join(FIRMWARE_DIR, firmwareFile);
    if (!fs.existsSync(firmwarePath)) {
      // Fallback to first .bin file
      const files = fs.readdirSync(FIRMWARE_DIR).filter(f => f.endsWith('.bin'));
      if (files.length > 0) {
        firmwareFile = files[0];
      } else {
        return res.status(404).json({ error: 'Firmware file not found' });
      }
    }

    const finalPath = path.join(FIRMWARE_DIR, firmwareFile);
    const stats = fs.statSync(finalPath);

    logger?.info('Serving firmware', { file: firmwareFile, size: stats.size });

    res.set('Content-Type', 'application/octet-stream');
    res.set('Content-Length', stats.size);
    res.set('Content-Disposition', `attachment; filename="${firmwareFile}"`);

    const stream = fs.createReadStream(finalPath);
    stream.pipe(res);
  });

  return router;
}

module.exports = { createRoutes };
