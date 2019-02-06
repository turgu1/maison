// DOOR SENSOR
//
// A very simple example of using the Maison framework to
// check if a door is open through a reed switch, using an ESP-12E 
// processor board.
//
// If the door stay open for more than 5 minutes, an "OPEN"
// message is sent every 5 minutes. Once the door is back in a close
// position, a "CLOSE" message is sent.
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
// 2019/01/15

#include <Arduino.h>

#include <Maison.h>

#ifndef DEBUGGING
  #define DEBUGGING 0
#endif

#if DEBUGGING
  #define SERIAL_SETUP Serial.begin(74880)
  #define PRINT(msg) Serial.print(msg)
  #define PRINTLN(msg) Serial.println(msg)
#else
  #define SERIAL_SETUP
  #define PRINT(msg)
  #define PRINTLN(msg)
#endif

#define LED         2
#define REED_SWITCH 5

#if DEBUGGING
  #define WAIT_TIME (30 * 1000)
#else
  #define WAIT_TIME (300 * 1000)
#endif

Maison maison(Maison::WATCHDOG_24H);

unsigned long event_time;

Maison::UserResult process(Maison::State state)
{
  switch (state) {

    case Maison::WAIT_FOR_EVENT:
      if (digitalRead(REED_SWITCH) == HIGH) {
        PRINTLN(F("==> AN EVENT HAS BEEN DETECTED <=="));
        #if !DEBUGGING
          digitalWrite(LED, LOW);
        #endif
        event_time = millis();
        return Maison::NEW_EVENT;
      }
      break;

    case Maison::PROCESS_EVENT:
      if (digitalRead(REED_SWITCH) == LOW) {
        #if !DEBUGGING
          digitalWrite(LED, HIGH);
        #endif
        PRINTLN(F("==> EVENT ABORTED: NOT LONG ENOUGH <=="));
        return Maison::ABORTED;
      }
      if ((millis() - event_time) > WAIT_TIME) {
        PRINTLN(F("==> SENDING OPEN MESSAGE <=="));
        maison.send_msg(
          MAISON_CTRL_TOPIC,
          F("{\"device\":\"%s\""
            ",\"msg_type\":\"EVENT_DATA\""
            ",\"content\":\"%s\"}"),
          maison.get_device_name(),
          "OPEN");
        event_time = millis();
      }
      else {
        return Maison::NOT_COMPLETED;
      }
      break;

    case Maison::WAIT_END_EVENT:
      if (digitalRead(REED_SWITCH) == HIGH) {
        if ((millis() - event_time) > WAIT_TIME) {
          return Maison::RETRY;
        }
        return Maison::NOT_COMPLETED;
      }
      #if !DEBUGGING
        digitalWrite(LED, HIGH);
      #endif
      PRINTLN(F("==> END OF EVENT DETECTED <=="));
      break;

    case Maison::END_EVENT:
      PRINTLN(F("==> SENDING CLOSE MESSAGE <=="));
      maison.send_msg(
        MAISON_CTRL_TOPIC,
        F("{\"device\":\"%s\""
          ",\"msg_type\":\"EVENT_DATA\""
          ",\"content\":\"%s\"}"),
        maison.get_device_name(),
        "CLOSE");
      break;

    case Maison::HOURS_24:
      PRINTLN(F("==> HOURS_24 <=="));
      break;

    case Maison::STARTUP:
      PRINTLN(F("==> STARTUP <=="));
      break;

    default:
      break;
  }

  return Maison::COMPLETED;
}

void setup() 
{
  delay(100);

  SERIAL_SETUP;
  
  #if !DEBUGGING
    pinMode(LED, OUTPUT);
    digitalWrite(LED, HIGH); // Turn LED off
  #endif

  pinMode(REED_SWITCH, INPUT_PULLUP);
  digitalWrite(REED_SWITCH, HIGH);

  maison.setup();
}

void loop() 
{
  maison.loop(process);
}