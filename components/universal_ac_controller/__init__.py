import esphome.codegen as cg
from esphome.components import number, select, switch, text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID

CONF_PROTOCOL_CONTROL = "protocol_control"
CONF_FAN_CONTROL = "fan_control"
CONF_SWING_CONTROL = "swing_control"
CONF_SPECIAL_CONTROL = "special_control"
CONF_TIMER_CONTROL = "timer_control"
CONF_LIGHT_CONTROL = "light_control"
CONF_POWER_CONTROL = "power_control"
CONF_STATUS_CONTROL = "status_control"

universal_ac_controller_ns = cg.esphome_ns.namespace("universal_ac_controller")
UniversalAcController = universal_ac_controller_ns.class_(
    "UniversalAcController", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(UniversalAcController),
        cv.Required(CONF_PROTOCOL_CONTROL): cv.use_id(select.Select),
        cv.Required(CONF_FAN_CONTROL): cv.use_id(select.Select),
        cv.Required(CONF_SWING_CONTROL): cv.use_id(select.Select),
        cv.Required(CONF_SPECIAL_CONTROL): cv.use_id(select.Select),
        cv.Required(CONF_TIMER_CONTROL): cv.use_id(number.Number),
        cv.Required(CONF_LIGHT_CONTROL): cv.use_id(switch.Switch),
        cv.Required(CONF_POWER_CONTROL): cv.use_id(switch.Switch),
        cv.Required(CONF_STATUS_CONTROL): cv.use_id(text_sensor.TextSensor),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    protocol_control = await cg.get_variable(config[CONF_PROTOCOL_CONTROL])
    fan_control = await cg.get_variable(config[CONF_FAN_CONTROL])
    swing_control = await cg.get_variable(config[CONF_SWING_CONTROL])
    special_control = await cg.get_variable(config[CONF_SPECIAL_CONTROL])
    timer_control = await cg.get_variable(config[CONF_TIMER_CONTROL])
    light_control = await cg.get_variable(config[CONF_LIGHT_CONTROL])
    power_control = await cg.get_variable(config[CONF_POWER_CONTROL])
    status_control = await cg.get_variable(config[CONF_STATUS_CONTROL])

    cg.add(var.set_protocol_control(protocol_control))
    cg.add(var.set_fan_control(fan_control))
    cg.add(var.set_swing_control(swing_control))
    cg.add(var.set_special_control(special_control))
    cg.add(var.set_timer_control(timer_control))
    cg.add(var.set_light_control(light_control))
    cg.add(var.set_power_control(power_control))
    cg.add(var.set_status_control(status_control))
