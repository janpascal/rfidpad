"""
Custom integration to integrate rfidpad with Home Assistant.

For more details about this integration, please refer to
https://github.com/janpascal/rfidpad
"""
import asyncio
from datetime import timedelta
import logging

from homeassistant.components import mqtt
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import Config, HomeAssistant, callback
from homeassistant.exceptions import ConfigEntryNotReady
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from custom_components.rfidpad.const import (
    CONF_PASSWORD,
    CONF_USERNAME,
    CONF_MQTT_PREFIX,
    DOMAIN,
    PLATFORMS,
    STARTUP_MESSAGE,
)

SCAN_INTERVAL = timedelta(seconds=30)

_LOGGER = logging.getLogger(__name__)


async def async_setup(hass: HomeAssistant, config: Config):
    """Set up this integration using YAML is not supported."""
    if "mqtt" not in hass.config.components:
        _LOGGER.error("MQTT integration is not set up")
        return False
    hass.data[DOMAIN] = {}
    return True

@callback
def async_receive_discovery(msg):
    _LOGGER.info(f"Received discovery message: {msg}")

async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry):
    """Set up this integration using UI."""
    if hass.data.get(DOMAIN) is None:
        hass.data.setdefault(DOMAIN, {})
        _LOGGER.info(STARTUP_MESSAGE)

        
    _LOGGER.info("async_setup_entry with config: {}".format(entry))

    mqtt_prefix = entry.data.get(CONF_MQTT_PREFIX)
    await mqtt.async_subscribe(
                            hass, f"{mqtt_prefix}/discovery/#", async_receive_discovery)



##    coordinator = BlueprintDataUpdateCoordinator(
##        hass, username=username, password=password
##    )
##    await coordinator.async_refresh()
##
##    if not coordinator.last_update_success:
##        raise ConfigEntryNotReady
##
##    hass.data[DOMAIN][entry.entry_id] = coordinator

##    for platform in PLATFORMS:
##        if entry.options.get(platform, True):
##            coordinator.platforms.append(platform)
##            hass.async_add_job(
##                hass.config_entries.async_forward_entry_setup(entry, platform)
##            )
##
##    entry.add_update_listener(async_reload_entry)
    return True


##class BlueprintDataUpdateCoordinator(DataUpdateCoordinator):
##    """Class to manage fetching data from the API."""
##
##    def __init__(self, hass, username, password):
##        """Initialize."""
##        self.api = Client(username, password)
##        self.platforms = []
##
##        super().__init__(hass, _LOGGER, name=DOMAIN, update_interval=SCAN_INTERVAL)
##
##    async def _async_update_data(self):
##        """Update data via library."""
##        try:
##            data = await self.api.async_get_data()
##            return data.get("data", {})
##        except Exception as exception:
##            raise UpdateFailed(exception)

##
##async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry):
##    """Handle removal of an entry."""
##    coordinator = hass.data[DOMAIN][entry.entry_id]
##    unloaded = all(
##        await asyncio.gather(
##            *[
##                hass.config_entries.async_forward_entry_unload(entry, platform)
##                for platform in PLATFORMS
##                if platform in coordinator.platforms
##            ]
##        )
##    )
##    if unloaded:
##        hass.data[DOMAIN].pop(entry.entry_id)
##
##    return unloaded
##
##
##async def async_reload_entry(hass: HomeAssistant, entry: ConfigEntry):
##    """Reload config entry."""
##    await async_unload_entry(hass, entry)
##    await async_setup_entry(hass, entry)

