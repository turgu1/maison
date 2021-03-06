/*

DEEP_SLEEP SENSOR

An example of using the Maison framework using almost all features available.
It gets data from GPIO14 pin of an ESP-12E board and when its state changes,
it sends a message to the MQTT broker. Possible values are "ON", "OFF" 
and "CHIRP". The "CHIRP" value is sent when the device has been awaken 
but the signal if OFF.

It is expected that hardware connexion  will reset the chip when GPIO14 pin 
goes high such that the code will gain control before the end of the 
DeepSleep period.

The framework requires the presence of a file named "/config.json" located 
in the device SPIFFS flash file system.To do so, please

  1. Update the supplied data config.json.sample file to your network 
     configuration parameters 
  2. Rename it to data / config.json 
  3. Kill the PlatformIO Serial Monitor 
  4. Launch the "Upload File System Image" task of the PlatformIO IDE.

Guy Turcotte
2019/01/13

*/

#include <Maison.h>

ADC_MODE(ADC_VCC);

#ifndef DEBUGGING
  #define DEBUGGING 0
#endif

#if DEBUGGING
  #define LONG_WAIT    10
  #define PRINT(a)     Serial.print(a)
  #define PRINTLN(a)   Serial.println(a)
#else
  #define LONG_WAIT 60
  #define PRINT(a)
  #define PRINTLN(a)
#endif

#if DEBUGGING || SERIAL_NEEDED
  #define SERIAL_SETUP Serial.begin(74880)
#else
  #define SERIAL_SETUP
#endif

#define SENSE_PIN 14

// The number of time the pin High message will be sent to the broker

#define MAX_XMIT_COUNT 5

struct mem_info {
  int32_t  crc;
  uint32_t xmit_count;
} my_mem;

int pin_state;

Maison maison(Maison::WATCHDOG_24H  |
              Maison::VOLTAGE_CHECK |
              Maison::DEEP_SLEEP,
              &my_mem,
              sizeof(my_mem));


Maison::UserResult process(Maison::State state)
{
  switch (state) {
  
    case Maison::WAIT_FOR_EVENT:
      PRINTLN(F("==> WAIT_FOR_EVENT <=="));
      if (pin_state == HIGH) {
        maison.set_deep_sleep_wait_time(1);
        PRINTLN(F("==> AN EVENT HAS BEEN DETECTED <=="));
        return Maison::NEW_EVENT;
      }
      break;

    case Maison::PROCESS_EVENT:
      PRINTLN(F("==> PROCESS_EVENT <=="));
      if (pin_state == LOW) {
        maison.send_msg(
            MAISON_EVENT_TOPIC,
            F("{\"device\":\"%s\""
              ",\"msg_type\":\"EVENT_DATA\""
              ",\"content\":\"%s\"}"),
            maison.get_device_name(),
            "CHIRP");
        maison.set_deep_sleep_wait_time(1);
        PRINTLN(F("==> NOT NOW <=="));
        return my_mem.xmit_count == 0 ? Maison::ABORTED : Maison::COMPLETED;
      }
      maison.send_msg(
        MAISON_EVENT_TOPIC, 
        F("{\"device\":\"%s\""
          ",\"msg_type\":\"EVENT_DATA\""
          ",\"content\":\"%s\"}"),
        maison.get_device_name(),
        "ON");
      PRINTLN(F("==> YES THERE IS <=="));
      maison.set_deep_sleep_wait_time(LONG_WAIT);
      break;

    case Maison::WAIT_END_EVENT:
      PRINTLN(F("==> WAIT_END_EVENT <=="));
      if (pin_state == HIGH) {
        if (++my_mem.xmit_count < MAX_XMIT_COUNT) {
          maison.set_deep_sleep_wait_time(1);
          return Maison::RETRY;
        }

        maison.set_deep_sleep_wait_time(LONG_WAIT);
        PRINTLN(F("==> NOT YET <=="));
        return Maison::NOT_COMPLETED;
      }
      PRINTLN(F("==> END OF EVENT DETECTED <=="));
      maison.set_deep_sleep_wait_time(1);
      my_mem.xmit_count = 0;
      break;

    case Maison::END_EVENT:
      PRINTLN(F("==> END_EVENT <=="));
      maison.send_msg(
          MAISON_EVENT_TOPIC,
          F("{\"device\":\"%s\""
            ",\"msg_type\":\"EVENT_DATA\""
            ",\"content\":\"%s\"}"),
          maison.get_device_name(),
          "OFF");
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
  pinMode(SENSE_PIN, INPUT);
  delay(10);
  pin_state = digitalRead(SENSE_PIN);

  delay(100);

  SERIAL_SETUP;

  PRINTLN(F("==> SETUP <=="));

  turnOff(0);
  turnOff(2);
  turnOff(4);
  turnOff(5);
  turnOff(12);
  turnOff(13);
  //turnOff(14);
  turnOff(15, 0);

  PRINTLN(F("  maison.setup()"));

  maison.setup();

  if (maison.is_hard_reset()) {
    PRINTLN(F("==> HARD RESET! <=="));
    my_mem.xmit_count = 0;
  }
  else {
    PRINTLN(F("==> DEEP SLEEP WAKEUP <=="));
  }
}

void loop() 
{
  maison.loop(process);
}