"""Platform for sensor integration."""
import logging

from homeassistant.const import ATTR_VOLTAGE, DEVICE_CLASS_BATTERY, PERCENTAGE
from homeassistant.helpers.entity import Entity

from .const import DOMAIN, SENSOR, PLATFORMS

_LOGGER = logging.getLogger(__name__)

def setup_platform(hass, config, add_entities, discovery_info=None):
    """Set up the RFIDPad platform."""
    _LOGGER.info(f"setup_platform for RFIDPadSensor: {config.data} ({config})")
    add_entities([RFIDPadSensor()])

async def async_setup_entry(hass, config_entry, async_add_devices):
    """Add sensors for passed config_entry in HA."""
    handler = hass.data[DOMAIN][config_entry.entry_id]
    _LOGGER.info(f"async_setup_entry() for RFIDPadSensor: {config_entry.data}")

    handler.async_add_devices[SENSOR] = async_add_devices

    if len(handler.async_add_devices) < len(PLATFORMS):
        _LOGGER.debug("Not all platforms initialised, not starting discovery!")
        return

    await handler.start_discovery()

class BatterySensor(Entity):
    """Representation of a Sensor."""

    def __init__(self, hass, device):
        """Initialize the sensor."""
        _LOGGER.info(f"Battery sensor has hass: {hass}")
        self.hass = hass
        self._device = device
        self._state = None

    @property
    def unique_id(self):
        return f"{DOMAIN}_{self._device.id}_bat"

    @property
    def name(self):
        """Return the name of the sensor."""
        return f"{self._device.name} Battery" 

    @property
    def device_class(self):
        return DEVICE_CLASS_BATTERY

    @property
    def device_info(self):
        return {
            "identifiers": {(DOMAIN, self._device.id)},
            "name": self._device.name,
            "model": self._device.model,
            "manufacturer": self._device.manufacturer,
            "sw_version": self._device.sw_version,
        }

    @property
    def state(self):
        """Return the state of the sensor."""
        return self._device.battery_level

    @property
    def should_poll(self):
        """No need to poll, rfidpad will push state over MQTT."""
        return False

    @property
    def unit_of_measurement(self):
        """Return the unit of measurement."""
        return PERCENTAGE

    @property
    def device_state_attributes(self):
        """Return the state attributes of the sensor."""
        attr = {}

        attr[ATTR_VOLTAGE] = self._device.battery_voltage

        return attr

