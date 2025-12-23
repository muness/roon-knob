const RoonApi = require('node-roon-api');
const RoonApiStatus = require('node-roon-api-status');
const RoonApiTransport = require('node-roon-api-transport');
const RoonApiImage = require('node-roon-api-image');
const { version: VERSION } = require('./package.json');
const fs = require('fs');
const path = require('path');

// Config file path - use data directory for persistence in Docker
const CONFIG_DIR = process.env.CONFIG_DIR || path.join(__dirname, 'data');
const CONFIG_FILE = path.join(CONFIG_DIR, 'config.json');

// Ensure config directory exists
if (!fs.existsSync(CONFIG_DIR)) {
  fs.mkdirSync(CONFIG_DIR, { recursive: true });
}

const RATE_LIMIT_INTERVAL_MS = 100;
const MAX_RELATIVE_STEP_PER_CALL = 25;
const MAX_VOLUME = 100;
const MIN_VOLUME = 0;
const CORE_LOSS_TIMEOUT_MS = 5 * 60 * 1000;
const TRANSPORT_GRACE_PERIOD_MS = 5 * 1000; // Keep serving cached data for 5s during reconnect

const fallbackSummary = (zone) => {
  const vol = zone?.outputs?.[0]?.volume;
  return {
    line1: 'Idle',
    line2: zone?.now_playing?.three_line?.line2 || zone?.display_name || '',
    is_playing: false,
    volume: vol?.value ?? null,
    volume_min: vol?.min ?? -80,
    volume_max: vol?.max ?? 0,
    volume_step: vol?.step ?? 2,
    seek_position: zone?.now_playing?.seek_position ?? null,
    length: zone?.now_playing?.length ?? null,
    zone_id: zone?.zone_id,
  };
};

function createRoonBridge(opts = {}) {
  const log = opts.logger || console;
  const baseUrl = opts.base_url || '';
  const state = {
    core: null,
    coreInfo: null,
    transport: null,
    image: null,
    zones: [],
    nowPlayingByZone: new Map(),
    lastVolumeTick: new Map(),
    pendingRelative: new Map(),
    lastCoreSeen: 0,
    coreLossTimer: null,
    transportDisconnectedAt: null,
  };

  const roon = new RoonApi({
    extension_id: opts.extension_id || 'roon.knob.sidecar',
    display_name: opts.display_name || 'Roon Knob Bridge',
    display_version: opts.display_version || VERSION,
    publisher: 'Roon Knob',
    email: 'support@example.com',
    website: 'https://github.com/muness/roon-knob',
    log_level: 'none',  // Suppress verbose RPC logging (Buffer data, etc.)
    core_paired(core) {
      log.info('Roon core paired', { id: core.core_id, name: core.display_name });
      state.core = core;
      state.transport = core.services.RoonApiTransport;
      state.image = core.services.RoonApiImage || null;
      state.coreInfo = {
        id: core.core_id,
        name: core.display_name,
        version: core.display_version,
      };
      state.lastCoreSeen = Date.now();
      state.transportDisconnectedAt = null; // Clear disconnect timer on reconnect
      if (state.coreLossTimer) {
        clearTimeout(state.coreLossTimer);
        state.coreLossTimer = null;
      }
      subscribe(core);
    },
    core_unpaired() {
      log.warn('Roon core disconnected');
      state.core = null;
      state.transport = null;
      state.image = null;
      state.coreInfo = null;
      svc_status.set_status('Waiting for Roon core', true);
      if (state.coreLossTimer) {
        clearTimeout(state.coreLossTimer);
      }
      state.coreLossTimer = setTimeout(() => {
        if (state.core) return;
        log.warn('Core offline for prolonged period, clearing zone cache');
        state.zones = [];
        state.nowPlayingByZone.clear();
        state.pendingRelative.clear();
      }, CORE_LOSS_TIMEOUT_MS);
    },
  });

  // Override config storage to use data directory for Docker persistence
  roon.save_config = function(k, v) {
    try {
      let config = {};
      try {
        const content = fs.readFileSync(CONFIG_FILE, { encoding: 'utf8' });
        config = JSON.parse(content) || {};
      } catch (e) {
        // File doesn't exist yet, start fresh
      }
      if (v === undefined || v === null) {
        delete config[k];
      } else {
        config[k] = v;
      }
      fs.writeFileSync(CONFIG_FILE, JSON.stringify(config, null, '    '));
      log.debug('Config saved', { key: k, path: CONFIG_FILE });
    } catch (e) {
      log.error('Failed to save config', { error: e.message, path: CONFIG_FILE });
    }
  };

  roon.load_config = function(k) {
    try {
      const content = fs.readFileSync(CONFIG_FILE, { encoding: 'utf8' });
      const config = JSON.parse(content) || {};
      return config[k];
    } catch (e) {
      return undefined;
    }
  };

  roon.service_port = opts.service_port || 9330;

  const svc_status = new RoonApiStatus(roon);
  svc_status.set_status('Waiting for authorization in Roon → Settings → Extensions', false);
  log.info('Waiting for Roon Core. Authorize in Roon → Settings → Extensions');

  function subscribe(core) {
    const transport = core.services.RoonApiTransport;
    if (!transport) {
      const msg = 'Transport service unavailable';
      log.error(msg);
      svc_status.set_status(msg, true);
      return;
    }

    transport.subscribe_zones((msg, data) => {
      // Only log if there's meaningful data (suppress noisy empty Changed events)
      const hasData = data?.zones?.length || data?.zones_changed?.length || data?.zones_removed?.length;
      if (hasData) {
        log.debug('transport event', { msg, zones: data?.zones?.length, changed: data?.zones_changed?.length });
      }
      if (msg === 'Subscribed' && data?.zones) {
        state.zones = data.zones;
        data.zones.forEach(updateZone);
      } else if (msg === 'Changed') {
        if (Array.isArray(data?.zones_removed)) {
          data.zones_removed.forEach((zone_id) => {
            state.nowPlayingByZone.delete(zone_id);
            state.zones = state.zones.filter((z) => z.zone_id !== zone_id);
          });
        }
        if (Array.isArray(data?.zones_changed)) {
          data.zones_changed.forEach(updateZone);
        }
      } else if (msg === 'NetworkError') {
        log.warn('Zone subscription network error - entering grace period');
        // Mark when transport became unavailable, but keep serving cached data briefly
        if (!state.transportDisconnectedAt) {
          state.transportDisconnectedAt = Date.now();
        }
      } else if (msg === 'Unsubscribed') {
        log.info('Zone subscription ended');
      } else {
        log.warn('Unexpected transport event', { msg, data });
      }
    });

    const statusMsg = baseUrl ? `Connected to Roon core • ${baseUrl}` : 'Connected to Roon core';
    svc_status.set_status(statusMsg, false);
  }

  function updateZone(zone) {
    if (!zone || !zone.zone_id) return;
    log.debug('update zone', { zone: zone.zone_id, state: zone.state });
    const vol = zone.outputs?.[0]?.volume;
    const summary = {
      line1: zone.now_playing?.three_line?.line1 || zone.display_name || 'Unknown zone',
      line2: zone.now_playing?.three_line?.line2 || '',
      is_playing: zone.state === 'playing',
      volume: vol?.value ?? null,
      volume_min: vol?.min ?? -80,
      volume_max: vol?.max ?? 0,
      volume_step: vol?.step ?? 2,
      seek_position: zone.now_playing?.seek_position ?? null,
      length: zone.now_playing?.length ?? null,
      zone_id: zone.zone_id,
      image_key: zone.now_playing?.image_key || null,
    };
    state.nowPlayingByZone.set(zone.zone_id, summary);

    const idx = state.zones.findIndex((z) => z.zone_id === zone.zone_id);
    if (idx >= 0) {
      state.zones[idx] = zone;
    } else {
      state.zones.push(zone);
    }
  }

  function getZones() {
    return state.zones.map((zone) => ({
      zone_id: zone.zone_id,
      zone_name: zone.display_name,
    }));
  }

  function getNowPlaying(zone_id) {
    if (!zone_id) return null;

    // Check if transport is available or still within grace period
    const transportUnavailable = !state.core || !state.transport;
    const withinGracePeriod = state.transportDisconnectedAt &&
      (Date.now() - state.transportDisconnectedAt) < TRANSPORT_GRACE_PERIOD_MS;

    if (transportUnavailable && !withinGracePeriod) {
      log.warn('Roon not connected. Authorize "Roon Knob Bridge" in Roon → Settings → Extensions', { zone_id });
      return null;
    }

    if (transportUnavailable && withinGracePeriod) {
      log.debug('getNowPlaying: serving cached data during grace period', { zone_id });
    }

    const cached = state.nowPlayingByZone.get(zone_id);
    if (cached) return cached;
    const zone = state.zones.find((z) => z.zone_id === zone_id);
    if (!zone) return null;
    const fallback = fallbackSummary(zone);
    state.nowPlayingByZone.set(zone_id, fallback);
    return fallback;
  }

  function getImage(image_key, opts = {}) {
    return new Promise((resolve, reject) => {
      if (!state.core || !state.image || !image_key) {
        const reason = !state.core ? 'no core' : !state.image ? 'no image service' : 'no image_key';
        log.warn('getImage unavailable', { reason, hasCore: !!state.core, hasImage: !!state.image, hasKey: !!image_key });
        return reject(new Error(`image service unavailable: ${reason}`));
      }
      const options = {};
      if (opts.scale) options.scale = opts.scale;
      if (opts.width) options.width = Number(opts.width);
      if (opts.height) options.height = Number(opts.height);
      if (opts.format) options.format = opts.format; // e.g., 'image/jpeg'
      log.debug('getImage requesting', { image_key: image_key.substring(0, 32), options });
      try {
        state.image.get_image(image_key, options, (err, contentType, body) => {
          if (err) {
            log.warn('getImage failed', { error: String(err), image_key: image_key.substring(0, 32) });
            return reject(new Error(String(err)));
          }
          log.debug('getImage success', { contentType, bodyLength: body?.length });
          resolve({ contentType, body });
        });
      } catch (e) {
        log.error('getImage exception', { error: e.message, image_key: image_key.substring(0, 32) });
        reject(e);
      }
    });
  }

  async function control(zone_id, action, value) {
    if (!state.transport) {
      throw new Error('Roon core unavailable');
    }
    const zone = state.zones.find((z) => z.zone_id === zone_id);
    if (!zone) throw new Error('Zone not found');
    const output = zone.outputs?.[0];
    if (!output) throw new Error('No output for zone');

    switch (action) {
      case 'play_pause':
        log.debug('control play_pause', { zone_id });
        await callTransport('control', zone_id, 'playpause');
        break;
      case 'next':
        log.debug('control next', { zone_id });
        await callTransport('control', zone_id, 'next');
        break;
      case 'previous':
      case 'prev':
        log.debug('control previous', { zone_id });
        await callTransport('control', zone_id, 'previous');
        break;
      case 'vol_rel':
        log.debug('control vol_rel', { zone_id, value: deltaValue(value) });
        await enqueueRelativeVolume(output.output_id, Number(value) || 0);
        break;
      case 'vol_abs':
        log.debug('control vol_abs', { zone_id, value });
        await callTransport('change_volume', output.output_id, 'absolute', clampVolume(Number(value)));
        break;
      default:
        log.warn('Unknown control action', { action });
        throw new Error('Unknown action');
    }
  }

  function deltaValue(value) {
    const num = Number(value);
    return Number.isNaN(num) ? 0 : num;
  }

  async function enqueueRelativeVolume(output_id, delta) {
    if (!delta) return;
    const current = state.pendingRelative.get(output_id) || 0;
    state.pendingRelative.set(output_id, clampStep(current + delta));
    await flushRelativeQueue(output_id);
  }

  async function flushRelativeQueue(output_id) {
    const pending = state.pendingRelative.get(output_id) || 0;
    if (!pending) return;

    const now = Date.now();
    const lastTick = state.lastVolumeTick.get(output_id) || 0;
    if (now - lastTick < RATE_LIMIT_INTERVAL_MS) {
      setTimeout(() => flushRelativeQueue(output_id), RATE_LIMIT_INTERVAL_MS - (now - lastTick));
      return;
    }

    const step = Math.max(-MAX_RELATIVE_STEP_PER_CALL, Math.min(MAX_RELATIVE_STEP_PER_CALL, pending));
    state.pendingRelative.set(output_id, pending - step);
    state.lastVolumeTick.set(output_id, now);

    await callTransport('change_volume', output_id, 'relative_step', step);

    if (state.pendingRelative.get(output_id)) {
      setTimeout(() => flushRelativeQueue(output_id), RATE_LIMIT_INTERVAL_MS);
    }
  }

  function clampVolume(value) {
    if (Number.isNaN(value)) return MIN_VOLUME;
    if (value < MIN_VOLUME) return MIN_VOLUME;
    if (value > MAX_VOLUME) return MAX_VOLUME;
    return value;
  }

  function clampStep(value) {
    if (Number.isNaN(value)) return 0;
    const max = MAX_RELATIVE_STEP_PER_CALL;
    if (value > max) return max;
    if (value < -max) return -max;
    return value;
  }

  function callTransport(method, ...args) {
    return new Promise((resolve, reject) => {
      const callback = (err) => {
        if (err) return reject(new Error(err));
        resolve();
      };
      state.transport[method](...args, callback);
    });
  }

  function start() {
    roon.init_services({
      required_services: [RoonApiTransport, RoonApiImage],
      provided_services: [svc_status],
    });
    roon.start_discovery();
  }

  return {
    start,
    getZones,
    getNowPlaying,
    getImage,
    control,
    getStatus: () => ({
      connected: !!state.core,
      core: state.coreInfo,
      zone_count: state.zones.length,
      zones: getZones(),
      now_playing: Array.from(state.nowPlayingByZone.values()),
      updated_at: Date.now(),
    }),
  };
}

module.exports = { createRoonBridge };
