// MATRIX TERMINAL
//
// This is an example of a matrix terminal. It receives text to be
// shown from the MQTT Broker and display the information on 
// a LED matrix built from 8x8 cells.
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
// 2019/01/14

#include <Arduino.h>

#include <Maison.h>
#include <string.h>

#include "utils.h"

#define TERMINAL_TOPIC "terminal"

// ---- Globals ----

bool start;
char msg[200];
int  msg_len;

// ---- Maison Framework ----

Maison maison(Maison::WATCHDOG_24H);

void matrix_callback(const char * topic, byte * payload, unsigned int length) 
{
  int i;
  
  if (length > 200) length = 199;

  i = 0;
  if (payload[0] == 7) {
    buzz3();
    i = 1;
    length--;
  }

  memcpy(msg, &payload[i], length);
  msg[length] = 0;
  msg_len = length;

  PRINT("Received the following message: ");
  PRINTLN(msg);
}

void setup() 
{
  delay(100);

  SERIAL_SETUP;
  
  init_buzzer(); // This will buzz
  
  maison.setup();

  char buff[60];
  maison.set_msg_callback(matrix_callback, 
                          maison.my_topic(TERMINAL_TOPIC, buff, 60), 
                          0);
  init_display();
 
  start = true;
    
  strcpy(msg, "Booting...");
  msg_len = 10 ;
}

void loop() 
{
  maison.loop();

  if (msg_len > 0) {
    if (displayText(msg, msg_len)) msg_len = 0;
  }

  if (start) {
    if (msg_len == 0) {
      strcpy(msg, "Ready!");
      msg_len = 6;  
      start = false;
    }    
  }
}

