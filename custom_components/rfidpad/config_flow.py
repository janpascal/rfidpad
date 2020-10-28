import logging

from homeassistant import config_entries
from homeassistant.core import callback
from .const import ( DOMAIN, PLATFORMS )

import voluptuous as vol

_LOGGER = logging.getLogger(__name__)

class RFIDPadConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    async def async_step_user(self, info):
        _LOGGER.info("async_step_user: " + str(info))

        await self.async_set_unique_id(device_unique_id)
        self._abort_if_unique_id_configured()
        return self.async_show_form(
            step_id="user", data_schema=vol.Schema({vol.Required("User password"): str})
        )


    async def async_step_mqtt(self, info):
        _LOGGER.info("async_step_mqtt: " + str(info))

        await self.async_set_unique_id(device_unique_id)
        self._abort_if_unique_id_configured()
        return self.async_show_form(
            step_id="mqtt", data_schema=vol.Schema({vol.Required("MQTT password"): str})
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
        self.options = dict(config_entry.options)

    async def async_step_init(self, user_input=None):  # pylint: disable=unused-argument
        """Manage the options."""
        return await self.async_step_user()

    async def async_step_user(self, user_input=None):
        """Handle a flow initialized by the user."""
        if user_input is not None:
            self.options.update(user_input)
            return await self._update_options()

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required(x, default=self.options.get(x, True)): bool
                    for x in sorted(PLATFORMS)
                }
            ),
        )

    async def _update_options(self):
        """Update config entry options."""
        return self.async_create_entry(
            title=self.config_entry.data.get(CONF_USERNAME), data=self.options
        )
