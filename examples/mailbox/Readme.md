MAILBOX SENSOR

A very simple example of using the Maison framework to
check if a mailbox lid has been opened through a reed switch, using an ESP-12E 
processor board.

A first "OPEN" message is sent after the lid has been opened.  A second "STILL" message is sent with 1 minutes interval if the lid stayed opened. Once the lid is back in a close position, a "CLOSE" message is sent.

The framework requires the presence of a file named "/config.json" 
located in the device SPIFFS flash file system. To do so, please 

1. Update the supplied data/config.json.sample file to your
   network configuration parameters
2. Rename it to data/config.json
3. Kill the PlatformIO Serial Monitor
4. Launch the "Upload File System Image" task of the PlatformIO IDE.
