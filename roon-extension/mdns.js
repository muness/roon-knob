const bonjour = require('bonjour')();

function advertise(port, props = {}, log = console) {
  const serviceConfig = {
    name: props.name || 'Roon Knob Bridge',
    type: 'roonknob',
    protocol: 'tcp',
    port,
    txt: {
      base: props.base || `http://localhost:${port}`,
      api: '1',
      ...props.txt
    }
  };

  log.info('mDNS: Publishing service', {
    name: serviceConfig.name,
    type: `_${serviceConfig.type}._${serviceConfig.protocol}`,
    port: serviceConfig.port,
    txt: serviceConfig.txt
  });

  const service = bonjour.publish(serviceConfig);

  service.on('up', () => {
    log.info('mDNS: Service published successfully', { name: serviceConfig.name });
  });

  service.on('error', (err) => {
    log.error('mDNS: Service error', { error: err.message || err });
  });

  process.on('exit', () => service.stop());
  return service;
}

module.exports = { advertise };
