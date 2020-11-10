# rfidpad
Hardware, firmware, and Home Assistant integration for an ESP32 based device to enable and disable a Home Assistant alarm system

# Design goals
- low-cost (at most 50 euros per panel, comparable to the Zipato RFID Mini Keypad)
- arm and disarm Home Assistant alarm system using cheap RFID tags
- panel should not require any wiring
- panel should run on battery power for at least one year

And if possible also:
- Configure tags in Home Assistant UI
- Display list of recent arm and disarm actions in Home Assistant
- Associate name with each tag
- Enabled tags should not be stored in the panel, only in Home Assistant

# Hardware panel
- Based on low power ESP32 board from EZSBC with a LiPo battery (https://www.ezsbc.com/index.php/products/wifi01-cell10.html) and PN532 board from Elechouse (https://www.elechouse.com/elechouse/index.php?main_page=product_info&cPath=90_93&products_id=2276).
- The device will spend most of the time in deep sleep. The ESP32 should draw less than 20 μA in deep sleep. The PN532 chip itself has a deep sleep mode that should draw around 10 μA.
- Three pushbuttons to wake up the device and select the new alarm mode (disarm, arm home, arm away)
- After waking up the device, it will connect to WiFi and an MQTT server
- When an RFID tag is detected, its ID is sent over MQTT, together with the desired new alarm mode
- The device listens on MQTT for messages about the Home Assistant alarm mode.
- Three LEDs on the panel to display the current alarm mode. Also serves as feedback to the user that the mode has been successfully changed

## Hardware challenges:
- Power usage. The PN532 board has an LED that burns as long as the board is powered. That means the Vcc of the PN532 will need to be switched using e.g. a FET to prevent the board from using too much power during the deep sleep period. Also, the deep sleep mode of the PN532 chip isn't supported by the library supplied by Elechouse.
- Housing, including the pushbutton and status LEDs.

# Firmware
Use the Arduino IDE to compile the firmware. Select the ESP32 dev board as the
target. Upload it to your RFIDPad

# Home Assistant custom component

## Prerequisities
You will need an MQTT server such as Mosquitto. Configure MQTT in your Home
Assistant installation. Give the homeassistant user read access to
`rfidpad/discovery/#`, to `rfidpad/+/battery` and to `rfidpad/+/action`.
Give it write access to `rfidpad/+/status`.

Add an `rfidpad` user and give it write access to 
`rfidpad/discovery/#`, to `rfidpad/+/battery` and to `rfidpad/+/action`.
Give it write access to `rfidpad/+/status`.

## Installation via HACS
- First install HACS in Home assistant. See https://hacs.xyz/ for instructions.
- Open the Community panel in Home Assistant
- Open the 'hamburger menu' and select 'Custom repositories'
- Enter https://github.com/janpascal/rfidpad where it says 'Add custom repository URL'. In the Category dropdown select 'Integration' and click 'Add' 
- Now press the big '+' on the bottom of the screen. Search for 'RFIDPad', select it and click on 'Install this repository in HACS'
- Configure the RFID custom component by filling in the base MQTT topic. The default (rfidpad) will do if you haven't changed the firmware.

## Configuration
The tags that can be used to arm/disarm your Home Assistant alarm need to be
configured in Home Assistant's `configuration.yaml` configuration file.
Add a snippet like:

  rfidpad:
    tags:
      - tag: ABCD0145
        name: Mary
      - tag: 45AB12EC
        name: John

and restart Home Assistant

## Automations
