const fs = require('fs');
const path = require('path');
const { createKnobsStore, DEFAULT_CONFIG, computeSha } = require('./knobs');

const TEST_DATA_DIR = path.join(__dirname, 'test-data');
const TEST_KNOBS_FILE = path.join(TEST_DATA_DIR, 'knobs.json');

describe('knobs', () => {
  let knobs;

  beforeEach(() => {
    // Use test data directory
    process.env.CONFIG_DIR = TEST_DATA_DIR;

    // Clean up any existing test data
    if (fs.existsSync(TEST_KNOBS_FILE)) {
      fs.unlinkSync(TEST_KNOBS_FILE);
    }
    if (fs.existsSync(TEST_DATA_DIR)) {
      fs.rmSync(TEST_DATA_DIR, { recursive: true });
    }

    // Create fresh store
    knobs = createKnobsStore({ logger: null });
  });

  afterEach(() => {
    // Clean up test data
    if (fs.existsSync(TEST_KNOBS_FILE)) {
      fs.unlinkSync(TEST_KNOBS_FILE);
    }
    if (fs.existsSync(TEST_DATA_DIR)) {
      fs.rmSync(TEST_DATA_DIR, { recursive: true });
    }
    delete process.env.CONFIG_DIR;
  });

  describe('computeSha', () => {
    it('returns 8-character hex string', () => {
      const sha = computeSha({ foo: 'bar' });
      expect(sha).toMatch(/^[a-f0-9]{8}$/);
    });

    it('returns different SHA for different configs', () => {
      const sha1 = computeSha({ rotation: 0 });
      const sha2 = computeSha({ rotation: 180 });
      expect(sha1).not.toBe(sha2);
    });

    it('returns same SHA for identical configs', () => {
      const sha1 = computeSha({ rotation: 90, name: 'test' });
      const sha2 = computeSha({ rotation: 90, name: 'test' });
      expect(sha1).toBe(sha2);
    });
  });

  describe('getOrCreateKnob', () => {
    it('creates new knob with default config', () => {
      const knob = knobs.getOrCreateKnob('abc123');
      expect(knob).toBeDefined();
      expect(knob.config.rotation_charging).toBe(180);
      expect(knob.config.rotation_not_charging).toBe(0);
    });

    it('sets last_seen timestamp', () => {
      const knob = knobs.getOrCreateKnob('abc123');
      expect(knob.last_seen).toBeDefined();
      expect(new Date(knob.last_seen).getTime()).toBeGreaterThan(0);
    });

    it('stores firmware version', () => {
      const knob = knobs.getOrCreateKnob('abc123', '1.2.3');
      expect(knob.version).toBe('1.2.3');
    });

    it('returns existing knob on second call', () => {
      const knob1 = knobs.getOrCreateKnob('abc123');
      knobs.updateKnobConfig('abc123', { name: 'Test' });
      const knob2 = knobs.getOrCreateKnob('abc123');
      expect(knob2.name).toBe('Test');
    });

    it('generates config_sha', () => {
      const knob = knobs.getOrCreateKnob('abc123');
      expect(knob.config_sha).toMatch(/^[a-f0-9]{8}$/);
    });

    it('returns null for empty knob_id', () => {
      expect(knobs.getOrCreateKnob('')).toBeNull();
      expect(knobs.getOrCreateKnob(null)).toBeNull();
      expect(knobs.getOrCreateKnob(undefined)).toBeNull();
    });
  });

  describe('getKnob', () => {
    it('returns null for unknown knob', () => {
      expect(knobs.getKnob('unknown')).toBeNull();
    });

    it('returns existing knob', () => {
      knobs.getOrCreateKnob('abc123', '1.0.0');
      const knob = knobs.getKnob('abc123');
      expect(knob).toBeDefined();
      expect(knob.version).toBe('1.0.0');
    });
  });

  describe('updateKnobConfig', () => {
    beforeEach(() => {
      knobs.getOrCreateKnob('abc123');
    });

    it('updates top-level fields', () => {
      const updated = knobs.updateKnobConfig('abc123', {
        name: 'Living Room',
        rotation_charging: 90,
      });
      expect(updated.name).toBe('Living Room');
      expect(updated.config.rotation_charging).toBe(90);
    });

    it('merges nested objects', () => {
      const updated = knobs.updateKnobConfig('abc123', {
        dim_charging: { enabled: false },
      });
      expect(updated.config.dim_charging.enabled).toBe(false);
      expect(updated.config.dim_charging.timeout_sec).toBe(120); // preserved
    });

    it('updates config_sha on change', () => {
      const knob1 = knobs.getKnob('abc123');
      const sha1 = knob1.config_sha;

      knobs.updateKnobConfig('abc123', { rotation_charging: 270 });
      const knob2 = knobs.getKnob('abc123');

      expect(knob2.config_sha).not.toBe(sha1);
    });

    it('returns null for unknown knob_id', () => {
      expect(knobs.updateKnobConfig('', {})).toBeNull();
    });
  });

  describe('listKnobs', () => {
    it('returns empty array when no knobs', () => {
      expect(knobs.listKnobs()).toEqual([]);
    });

    it('returns all known knobs', () => {
      knobs.getOrCreateKnob('abc123', '1.0.0');
      knobs.updateKnobConfig('abc123', { name: 'Knob 1' });
      knobs.getOrCreateKnob('def456', '1.1.0');
      knobs.updateKnobConfig('def456', { name: 'Knob 2' });

      const list = knobs.listKnobs();
      expect(list).toHaveLength(2);
      expect(list.map(k => k.knob_id).sort()).toEqual(['abc123', 'def456']);
      expect(list.find(k => k.knob_id === 'abc123').name).toBe('Knob 1');
    });

    it('includes last_seen and version', () => {
      knobs.getOrCreateKnob('abc123', '2.0.0');
      const list = knobs.listKnobs();
      expect(list[0].last_seen).toBeDefined();
      expect(list[0].version).toBe('2.0.0');
    });
  });

  describe('recordKnobSeen', () => {
    it('creates knob if not exists', () => {
      knobs.recordKnobSeen('newknob', '1.0.0');
      const knob = knobs.getKnob('newknob');
      expect(knob).toBeDefined();
      expect(knob.version).toBe('1.0.0');
    });

    it('updates last_seen for existing knob', () => {
      knobs.getOrCreateKnob('abc123');
      const knob1 = knobs.getKnob('abc123');
      const time1 = new Date(knob1.last_seen).getTime();

      // Wait a bit
      const waitUntil = Date.now() + 10;
      while (Date.now() < waitUntil) {}

      knobs.recordKnobSeen('abc123');
      const knob2 = knobs.getKnob('abc123');
      const time2 = new Date(knob2.last_seen).getTime();

      expect(time2).toBeGreaterThanOrEqual(time1);
    });
  });

  describe('persistence', () => {
    it('persists knobs to file', () => {
      knobs.getOrCreateKnob('abc123');
      knobs.updateKnobConfig('abc123', { name: 'Persistent Knob' });

      // Create new store instance
      const knobs2 = createKnobsStore({ logger: null });
      const knob = knobs2.getKnob('abc123');

      expect(knob).toBeDefined();
      expect(knob.name).toBe('Persistent Knob');
    });
  });

  describe('DEFAULT_CONFIG', () => {
    it('has expected default values', () => {
      expect(DEFAULT_CONFIG.rotation_charging).toBe(180);
      expect(DEFAULT_CONFIG.rotation_not_charging).toBe(0);
      expect(DEFAULT_CONFIG.art_mode_charging.enabled).toBe(true);
      expect(DEFAULT_CONFIG.art_mode_charging.timeout_sec).toBe(60);
      expect(DEFAULT_CONFIG.art_mode_battery.enabled).toBe(true);
      expect(DEFAULT_CONFIG.art_mode_battery.timeout_sec).toBe(30);
      expect(DEFAULT_CONFIG.dim_charging.enabled).toBe(true);
      expect(DEFAULT_CONFIG.dim_charging.timeout_sec).toBe(120);
      expect(DEFAULT_CONFIG.sleep_charging.enabled).toBe(false);
      expect(DEFAULT_CONFIG.sleep_charging.timeout_sec).toBe(0);
    });
  });
});
