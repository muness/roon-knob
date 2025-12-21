const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

// Config file path - use data directory for persistence in Docker
function getConfigDir() {
  return process.env.CONFIG_DIR || path.join(__dirname, 'data');
}

function getKnobsFile() {
  return path.join(getConfigDir(), 'knobs.json');
}

// Default config for new knobs (match firmware defaults in rk_cfg.h)
// Note: 'name' is a top-level knob field, not part of config
const DEFAULT_CONFIG = {
  rotation_charging: 180,
  rotation_not_charging: 0,
  // Charging: longer timeouts, no sleep (plugged in = always on)
  art_mode_charging: { enabled: true, timeout_sec: 60 },
  dim_charging: { enabled: true, timeout_sec: 120 },
  sleep_charging: { enabled: false, timeout_sec: 0 },
  // Battery: shorter timeouts to save power
  art_mode_battery: { enabled: true, timeout_sec: 30 },
  dim_battery: { enabled: true, timeout_sec: 30 },
  sleep_battery: { enabled: true, timeout_sec: 60 },
};

function computeSha(config) {
  const json = JSON.stringify(config);
  return crypto.createHash('sha256').update(json).digest('hex').substring(0, 8);
}

function ensureDir() {
  const configDir = getConfigDir();
  if (!fs.existsSync(configDir)) {
    fs.mkdirSync(configDir, { recursive: true });
  }
}

function loadKnobs() {
  ensureDir();
  try {
    const content = fs.readFileSync(getKnobsFile(), { encoding: 'utf8' });
    return JSON.parse(content) || {};
  } catch (e) {
    return {};
  }
}

function saveKnobs(knobs) {
  ensureDir();
  fs.writeFileSync(getKnobsFile(), JSON.stringify(knobs, null, 2));
}

function createKnobsStore(opts = {}) {
  const log = opts.logger || console;
  let knobs = loadKnobs();

  function getKnob(knobId) {
    if (!knobId) return null;
    return knobs[knobId] || null;
  }

  function getOrCreateKnob(knobId, version = null) {
    if (!knobId) return null;

    if (!knobs[knobId]) {
      log.info('Creating new knob config', { knobId });
      const config = { ...DEFAULT_CONFIG };
      const name = '';
      knobs[knobId] = {
        name,
        last_seen: new Date().toISOString(),
        version: version || null,
        config,
        config_sha: computeSha({ ...config, name }),  // Include name in SHA
        status: {
          battery_level: null,
          battery_charging: null,
          zone_id: null,
        },
      };
      saveKnobs(knobs);
    } else {
      // Update last_seen and version
      knobs[knobId].last_seen = new Date().toISOString();
      if (version) {
        knobs[knobId].version = version;
      }
      // Ensure status object exists (migration for existing entries)
      if (!knobs[knobId].status) {
        knobs[knobId].status = {
          battery_level: null,
          battery_charging: null,
          zone_id: null,
        };
      }
      saveKnobs(knobs);
    }

    return knobs[knobId];
  }

  function updateKnobConfig(knobId, updates) {
    if (!knobId) return null;

    const knob = getOrCreateKnob(knobId);
    if (!knob) return null;

    // Merge updates into existing config
    const newConfig = { ...knob.config };

    // Handle top-level updates
    if (updates.name !== undefined) knob.name = updates.name;
    if (updates.rotation_charging !== undefined) newConfig.rotation_charging = updates.rotation_charging;
    if (updates.rotation_not_charging !== undefined) newConfig.rotation_not_charging = updates.rotation_not_charging;

    // Handle nested objects (merge, don't replace)
    if (updates.art_mode_charging) {
      newConfig.art_mode_charging = { ...newConfig.art_mode_charging, ...updates.art_mode_charging };
    }
    if (updates.art_mode_battery) {
      newConfig.art_mode_battery = { ...newConfig.art_mode_battery, ...updates.art_mode_battery };
    }
    if (updates.dim_charging) {
      newConfig.dim_charging = { ...newConfig.dim_charging, ...updates.dim_charging };
    }
    if (updates.dim_battery) {
      newConfig.dim_battery = { ...newConfig.dim_battery, ...updates.dim_battery };
    }
    if (updates.sleep_charging) {
      newConfig.sleep_charging = { ...newConfig.sleep_charging, ...updates.sleep_charging };
    }
    if (updates.sleep_battery) {
      newConfig.sleep_battery = { ...newConfig.sleep_battery, ...updates.sleep_battery };
    }

    knob.config = newConfig;
    // SHA includes name since it's part of the config payload sent to knob
    knob.config_sha = computeSha({ ...newConfig, name: knob.name });
    knob.last_seen = new Date().toISOString();

    log.info('Updated knob config', { knobId, config_sha: knob.config_sha });
    saveKnobs(knobs);

    return knob;
  }

  function listKnobs() {
    return Object.entries(knobs).map(([knobId, data]) => ({
      knob_id: knobId,
      name: data.name || '',
      last_seen: data.last_seen,
      version: data.version,
      status: data.status || null,
    }));
  }

  function getConfigSha(knobId) {
    const knob = knobs[knobId];
    return knob?.config_sha || null;
  }

  function recordKnobSeen(knobId, version = null) {
    if (!knobId) return;
    getOrCreateKnob(knobId, version);
  }

  function updateKnobStatus(knobId, statusUpdates) {
    if (!knobId) return;
    const knob = getOrCreateKnob(knobId);
    if (!knob) return;

    // Merge status updates
    knob.status = { ...knob.status, ...statusUpdates };
    knob.last_seen = new Date().toISOString();
    saveKnobs(knobs);
  }

  return {
    getKnob,
    getOrCreateKnob,
    updateKnobConfig,
    updateKnobStatus,
    listKnobs,
    getConfigSha,
    recordKnobSeen,
    DEFAULT_CONFIG,
  };
}

module.exports = { createKnobsStore, DEFAULT_CONFIG, computeSha };
