const MAX_EVENTS = 50;

function createMetricsTracker() {
  return {
    startedAt: Date.now(),
    recentEvents: [],
    lastZonesRequest: null,
    lastControlRequest: null,
    lastNowPlayingRequest: new Map(),
    clients: new Map(),
    knobs: new Map(),
    mdns: null,
  };
}

function recordEvent(metrics, type, req, extra = {}) {
  const entry = {
    type,
    path: req?.path,
    ip: req?.ip,
    zone_id: extra.zone_id,
    action: extra.action,
    value: extra.value,
    knob: extra.knob,
    timestamp: Date.now(),
  };
  metrics.recentEvents.push(entry);
  if (metrics.recentEvents.length > MAX_EVENTS) {
    metrics.recentEvents.splice(0, metrics.recentEvents.length - MAX_EVENTS);
  }

  if (entry.ip) {
    metrics.clients.set(entry.ip, entry.timestamp);
  }

  if (entry.knob) {
    const key = entry.knob.id || entry.ip || Math.random().toString(36).slice(2);
    const prev = metrics.knobs.get(key) || {};
    metrics.knobs.set(key, {
      id: entry.knob.id,
      ip: entry.ip || prev.ip,
      version: entry.knob.version || prev.version,
      last_seen: entry.timestamp,
      last_zone: entry.zone_id || prev.last_zone,
      last_action: entry.action || prev.last_action || entry.type,
    });
  }

  if (type === 'zones') {
    metrics.lastZonesRequest = entry;
  } else if (type === 'control') {
    metrics.lastControlRequest = entry;
  } else if (type === 'now_playing') {
    metrics.lastNowPlayingRequest.set(extra.zone_id || 'unknown', entry);
  }
}

function snapshot(metrics) {
  const nowPlaying = {};
  for (const [zone, entry] of metrics.lastNowPlayingRequest.entries()) {
    nowPlaying[zone] = entry;
  }
  const clients = [];
  for (const [ip, ts] of metrics.clients.entries()) {
    clients.push({ ip, last_seen: ts });
  }
  clients.sort((a, b) => b.last_seen - a.last_seen);

  const knobs = [];
  for (const [id, info] of metrics.knobs.entries()) {
    knobs.push({
      key: id,
      id: info.id,
      ip: info.ip,
      version: info.version,
      last_seen: info.last_seen,
      last_zone: info.last_zone,
      last_action: info.last_action,
    });
  }
  knobs.sort((a, b) => (b.last_seen || 0) - (a.last_seen || 0));

  return {
    startedAt: metrics.startedAt,
    uptimeMs: Date.now() - metrics.startedAt,
    version: metrics.version,
    git_sha: metrics.git_sha,
    mdns: metrics.mdns,
    lastZonesRequest: metrics.lastZonesRequest,
    lastControlRequest: metrics.lastControlRequest,
    lastNowPlayingRequest: nowPlaying,
    recentEvents: metrics.recentEvents.slice().reverse(),
    clients,
    knobs,
  };
}

module.exports = {
  createMetricsTracker,
  recordEvent,
  snapshot,
};
