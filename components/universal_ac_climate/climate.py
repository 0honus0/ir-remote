import esphome.codegen as cg
from esphome.components import climate, universal_ac_controller
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["universal_ac_controller"]
DEPENDENCIES = ["universal_ac_controller"]
CONF_CONTROLLER = "controller"

universal_ac_climate_ns = cg.esphome_ns.namespace("universal_ac_climate")
UniversalAcClimate = universal_ac_climate_ns.class_(
    "UniversalAcClimate", climate.Climate, cg.Component
)

CONFIG_SCHEMA = climate.climate_schema(UniversalAcClimate).extend(
    {
        cv.Required(CONF_CONTROLLER): cv.use_id(
            universal_ac_controller.UniversalAcController
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    controller = await cg.get_variable(config[CONF_CONTROLLER])
    cg.add(var.set_controller(controller))
