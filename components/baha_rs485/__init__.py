import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = []
DEPENDENCIES = ["uart"]

CONF_STALE_TIMEOUT = "stale_timeout"
CONF_STARTUP_LISTEN_WINDOW = "startup_listen_window"
CONF_POLL_RETRY_INTERVAL = "poll_retry_interval"
CONF_IDLE_BEFORE_TX = "idle_before_tx"
CONF_INTER_FRAME_GAP = "inter_frame_gap"
CONF_POST_WRITE_READBACK = "post_write_readback"
CONF_BAHA_RS485_ID = "baha_rs485_id"

baha_rs485_ns = cg.esphome_ns.namespace("baha_rs485")
BahaRS485Component = baha_rs485_ns.class_(
    "BahaRS485Component", cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BahaRS485Component),
            cv.Optional(CONF_STALE_TIMEOUT, default="30s"): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_STARTUP_LISTEN_WINDOW, default="10s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_POLL_RETRY_INTERVAL, default="5s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_IDLE_BEFORE_TX, default="80ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_INTER_FRAME_GAP, default="120ms"): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_POST_WRITE_READBACK, default="400ms"
            ): cv.positive_time_period_milliseconds,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "baha_rs485",
    baud_rate=9600,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="NONE",
    stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_stale_timeout(config[CONF_STALE_TIMEOUT].total_milliseconds))
    cg.add(
        var.set_startup_listen_window(
            config[CONF_STARTUP_LISTEN_WINDOW].total_milliseconds
        )
    )
    cg.add(
        var.set_poll_retry_interval(
            config[CONF_POLL_RETRY_INTERVAL].total_milliseconds
        )
    )
    cg.add(var.set_idle_before_tx(config[CONF_IDLE_BEFORE_TX].total_milliseconds))
    cg.add(var.set_inter_frame_gap(config[CONF_INTER_FRAME_GAP].total_milliseconds))
    cg.add(
        var.set_post_write_readback(
            config[CONF_POST_WRITE_READBACK].total_milliseconds
        )
    )
