import logging

from homeassistant import config_entries
from homeassistant.core import callback
from .const import ( 
    DOMAIN, 
    PLATFORMS, 
    CONF_MQTT_PREFIX, 
    DEFAULT_MQTT_PREFIX 
)


import voluptuous as vol

_LOGGER = logging.getLogger(__name__)

@config_entries.HANDLERS.register(DOMAIN)
class RFIDPadConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    async def async_step_user(self, info):
        _LOGGER.info("async_step_user: " + str(info))

        # Only a single instance of the integration is allowed:
        if self._async_current_entries():
            return self.async_abort(reason="single_instance_allowed")

        if info is not None:
            return self.async_create_entry(
                title=info[CONF_MQTT_PREFIX], data=info
            )

        return self.async_show_form(
            step_id="user", data_schema=vol.Schema({vol.Optional(CONF_MQTT_PREFIX, default=DEFAULT_MQTT_PREFIX): str})
        )


    async def async_step_mqtt(self, info):
        _LOGGER.info("async_step_mqtt: " + str(info))

        return self.async_show_form(
            step_id="mqtt", data_schema=vol.Schema({vol.Required("MQTT topic prefix"): str})
        )



    @staticmethod
    @callback
    def async_get_options_flow(config_entry):
        return RFIDPadOptionsFlowHandler(config_entry)


class RFIDPadOptionsFlowHandler(config_entries.OptionsFlow):
    """RFIDPad config flow options handler."""

    def __init__(self, config_entry):
        """Initialize HACS options flow."""
        self.config_entry = config_entry
        _LOGGER.info("Options init with {}".format(config_entry))
        self.options = dict(config_entry.options)

    async def async_step_init(self, user_input=None):  # pylint: disable=unused-argument
        """Manage the options."""
        return await self.async_step_user()

    async def async_step_user(self, user_input=None):
        """Handle a flow initialized by the user."""
        _LOGGER.info("Options step user with {}".format(user_input))
        if user_input is not None:
            self.options.update(user_input)
            return await self._update_options()

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Optional(CONF_MQTT_PREFIX, default=self.options.get(CONF_MQTT_PREFIX, DEFAULT_MQTT_PREFIX)): str
                }
            ),
        )

    async def _update_options(self):
        """Update config entry options."""
        return self.async_create_entry(
            title=self.config_entry.data.get(CONF_MQTT_PREFIX), data=self.options
        )
