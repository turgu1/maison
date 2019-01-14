# MATRIX TERMINAL

This is an example of a matrix terminal. It receives text to be
shown from the MQTT Broker and display the information on 
a LED matrix built from 8x8 cells.

The framework requires the presence of a file named "/config.json" 
located in the device SPIFFS flash file system. To do so, please 

1. Update the supplied data/config.json.sample file to your
   network configuration parameters
2. Rename it to data/config.json
3. Kill the PlatformIO Serial Monitor
4. Launch the "Upload File System Image" task of the PlatformIO IDE.
