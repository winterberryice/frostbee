import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['FBE_TH_1'],
    model: 'FBE_TH_1',
    vendor: 'Frostbee',
    description: 'Temperature & humidity sensor (SHT40)',
    extend: [m.battery(), m.temperature(), m.humidity()],
};