const {temperature, humidity, battery} = require('zigbee-herdsman-converters/lib/modernExtend');

const definition = {
    zigbeeModel: ['FBE_TH_1'],
    model: 'FBE_TH_1',
    vendor: 'Frostbee',
    description: 'Temperature & humidity sensor (SHT40)',
    extend: [
        temperature(),
        humidity(),
        battery(),
    ],
};

module.exports = definition;
