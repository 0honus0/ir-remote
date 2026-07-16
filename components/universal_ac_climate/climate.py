import esphome.codegen as cg
from esphome.components import climate, number, select, switch
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["climate"]
CONF_TEMPERATURE_STORAGE = "temperature_storage"
CONF_MODE_STORAGE = "mode_storage"
CONF_FAN_STORAGE = "fan_storage"
CONF_SWING_STORAGE = "swing_storage"
CONF_SPECIAL_STORAGE = "special_storage"
CONF_POWER_STORAGE = "power_storage"
CONF_TIMER_STORAGE = "timer_storage"

universal_ac_climate_ns = cg.esphome_ns.namespace("universal_ac_climate")
UniversalAcClimate = universal_ac_climate_ns.class_(
    "UniversalAcClimate", climate.Climate, cg.PollingComponent
)

CONFIG_SCHEMA = climate.climate_schema(UniversalAcClimate).extend(
    {
        cv.Required(CONF_TEMPERATURE_STORAGE): cv.use_id(number.Number),
        cv.Required(CONF_MODE_STORAGE): cv.use_id(select.Select),
        cv.Required(CONF_FAN_STORAGE): cv.use_id(select.Select),
        cv.Required(CONF_SWING_STORAGE): cv.use_id(select.Select),
        cv.Required(CONF_SPECIAL_STORAGE): cv.use_id(select.Select),
        cv.Required(CONF_POWER_STORAGE): cv.use_id(switch.Switch),
        cv.Required(CONF_TIMER_STORAGE): cv.use_id(number.Number),
    }
).extend(
    cv.polling_component_schema("1s")
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    temperature_storage = await cg.get_variable(config[CONF_TEMPERATURE_STORAGE])
    mode_storage = await cg.get_variable(config[CONF_MODE_STORAGE])
    fan_storage = await cg.get_variable(config[CONF_FAN_STORAGE])
    swing_storage = await cg.get_variable(config[CONF_SWING_STORAGE])
    special_storage = await cg.get_variable(config[CONF_SPECIAL_STORAGE])
    power_storage = await cg.get_variable(config[CONF_POWER_STORAGE])
    timer_storage = await cg.get_variable(config[CONF_TIMER_STORAGE])
    cg.add(var.set_temperature_storage(temperature_storage))
    cg.add(var.set_mode_storage(mode_storage))
    cg.add(var.set_fan_storage(fan_storage))
    cg.add(var.set_swing_storage(swing_storage))
    cg.add(var.set_special_storage(special_storage))
    cg.add(var.set_power_storage(power_storage))
    cg.add(var.set_timer_storage(timer_storage))
