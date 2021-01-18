"""
Custom integration to integrate rfidpad with Home Assistant.

For more details about this integration, please refer to
https://github.com/janpascal/rfidpad
"""
import asyncio
from datetime import timedelta
import json
import logging

import voluptuous as vol
from homeassistant.components import mqtt, websocket_api
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import Config, HomeAssistant, callback
from homeassistant.exceptions import ConfigEntryNotReady
import homeassistant.helpers.config_validation as cv
from homeassistant.helpers.storage import Store
from homeassistant.helpers.typing import ConfigType, HomeAssistantType
import homeassistant.util.dt as dt_util

from homeassistant.const import (
        CONF_NAME,
)
from .const import (
    CONF_MQTT_PREFIX,
    CONF_TAGS,
    CONF_TAG,
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
    STATUS_TRANSITIONS,
    STORAGE_KEY,
    STORAGE_VERSION,
    SAVE_DELAY,
    MAX_HISTORY,
    HISTORY_UPDATED_EVENT,
    TAG_SCANNED_EVENT,
)

from .config_flow import RFIDPadConfigFlow
from .sensor import BatterySensor, LastTagSensor

SCAN_INTERVAL = timedelta(seconds=30)

_LOGGER = logging.getLogger(__name__)

TAG_SCHEMA = vol.All(cv.string, cv.matches_regex(r'([0-9a-fA-F][0-9a-fA-F]){1,}'))


CONFIG_SCHEMA = vol.Schema({
    DOMAIN: vol.Schema({
        vol.Optional(CONF_TAGS): vol.Schema([
            vol.Schema({
                vol.Required(CONF_NAME): cv.string,
                vol.Required(CONF_TAG): TAG_SCHEMA
            })
        ])
    }),
}, extra = vol.ALLOW_EXTRA)

async def async_setup(hass: HomeAssistant, config: Config):
    """Set up this integration using YAML is not supported."""
    if "mqtt" not in hass.config.components:
        _LOGGER.error("MQTT integration is not set up")
        return False
    hass.data[DOMAIN] = {}
    hass.data[DOMAIN][CONF_TAGS] = {}

    if DOMAIN not in config:
        _LOGGER.error(f"{DOMAIN} not configured in configuration.yaml, no tags will be recognized!")
        return True

    conf = config[DOMAIN]
    _LOGGER.debug(f"Setting up rfidpad integration with conf: {conf}")

    if CONF_TAGS not in conf:
        _LOGGER.error(f"{DOMAIN}.{CONF_TAGS} not configured in configuration.yaml, no tags will be recognized!")
        return True

    tags = conf[CONF_TAGS]

    tag_dict = { item[CONF_TAG].upper():item[CONF_NAME] for item in tags }
    _LOGGER.debug(f"Tags enabled for {DOMAIN}: {tag_dict}")

    hass.data[DOMAIN][CONF_TAGS] = tag_dict

   # Register websocket API
    websocket_api.async_register_command(hass, ws_tag_history)

    # Return boolean to indicate that initialization was successfully.
    return True


@websocket_api.websocket_command({
    vol.Required('type'): 'rfidpad/tag_history',
})
def ws_tag_history(hass: HomeAssistantType,
                   connection: websocket_api.ActiveConnection, msg):
    """History of usage of user code tags."""
    manager = hass.data[DOMAIN]  # type: RFIDPadHandler
    connection.send_result(msg['id'], {
        'history': list(reversed(manager._history)),
    })
    

    return True

async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry):
    """Set up this integration using UI."""
    if hass.data.get(DOMAIN) is None:
        hass.data.setdefault(DOMAIN, {})
        _LOGGER.info(STARTUP_MESSAGE)

        
    _LOGGER.debug("async_setup_entry with entry: {}".format(entry))
    _LOGGER.debug(f"entry_id: {entry.entry_id}; version: {entry.version}; data: {entry.data}")

    mqtt_prefix = entry.data.get(CONF_MQTT_PREFIX)
    handler = RFIDPadHandler(hass, entry, mqtt_prefix)
    await handler.async_initialize()
    hass.data[DOMAIN][entry.entry_id] = handler

    for platform in PLATFORMS:
        hass.async_add_job(
            hass.config_entries.async_forward_entry_setup(entry, platform)
        )

    def handle_update_status(call):
        """Handle the service call."""
        _LOGGER.debug(f"rfidpad service call: {call.data}")
        new_status = call.data.get("new_status", "")
        if new_status == '':
            _LOGGER.warning(f"Received illegal data in service rfidpad.update_status: {call.data}")
            return

        hass.async_add_job(handler.async_update_status(new_status))


    hass.services.async_register(DOMAIN, "update_status", handle_update_status)

    # TODO
    #entry.add_update_listener(async_reload_entry)
    return True


# Migration function
async def async_migrate_entry(hass, config_entry: ConfigEntry):
    """Migrate old entry."""
    _LOGGER.debug("Migrating config_entry from version %s", config_entry.version)

    if config_entry.version is None or config_entry.version == 1:

        new = {**config_entry.data}

        # modify config data, i.c. change config tag
        if CONF_MQTT_PREFIX not in new:
            new[CONF_MQTT_PREFIX] = new["MQTT prefix"]
            del new["MQTT prefix"]

        config_entry.data = {**new}

        config_entry.version = 2

    _LOGGER.info("Migration to version %s successful", config_entry.version)

    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry):
    """Handle removal of an entry."""
    handler = hass.data[DOMAIN].pop(entry.entry_id)

    _LOGGER.debug(f"Unloading RFIDPad for {handler.mqtt_prefix}")

    unloaded = all(
        await asyncio.gather(
            *[
                hass.config_entries.async_forward_entry_unload(entry, platform)
                for platform in PLATFORMS
            ]
        )
    )

    return unloaded


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
        self.devices = {}
        self.store = Store(hass, STORAGE_VERSION, STORAGE_KEY)
        self._history = []

    async def async_initialize(self):
        try:
            raw_storage = await self.store.async_load()
            self._history = raw_storage["history"]
        except:
            self._history = []

        _LOGGER.debug(f"Initial history: {self._history}")


    @callback
    def _async_schedule_save(self) -> None:
        """Schedule saving the history and tag data"""
        self.store.async_delay_save(self._data_to_save, SAVE_DELAY)

    @callback
    def _data_to_save(self) -> dict:
        """Return history and tag data store in a file."""
        return {
            'history': self._history[-MAX_HISTORY:],
        }


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
            return

        pad = RFIDPad(self.hass, self, config)
        if pad.id in self.devices:
            _LOGGER.info(f"Ignoring RFIDPAD {pad.id} ({pad.name}), id already exists")
            # TODO update device configuration and restart device
        else:
            self.devices[pad.id] = pad
            await pad.start()

    async def async_handle_action(self, action):
        """ Called by an RFIDPad when a tag has been scanned """

        self._history.append(action.as_dict())
        _LOGGER.debug(f"Current history: {self._history}")
        self.hass.bus.fire(HISTORY_UPDATED_EVENT, {})
        self._async_schedule_save()

        if not action.tag_valid:
            _LOGGER.warn(f"unknown tag {action.tag} scanned")
            return

        # Fire event, for use by automations
        self.hass.bus.fire(TAG_SCANNED_EVENT, {"button": action.button, "tag":
            action.tag, "name": action.tag_name})

        # Send new status to all boards
        new_status = STATUS_TRANSITIONS[action.button]
        if new_status != None:
            for device in self.devices.values():
                await device.async_update_status(new_status)

class RFIDAction:
    def __init__(self, pad, button, tag, timestamp=None):
        self.pad = pad
        self.button = button
        self.tag = tag
        self.timestamp = timestamp
        if timestamp is None:
            self.timestamp = dt_util.now()

        self.allowed_tags = pad.allowed_tags

    @property
    def tag_valid(self):
        return self.tag in self.allowed_tags

    @property
    def tag_name(self):
        return self.allowed_tags.get(self.tag, "")

    def as_dict(self):
        return {
            "pad": self.pad.name,
            "button": self.button,
            "tag": self.tag,
            "tag_name": self.tag_name,
            "tag_valid": self.tag_valid,
            "timestamp": dt_util.as_timestamp(self.timestamp),
            "date": self.timestamp.strftime("%d/%m/%Y %H:%M:%S"),
        }


class RFIDPad:
    def __init__(self, hass, handler, config):
        self.hass = hass
        self.handler = handler
        self.allowed_tags = hass.data[DOMAIN][CONF_TAGS]
        _LOGGER.info(f"Allowed tags: {self.allowed_tags}")
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

        self.last_action = None

    async def start(self):
        _LOGGER.info(f"Subscribing to rfidpad action: {self.action_topic}")

        self.battery_sensor = BatterySensor(self.hass, self)
        self.last_tag_sensor = LastTagSensor(self.hass, self)
        new_devices = [self.battery_sensor, self.last_tag_sensor]

        _LOGGER.info(f"Adding {len(new_devices)} entities")
        self.handler.async_add_devices[SENSOR](new_devices)

        await mqtt.async_subscribe(self.hass, self.action_topic, self.async_receive_action)
        await mqtt.async_subscribe(self.hass, self.battery_topic, self.async_receive_battery)

    async def async_receive_action(self, msg):
        _LOGGER.info(f"Received action message: {msg}")
        try:
            message = json.loads(msg.payload)
        except:
            _LOGGER.info(f"Cannot parse rfidpad battery message: {msg.payload}")
            return

        try:
            button = message["button"]
            tag = message["tag"].upper()
        except:
            _LOGGER.info(f"Action message from board does not contain 'button' and 'message' tags: {msg.payload}")
            return

        action = RFIDAction(self, button, tag)
        self.last_action = action

        await self.handler.async_handle_action(action)

        await self.last_tag_sensor.async_update_ha_state()

    async def async_receive_battery(self, msg):
        _LOGGER.info(f"Received battery message: {msg}")
        try:
            message = json.loads(msg.payload)
        except:
            _LOGGER.info(f"Cannot parse rfidpad battery message: {msg.payload}")
        self.battery_level = message["level"]
        self.battery_voltage = message["voltage"]

        await self.battery_sensor.async_update_ha_state()

    async def async_update_status(self, new_status):
        message = {
            "new_status": new_status
        }
        msg = json.dumps(message)
        _LOGGER.debug(f"Publishing {msg} to {self.status_topic}")
        mqtt.async_publish(self.hass, self.status_topic, msg, retain=True)
