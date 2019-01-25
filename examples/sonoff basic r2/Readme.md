SONOFF BASIC R2

An example of an application for the Sonoff BASIC R2 Switch.

This application allow for the control of the Sonoff both from a wall switch connected to GPIO3/GND though the FTDI connector (GPIO3) and MQTT based messages received through the normal device related topic maison/device_name/relay. The relay will be toggling between off and on positions depending on the reception of messages (ON! OFF! or TOGGLE!) or the toggling of the wall switch. A status message will be sent to the MQTT broker "maison/status" every time the relay will be toggled.

The framework requires the presence of a file named "/config.json" located in the device SPIFFS flash file system. To do so, please

1. Update the supplied data/config.json.sample file to your
   network configuration parameters
2. Rename it to data/config.json
3. Kill the PlatformIO Serial Monitor
4. Launch the "Upload File System Image" task of the PlatformIO IDE.
