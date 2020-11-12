"""Constants for rfidpad."""

# Base component constants
NAME = "RFIDPad"
DOMAIN = "rfidpad"
DOMAIN_DATA = f"{DOMAIN}_data"
VERSION = "0.0.1"

ISSUE_URL = "https://github.com/janpascal/rfidpad/issues"

# Icons
ICON = "mdi:format-quote-close"

# Device classes
BINARY_SENSOR_DEVICE_CLASS = "connectivity"

# Platforms
BINARY_SENSOR = "binary_sensor"
SENSOR = "sensor"
SWITCH = "switch"
#PLATFORMS = [BINARY_SENSOR, SENSOR, SWITCH]
PLATFORMS = [SENSOR]


# Configuration and options
CONF_MQTT_PREFIX = "mqtt_prefix"
CONF_TAGS = "tags"
CONF_TAG = "tag"

DEVICE_CONF_ID = "id"
DEVICE_CONF_NAME = "name"
DEVICE_CONF_MODEL = "model"
DEVICE_CONF_MANUFACTURER = "manufacturer"
DEVICE_CONF_SW_VERSION = "sw_version"
DEVICE_CONF_BASE_TOPIC = "base_topic"
DEVICE_CONF_ACTION_TOPIC = "action_topic"
DEVICE_CONF_STATUS_TOPIC = "status_topic"
DEVICE_CONF_BATTERY_TOPIC = "battery_topic"

STATUS_TRANSITIONS = {
    'DISARM': 'DISARMED',
    'ARM_HOME': 'ARMED_HOME',
    'ARM_AWAY': 'ARMED_AWAY',
}

# Defaults
DEFAULT_NAME = DOMAIN
DEFAULT_MQTT_PREFIX = "rfidpad"

DEFAULT_MODEL = "RFIDPad"
DEFAULT_MANUFACTURER = "Jan-Pascal van Best"
DEFAULT_SW_VERSION = "0.0.1"
DEFAULT_STATUS_TOPIC = "status"
DEFAULT_ACTION_TOPIC = "action"
DEFAULT_BATTERY_TOPIC = "battery"


STARTUP_MESSAGE = f"""
-------------------------------------------------------------------
{NAME}
Version: {VERSION}
This is a custom integration!
If you have any issues with this you need to open an issue here:
{ISSUE_URL}
-------------------------------------------------------------------
"""

