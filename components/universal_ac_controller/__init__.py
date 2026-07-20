from pathlib import Path

import esphome.codegen as cg
from esphome.components import remote_transmitter, select, switch, text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = [
    "irext_adapter",
    "remote_transmitter",
    "select",
    "shared",
    "switch",
    "text_sensor",
]

CONF_TRANSMITTER_ID = "transmitter_id"
CONF_TYPE_CONTROL = "type_control"
CONF_BRAND_CONTROL = "brand_control"
CONF_MODEL_CONTROL = "model_control"
CONF_FAN_CONTROL = "fan_control"
CONF_SWING_CONTROL = "swing_control"
CONF_POWER_CONTROL = "power_control"
CONF_STATUS_CONTROL = "status_control"

universal_ac_controller_ns = cg.esphome_ns.namespace("universal_ac_controller")
UniversalAcController = universal_ac_controller_ns.class_(
    "UniversalAcController", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(UniversalAcController),
        cv.Required(CONF_TRANSMITTER_ID): cv.use_id(
            remote_transmitter.RemoteTransmitterComponent
        ),
        cv.Required(CONF_TYPE_CONTROL): cv.use_id(select.Select),
        cv.Required(CONF_BRAND_CONTROL): cv.use_id(select.Select),
        cv.Required(CONF_MODEL_CONTROL): cv.use_id(select.Select),
        cv.Required(CONF_FAN_CONTROL): cv.use_id(select.Select),
        cv.Required(CONF_SWING_CONTROL): cv.use_id(select.Select),
        cv.Required(CONF_POWER_CONTROL): cv.use_id(switch.Switch),
        cv.Required(CONF_STATUS_CONTROL): cv.use_id(text_sensor.TextSensor),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    irext_core = Path(__file__).resolve().parents[2] / "vendor" / "irext_core"
    cg.add_library("irext-core", None, repository=f"file://{irext_core.as_posix()}")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    transmitter = await cg.get_variable(config[CONF_TRANSMITTER_ID])
    type_control = await cg.get_variable(config[CONF_TYPE_CONTROL])
    brand_control = await cg.get_variable(config[CONF_BRAND_CONTROL])
    model_control = await cg.get_variable(config[CONF_MODEL_CONTROL])
    fan_control = await cg.get_variable(config[CONF_FAN_CONTROL])
    swing_control = await cg.get_variable(config[CONF_SWING_CONTROL])
    power_control = await cg.get_variable(config[CONF_POWER_CONTROL])
    status_control = await cg.get_variable(config[CONF_STATUS_CONTROL])

    cg.add(var.set_transmitter(transmitter))
    cg.add(var.set_type_control(type_control))
    cg.add(var.set_brand_control(brand_control))
    cg.add(var.set_model_control(model_control))
    cg.add(var.set_fan_control(fan_control))
    cg.add(var.set_swing_control(swing_control))
    cg.add(var.set_power_control(power_control))
    cg.add(var.set_status_control(status_control))