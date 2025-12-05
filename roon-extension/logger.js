function timestamp() {
  return new Date().toISOString();
}

function serializeError(err) {
  if (!err) return err;
  if (err instanceof Error) {
    return {
      message: err.message,
      stack: err.stack,
      name: err.name,
      ...err,
    };
  }
  return err;
}

function serializeMeta(meta) {
  if (!meta || typeof meta !== 'object') return meta;
  const result = {};
  for (const [key, value] of Object.entries(meta)) {
    result[key] = value instanceof Error ? serializeError(value) : value;
  }
  return result;
}

function log(scope, level, message, meta) {
  const parts = [`[${timestamp()}][${scope}][${level.toUpperCase()}]`, message];
  if (meta && Object.keys(meta).length) {
    const serialized = serializeMeta(meta);
    parts.push(JSON.stringify(serialized));
  }
  console.log(parts.join(' '));
}

function createLogger(scope) {
  return {
    info: (msg, meta = {}) => log(scope, 'info', msg, meta),
    warn: (msg, meta = {}) => log(scope, 'warn', msg, meta),
    error: (msg, meta = {}) => log(scope, 'error', msg, meta),
    debug: (msg, meta = {}) => {
      if ((process.env.LOG_LEVEL || '').toLowerCase() === 'debug') {
        log(scope, 'debug', msg, meta);
      }
    },
  };
}

module.exports = { createLogger };
