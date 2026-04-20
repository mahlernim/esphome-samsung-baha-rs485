import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import (
    CONF_TYPE,
    DEVICE_CLASS_SWITCH,
    ICON_LIGHTBULB,
    ICON_POWER,
    ICON_RADIATOR,
)

from . import CONF_BAHA_RS485_ID, BahaRS485Component, baha_rs485_ns

DEPENDENCIES = ["baha_rs485"]

CONF_ROOM = "room"
CONF_CHANNEL = "channel"
CONF_ON_DELTA = "on_delta"
CONF_OFF_DELTA = "off_delta"

TYPE_HEATER = "heater"
TYPE_LIGHT = "light"
TYPE_MASTER = "master_cutoff"

ROOMS = {
    "zone1": 0,
    "zone2": 1,
    "zone3": 2,
    "zone4": 3,
    "zone5": 4,
}

BahaHeaterSwitch = baha_rs485_ns.class_(
    "BahaHeaterSwitch", switch.Switch, cg.Parented.template(BahaRS485Component)
)
BahaLightSwitch = baha_rs485_ns.class_(
    "BahaLightSwitch", switch.Switch, cg.Parented.template(BahaRS485Component)
)
BahaMasterSwitch = baha_rs485_ns.class_(
    "BahaMasterSwitch", switch.Switch, cg.Parented.template(BahaRS485Component)
)

CONFIG_SCHEMA = cv.typed_schema(
    {
        TYPE_HEATER: switch.switch_schema(
            BahaHeaterSwitch,
            icon=ICON_RADIATOR,
            device_class=DEVICE_CLASS_SWITCH,
            default_restore_mode="DISABLED",
        ).extend(
            {
                cv.GenerateID(CONF_BAHA_RS485_ID): cv.use_id(BahaRS485Component),
                cv.Required(CONF_ROOM): cv.enum(ROOMS, lower=True),
                cv.Optional(CONF_ON_DELTA, default=1): cv.int_range(min=0, max=5),
                cv.Optional(CONF_OFF_DELTA, default=-2): cv.int_range(min=-5, max=0),
            }
        ),
        TYPE_LIGHT: switch.switch_schema(
            BahaLightSwitch,
            icon=ICON_LIGHTBULB,
            device_class=DEVICE_CLASS_SWITCH,
            default_restore_mode="DISABLED",
        ).extend(
            {
                cv.GenerateID(CONF_BAHA_RS485_ID): cv.use_id(BahaRS485Component),
                cv.Required(CONF_CHANNEL): cv.int_range(min=1, max=4),
            }
        ),
        TYPE_MASTER: switch.switch_schema(
            BahaMasterSwitch,
            icon=ICON_POWER,
            device_class=DEVICE_CLASS_SWITCH,
            default_restore_mode="DISABLED",
        ).extend(
            {
                cv.GenerateID(CONF_BAHA_RS485_ID): cv.use_id(BahaRS485Component),
            }
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_BAHA_RS485_ID])

    if config[CONF_TYPE] == TYPE_HEATER:
        var = await switch.new_switch(config)
        await cg.register_parented(var, config[CONF_BAHA_RS485_ID])
        cg.add(var.set_room(config[CONF_ROOM]))
        cg.add(var.set_on_delta(config[CONF_ON_DELTA]))
        cg.add(var.set_off_delta(config[CONF_OFF_DELTA]))
        cg.add(parent.register_heater_switch(var))
        return

    if config[CONF_TYPE] == TYPE_LIGHT:
        var = await switch.new_switch(config)
        await cg.register_parented(var, config[CONF_BAHA_RS485_ID])
        cg.add(var.set_channel(config[CONF_CHANNEL]))
        cg.add(parent.register_light_switch(var))
        return

    var = await switch.new_switch(config)
    await cg.register_parented(var, config[CONF_BAHA_RS485_ID])
    cg.add(parent.register_master_switch(var))
