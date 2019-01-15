DOOR SENSOR

A very simple example of using the Maison framework to
check if a door is open through a reed switch, using an ESP-12E 
processor board.

If the door stay open for more than 5 minutes, an "OPEN"
message is sent every 5 minutes. Once the door is back in a close

The framework requires the presence of a file named "/config.json" 
located in the device SPIFFS flash file system. To do so, please 

1. Update the supplied data/config.json.sample file to your
   network configuration parameters
2. Rename it to data/config.json
3. Kill the PlatformIO Serial Monitor
4. Launch the "Upload File System Image" task of the PlatformIO IDE.
