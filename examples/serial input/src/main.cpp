// SERIAL PORT INPUT
//
// A very simple example of using the Maison framework to
// get data from a serial port on GPIO 13 and 15 of an ESP-12E board.
// The data is pushed as a message to the maison/ctrl topic.
//
// The framework requires the presence of a file named "/config.json" 
// located in the device SPIFFS flash file system. To do so, please 
//
// 1. Update the supplied data/config.json.sample file to your
//    network configuration parameters
// 2. Rename it to data/config.json
// 3. Kill the PlatformIO Serial Monitor
// 4. Launch the "Upload File System Image" task of the PlatformIO IDE.
//
// Guy Turcotte
// 2019/01/11

#include <Arduino.h>

#include <Maison.h>
#include <SoftwareSerial.h>

SoftwareSerial my_serial(13, 15, false, 256);

Maison maison(Maison::WATCHDOG_24H);

#define CR  ((char) 13)
#define LF  ((char) 10)
#define TAB ((char)  9)

char buffer[256];
int idx;

Maison::UserResult process(Maison::State state)
{
  switch (state) {
  
    case Maison::WAIT_FOR_EVENT:
      while (my_serial.available()) {
        char ch = my_serial.read();
        if ((ch == CR) || (ch == LF)) {
          if (idx > 0) {
            buffer[idx] = 0;
            return Maison::NEW_EVENT;
          }
        }
        else if ((ch == '"') || (ch == TAB) || (ch == '\\')) {
          if (idx < 254) {
            buffer[idx++] = '\\';
            buffer[idx++] = (ch == TAB) ? 't' : ch;
          }
        }
        else if ((ch >= ' ') && (ch < 127)) {
          if (idx < 255) {
            buffer[idx++] = ch;
          }
        }
      }
      break;

    case Maison::PROCESS_EVENT:
      maison.send_msg(
        MAISON_CTRL_TOPIC, 
        "{\"device\":\"%s\","
        "\"msg_type\":\"EVENT_DATA\","
        "\"content\":\"%s\"}",
        maison.get_device_name(),
        buffer);
      break;

    case Maison::END_EVENT:
      buffer[0] = 0;
      idx = 0;
      break;

    default:
      break;
  }

  return Maison::COMPLETED;
}

void setup() 
{
  delay(100);
  my_serial.begin(2400);
  
  maison.setup();

  idx = 0;
}

void loop() 
{
  maison.loop(process);
}