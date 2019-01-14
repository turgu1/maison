# SERIAL PORT INPUT

A very simple example of using the Maison framework to
get data from a serial port on GPIO 13 and 15 of an ESP-12E board.
The data is pushed as a message to the maison/ctrl topic.

The framework requires the presence of a file named "/config.json" 
located in the device SPIFFS flash file system. To do so, please 

1. Update the supplied data/config.json.sample file to your
   network configuration parameters
2. Rename it to data/config.json
3. Kill the PlatformIO Serial Monitor
4. Launch the "Upload File System Image" task of the PlatformIO IDE.
