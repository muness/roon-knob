const RoonApi = require('node-roon-api');
const RoonApiStatus = require('node-roon-api-status');
const RoonApiTransport = require('node-roon-api-transport');

const RATE_LIMIT_INTERVAL_MS = 100;
const MAX_RELATIVE_STEP_PER_CALL = 25;
const MAX_VOLUME = 100;
const MIN_VOLUME = 0;
const CORE_LOSS_TIMEOUT_MS = 5 * 60 * 1000;

const fallbackSummary = (zone) => ({
  line1: 'Idle',
  line2: zone?.now_playing?.three_line?.line2 || zone?.display_name || '',
  is_playing: false,
  volume: zone?.outputs?.[0]?.volume?.value ?? null,
  volume_step: zone?.outputs?.[0]?.volume?.step ?? 2,
  zone_id: zone?.zone_id,
});

function createRoonBridge(opts = {}) {
  const log = opts.logger || console;
  const state = {
    core: null,
    coreInfo: null,
    transport: null,
    zones: [],
    nowPlayingByZone: new Map(),
    lastVolumeTick: new Map(),
    pendingRelative: new Map(),
    lastCoreSeen: 0,
    coreLossTimer: null,
  };

  const roon = new RoonApi({
    extension_id: opts.extension_id || 'roon.knob.sidecar',
    display_name: opts.display_name || 'Roon Knob Bridge',
    display_version: opts.display_version || '0.1.0',
    publisher: 'Roon Knob',
    email: 'support@example.com',
    website: 'https://github.com/muness/roon-knob',
    core_paired(core) {
      log.info('Roon core paired', { id: core.core_id, name: core.display_name });
      state.core = core;
      state.transport = core.services.RoonApiTransport;
      state.coreInfo = {
        id: core.core_id,
        name: core.display_name,
        version: core.display_version,
      };
      state.lastCoreSeen = Date.now();
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

  roon.service_port = opts.service_port || 9330;

  const svc_status = new RoonApiStatus(roon);
  svc_status.set_status('Starting Roon discovery...', false);

  function subscribe(core) {
    const transport = core.services.RoonApiTransport;
    if (!transport) {
      const msg = 'Transport service unavailable';
      log.error(msg);
      svc_status.set_status(msg, true);
      return;
    }

    transport.subscribe_zones((msg, data) => {
      log.debug('transport event', { msg, zones: data?.zones?.length });
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
      }
    });

    svc_status.set_status('Connected to Roon core', false);
  }

  function updateZone(zone) {
    if (!zone || !zone.zone_id) return;
    log.debug('update zone', { zone: zone.zone_id, state: zone.state });
    const summary = {
      line1: zone.now_playing?.three_line?.line1 || zone.display_name || 'Unknown zone',
      line2: zone.now_playing?.three_line?.line2 || '',
      is_playing: zone.state === 'playing',
      volume: zone.outputs?.[0]?.volume?.value ?? null,
      volume_step: zone.outputs?.[0]?.volume?.step ?? 2,
      zone_id: zone.zone_id,
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
    const cached = state.nowPlayingByZone.get(zone_id);
    if (cached) return cached;
    const zone = state.zones.find((z) => z.zone_id === zone_id);
    if (!zone) return null;
    const fallback = fallbackSummary(zone);
    state.nowPlayingByZone.set(zone_id, fallback);
    return fallback;
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
        await callTransport('play_pause', { control: 'toggle', zone_or_output_id: zone_id });
        break;
      case 'vol_rel':
        log.debug('control vol_rel', { zone_id, value: deltaValue(value) });
        await enqueueRelativeVolume(output.output_id, Number(value) || 0);
        break;
      case 'vol_abs':
        log.debug('control vol_abs', { zone_id, value });
        await callTransport('change_volume', {
          volume_type: 'absolute',
          volume: clampVolume(Number(value)),
          zone_or_output_id: output.output_id,
        });
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

    await callTransport('change_volume', {
      zone_or_output_id: output_id,
      volume_type: 'relative_step',
      volume: step,
    });

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

  function callTransport(method, payload) {
    return new Promise((resolve, reject) => {
      state.transport[method](payload, (err, response) => {
        if (err) return reject(err);
        resolve(response);
      });
    });
  }

  function start() {
    roon.init_services({
      required_services: [RoonApiTransport],
      provided_services: [svc_status],
    });
    roon.start_discovery();
  }

  return {
    start,
    getZones,
    getNowPlaying,
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
