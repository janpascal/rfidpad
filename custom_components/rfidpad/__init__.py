"""
Custom integration to integrate rfidpad with Home Assistant.

For more details about this integration, please refer to
https://github.com/janpascal/rfidpad
"""
import asyncio
from datetime import timedelta
import json
import logging

from homeassistant.components import mqtt
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import Config, HomeAssistant, callback
from homeassistant.exceptions import ConfigEntryNotReady

from .const import (
    CONF_PASSWORD,
    CONF_USERNAME,
    CONF_MQTT_PREFIX,
    DOMAIN,
    BINARY_SENSOR,
    SENSOR,
    SWITCH,
    PLATFORMS,
    STARTUP_MESSAGE,
    DEVICE_CONF_ID,
    DEVICE_CONF_NAME,
    DEVICE_CONF_MODEL,
    DEVICE_CONF_MANUFACTURER,
    DEVICE_CONF_SW_VERSION,
    DEVICE_CONF_BASE_TOPIC,
    DEVICE_CONF_ACTION_TOPIC,
    DEVICE_CONF_STATUS_TOPIC,
    DEVICE_CONF_BATTERY_TOPIC,
    DEFAULT_MODEL,
    DEFAULT_MANUFACTURER,
    DEFAULT_SW_VERSION,
    DEFAULT_STATUS_TOPIC,
    DEFAULT_ACTION_TOPIC,
    DEFAULT_BATTERY_TOPIC,
)

from .sensor import BatterySensor

SCAN_INTERVAL = timedelta(seconds=30)

_LOGGER = logging.getLogger(__name__)


async def async_setup(hass: HomeAssistant, config: Config):
    """Set up this integration using YAML is not supported."""
    if "mqtt" not in hass.config.components:
        _LOGGER.error("MQTT integration is not set up")
        return False
    hass.data[DOMAIN] = {}
    return True

async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry):
    """Set up this integration using UI."""
    if hass.data.get(DOMAIN) is None:
        hass.data.setdefault(DOMAIN, {})
        _LOGGER.info(STARTUP_MESSAGE)

        
    _LOGGER.info("async_setup_entry with entry: {}".format(entry))
    _LOGGER.info(f"entry_id: {entry.entry_id}; data: {entry.data}")

    mqtt_prefix = entry.data.get(CONF_MQTT_PREFIX)
    handler = RFIDPadHandler(hass, entry, mqtt_prefix)
    hass.data[DOMAIN][entry.entry_id] = handler

    for platform in PLATFORMS:
        #coordinator.platforms.append(platform)
        hass.async_add_job(
            hass.config_entries.async_forward_entry_setup(entry, platform)
        )

    #entry.add_update_listener(async_reload_entry)
    return True


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

class RFIDPadHandler:
    def __init__(self, hass, config_entry, mqtt_prefix):
        self.hass = hass
        self.config_entry = config_entry
        self.mqtt_prefix = mqtt_prefix
        self.async_add_devices = {}

    async def start_discovery(self):
        topic_filter = f"{self.mqtt_prefix}/discovery/#"
        _LOGGER.info(f"Subscribing to MQTT filter {topic_filter}")
        await mqtt.async_subscribe(self.hass, f"{self.mqtt_prefix}/discovery/#", self.async_receive_discovery)

    async def async_receive_discovery(self, msg):
        _LOGGER.info(f"Received discovery message: {msg} ({type(msg)})")
        async_add_devices_sensor = self.async_add_devices[SENSOR]

        try:
            config = json.loads(msg.payload)
        except:
            _LOGGER.info(f"Cannot parse rfidpad discovery message: {msg.payload}")

        pad = RFIDPad(self.hass, self, config)

        await pad.start()

    @property
    def pads(self):
        return [RFIDPad(), ]

class RFIDPad:
    def __init__(self, hass, handler, config):
        self.hass = hass
        self.handler = handler
        self.id = config[DEVICE_CONF_ID]
        self.name = config[DEVICE_CONF_NAME]
        try:
            self.model = config[DEVICE_CONF_MODEL]
        except:
            self.model = DEFAULT_MODEL

        try:
            self.manufacturer = config[DEVICE_CONF_MANUFACTURER]
        except:
            self.manufacturer = DEFAULT_MANUFACTURER

        try:
            self.sw_version = config[DEVICE_CONF_SW_VERSION]
        except:
            self.sw_version = "0.0"

        try:
            self.base_topic = config[DEVICE_CONF_BASE_TOPIC]
        except:
            self.base_topic = f"{self.handler.mqtt_prefix}/{self.id}"

        try: 
            self.status_topic = f"{self.base_topic}/{config[DEVICE_CONF_STATUS_TOPIC]}" 
        except:
            self.status_topic = f"{self.base_topic}/{DEFAULT_STATUS_TOPIC}" 

        try: 
            self.action_topic = f"{self.base_topic}/{config[DEVICE_CONF_ACTION_TOPIC]}" 
        except:
            self.action_topic = f"{self.base_topic}/{DEFAULT_ACTION_TOPIC}" 
        try: 
            self.battery_topic = f"{self.base_topic}/{config[DEVICE_CONF_BATTERY_TOPIC]}" 
        except:
            self.battery_topic = f"{self.base_topic}/{DEFAULT_BATTERY_TOPIC}" 

        self.battery_level = None
        self.battery_voltage = None

    async def start(self):
        _LOGGER.info(f"Subscribing to rfidpad action: {self.action_topic}")

        self.battery_sensor = BatterySensor(self.hass, self)
        new_devices = [self.battery_sensor,]
        _LOGGER.info(f"Adding {len(new_devices)} entities")
        self.handler.async_add_devices[SENSOR](new_devices)

        await mqtt.async_subscribe(self.hass, self.action_topic, self.async_receive_action)
        await mqtt.async_subscribe(self.hass, self.battery_topic, self.async_receive_battery)

    async def async_receive_action(self, msg):
        _LOGGER.info(f"Received action message: {msg}")

    async def async_receive_battery(self, msg):
        _LOGGER.info(f"Received battery message: {msg}")
        try:
            message = json.loads(msg.payload)
        except:
            _LOGGER.info(f"Cannot parse rfidpad battery message: {msg.payload}")
        self.battery_level = message["level"]
        self.battery_voltage = message["voltage"]

        await self.battery_sensor.async_update_ha_state()

