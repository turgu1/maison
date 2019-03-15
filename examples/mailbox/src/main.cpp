// MAILBOX SENSOR
//
// A very simple example of using the Maison framework to
// check if a mailbox lid has been opened through a reed switch, 
// using an ESP-12E/F processor board.
//
// A first "OPEN" message is sent after the lid has been opened.  
// A second "OPEN" message is sent with 5 minutes interval if the lid
// stayed opened. Once the lid is back in a close position, a "CLOSE"
// message is sent. If the lid is opened for less than ~2 seconds a 
// "CHIRP" message is sent.
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
// 2019/03/14

#include <Maison.h>

ADC_MODE(ADC_VCC);

#ifndef DEBUGGING
  #define DEBUGGING 0
#endif

#if DEBUGGING
  #define SERIAL_SETUP Serial.begin(74880)
  #define PRINT(msg)   Serial.print(msg)
  #define PRINTLN(msg) Serial.println(msg)
#else
  #define SERIAL_SETUP
  #define PRINT(msg)
  #define PRINTLN(msg)
#endif

#define REED_SWITCH 14

#if DEBUGGING
  #define WAIT_TIME 30  // In seconds
#else
  #define WAIT_TIME 300 // In seconds
#endif

#define MAX_XMIT_COUNT 2

struct mem_info
{
  int32_t crc;
  uint32_t xmit_count;
  bool close_event_required;
} my_mem;

int reed_state;

Maison maison(Maison::WATCHDOG_24H |
              Maison::VOLTAGE_CHECK |
              Maison::DEEP_SLEEP,
              &my_mem,
              sizeof(my_mem));

Maison::UserResult process(Maison::State state)
{
  switch (state) {

    case Maison::WAIT_FOR_EVENT:
      PRINTLN(F("==> WAIT_FOR_EVENT <=="));
      if (reed_state == HIGH) {
        PRINTLN(F("==> AN EVENT HAS BEEN DETECTED <=="));
        maison.set_deep_sleep_wait_time(1);
        return Maison::NEW_EVENT;
      }
      break;

    case Maison::PROCESS_EVENT:
      PRINTLN(F("==> PROCESS_EVENT <=="));
      if (reed_state == LOW) {
        if (my_mem.close_event_required) {
          my_mem.close_event_required = false;
          maison.send_msg(
            MAISON_EVENT_TOPIC,
            F("{\"device\":\"%s\""
              ",\"msg_type\":\"EVENT_DATA\""
              ",\"content\":\"%s\"}"),
            maison.get_device_name(),
            "CLOSE");
        }
        else {
          PRINTLN(F("==> EVENT ABORTED: NOT LONG ENOUGH <=="));
          maison.send_msg(
              MAISON_EVENT_TOPIC,
              F("{\"device\":\"%s\""
                ",\"msg_type\":\"EVENT_DATA\""
                ",\"content\":\"%s\"}"),
              maison.get_device_name(),
              "CHIRP");
        }
        maison.set_deep_sleep_wait_time(1);
        return Maison::ABORTED;
      }
      PRINTLN(F("==> SENDING OPEN MESSAGE <=="));
      maison.send_msg(
        MAISON_EVENT_TOPIC,
        F("{\"device\":\"%s\""
          ",\"msg_type\":\"EVENT_DATA\""
          ",\"content\":\"%s\"}"),
        maison.get_device_name(),
        "OPEN");
      my_mem.close_event_required = true;
      maison.set_deep_sleep_wait_time(WAIT_TIME);
      break;

    case Maison::WAIT_END_EVENT:
      PRINTLN(F("==> WAIT_END_EVENT <=="));
      if (reed_state == HIGH) {
        if (++my_mem.xmit_count < MAX_XMIT_COUNT) {
          maison.set_deep_sleep_wait_time(1);
          return Maison::RETRY; // Will return to PROCESS_EVENT in 1 second
        }

        maison.set_deep_sleep_wait_time(WAIT_TIME);
        PRINTLN(F("==> NOT YET <=="));
        return Maison::NOT_COMPLETED;
      }
      PRINTLN(F("==> END OF EVENT DETECTED <=="));
      maison.set_deep_sleep_wait_time(1);
      my_mem.xmit_count = 0;
      my_mem.close_event_required = false;
      break;

    case Maison::END_EVENT:
      PRINTLN(F("==> END_EVENT <=="));
      PRINTLN(F("==> SENDING CLOSE MESSAGE <=="));
      maison.send_msg(
        MAISON_EVENT_TOPIC,
        F("{\"device\":\"%s\""
          ",\"msg_type\":\"EVENT_DATA\""
          ",\"content\":\"%s\"}"),
        maison.get_device_name(),
        "CLOSE");
      maison.set_deep_sleep_wait_time(1);
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

inline void turnOff(int pin, int val = 1)
{
  pinMode(pin, OUTPUT);
  digitalWrite(pin, val);
}

void setup() 
{
  delay(100);

  SERIAL_SETUP;

  PRINTLN(F("==> SETUP <=="));

  pinMode(REED_SWITCH, INPUT);

  delay(10);
  reed_state = digitalRead(REED_SWITCH);

  turnOff(0);
  turnOff(2);
  turnOff(4);
  turnOff(5);
  turnOff(12);
  turnOff(13);
  //turnOff(14);
  turnOff(15, 0);

  maison.setup();

  // This must be done after maison.setup() as my_mem would have been initialized 
  // or retrieved from RTC memory

  if (maison.is_hard_reset()) {
    PRINTLN(F("==> HARD RESET! <=="));
    my_mem.xmit_count = 0;
    my_mem.close_event_required = false;
  }
  else {
    PRINTLN(F("==> DEEP SLEEP WAKEUP <=="));
  }

}

void loop() 
{
  maison.loop(process);
}