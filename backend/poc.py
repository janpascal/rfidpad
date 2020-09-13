#!/usr/bin/env python3

import time
import sys
import paho.mqtt.client as mqtt

import config

COMMAND_RESPONSES = {
    "DISARM": "DISARMED",
    "ARM_HOME": "ARMED_HOME",
    "ARM_AWAY": "ARMED_AWAY"
}

def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))
    print("Connecting to {}".format(config.MQTT_ACTION_TOPIC))
    client.subscribe(config.MQTT_ACTION_TOPIC)

def on_message(client, userdata, msg):
    print(msg.topic+" "+str(msg.payload))
    line=msg.payload.decode('UTF-8')
    print("Type: {}".format(type(line)))
    command,tag=line.split(" ")
    print("Tag: [{}]".format(tag))
    if not command or not tag:
        print("Illegal command {}".format(line))
        return
    if tag not in config.ALLOWED_TAGS:
        print("Unknown tag {}".format(tag))
        return
    if command not in COMMAND_RESPONSES:
        print("Unknown command {}".format(command))
        return
    print("Handling command {} {}".format(command, tag))
    new_status = COMMAND_RESPONSES[command]
    print("New status: {}".format(new_status))
    print("Publishing {} to {}".format(new_status, config.MQTT_STATUS_TOPIC))
    client.publish(config.MQTT_STATUS_TOPIC, new_status, retain=True)




print("hello, World!")

client = mqtt.Client()
client.username_pw_set(config.MQTT_USER, config.MQTT_PASSWORD)
client.on_connect = on_connect
client.on_message = on_message

print("Connection to MQTT server {}:{}".format(config.MQTT_SERVER, config.MQTT_PORT))
client.connect(config.MQTT_SERVER, config.MQTT_PORT) 
client.loop_start()
sys.stdout.flush()

while True:
    time.sleep(1.0)
    sys.stdout.flush()
    print("<ping>")
