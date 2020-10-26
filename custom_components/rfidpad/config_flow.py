import logging

import voluptuous as vol

_LOGGER = logging.getLogger(__name__)

class RFIDPadConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    async def async_step_user(self, info):
        _LOGGER.info("async_step_user: " + str(info))

        await self.async_set_unique_id(device_unique_id)
        self._abort_if_unique_id_configured()
        return self.async_show_form(
            step_id="user", data_schema=vol.Schema({vol.Required("password"): str})
        )


    async def async_step_mqtt(self, info):
        _LOGGER.info("async_step_mqtt: " + str(info))

        await self.async_set_unique_id(device_unique_id)
        self._abort_if_unique_id_configured()
        return self.async_show_form(
            step_id="mqtt", data_schema=vol.Schema({vol.Required("password"): str})
        )



    @staticmethod
    @callback
    def async_get_options_flow(config_entry):
        return RFIDPadOptionsFlowHandler(config_entry)
