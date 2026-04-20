import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_TYPE,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)

from . import CONF_BAHA_RS485_ID, BahaRS485Component, baha_rs485_ns

DEPENDENCIES = ["baha_rs485"]

CONF_ROOM = "room"

TYPE_CURRENT_TEMPERATURE = "current_temperature"
TYPE_TARGET_TEMPERATURE = "target_temperature"

ROOMS = {
    "zone1": 0,
    "zone2": 1,
    "zone3": 2,
    "zone4": 3,
    "zone5": 4,
}

SensorKind = baha_rs485_ns.enum("SensorKind")
BahaTemperatureSensor = baha_rs485_ns.class_(
    "BahaTemperatureSensor",
    sensor.Sensor,
    cg.Parented.template(BahaRS485Component),
)

SENSOR_SCHEMA = sensor.sensor_schema(
    BahaTemperatureSensor,
    unit_of_measurement=UNIT_CELSIUS,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
)

CONFIG_SCHEMA = cv.typed_schema(
    {
        TYPE_CURRENT_TEMPERATURE: SENSOR_SCHEMA.extend(
            {
                cv.GenerateID(CONF_BAHA_RS485_ID): cv.use_id(BahaRS485Component),
                cv.Required(CONF_ROOM): cv.enum(ROOMS, lower=True),
            }
        ),
        TYPE_TARGET_TEMPERATURE: SENSOR_SCHEMA.extend(
            {
                cv.GenerateID(CONF_BAHA_RS485_ID): cv.use_id(BahaRS485Component),
                cv.Required(CONF_ROOM): cv.enum(ROOMS, lower=True),
            }
        ),
    }
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_parented(var, config[CONF_BAHA_RS485_ID])
    parent = await cg.get_variable(config[CONF_BAHA_RS485_ID])
    cg.add(var.set_room(config[CONF_ROOM]))

    if config[CONF_TYPE] == TYPE_CURRENT_TEMPERATURE:
        cg.add(var.set_kind(SensorKind.SENSOR_KIND_CURRENT_TEMPERATURE))
    else:
        cg.add(var.set_kind(SensorKind.SENSOR_KIND_TARGET_TEMPERATURE))

    cg.add(parent.register_temperature_sensor(var))
