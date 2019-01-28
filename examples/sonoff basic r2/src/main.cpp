// SONOFF BASIC R2
//
// An example of an application for the Sonoff BASIC R2 Switch.
//
// This application allow for the control of the Sonoff both from a wall
// switch connected to GPIO3/GND though the FTDI connector (GPIO3) and MQTT
// based messages received through the normal device related topic
// maison/device_name/relay. The relay will be toggling between off and on
// positions depending on the reception of messages  (ON! OFF! or TOGGLE!)
// or the toggling of the wall switch. A status message will be sent to the
// MQTT broker "maison/status" every time the relay will be toggled.
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
// 2019/01/24

#include <Arduino.h>

#include <Maison.h>

#ifndef DEBUGGING
  #define DEBUGGING 0
#endif

#if DEBUGGING
  #define SERIAL_SETUP Serial.begin(74880)
  #define PRINT(msg)   Serial.print(msg)
  #define PRINTLN(msg) Serial.println(msg)
  #define SHOW_RELAY { PRINT("Relay: "); PRINTLN(relay_is_on ? "ON" : "OFF"); }
#else
  #define SERIAL_SETUP
  #define PRINT(msg)
  #define PRINTLN(msg)
  #define SHOW_RELAY
#endif

#define RELAY 12
#define LED   13

#if DEBUGGING
  #define SWITCH      0  // Uses device button for debugging (IO0)
  #define WALL_SWITCH 0  // Push button
#else
  #define SWITCH      3  // Use RXD as switch entry (IO3)
  #define WALL_SWITCH 1  // Normal wall switch
#endif

#define MAX_DEBOUNCE_COUNT 3

#define LED_ON  digitalWrite(LED, LOW )
#define LED_OFF digitalWrite(LED, HIGH)

#define RELAY_ON     { digitalWrite(RELAY, HIGH); relay_is_on = true;  LED_ON;  }
#define RELAY_OFF    { digitalWrite(RELAY, LOW ); relay_is_on = false; LED_OFF; }
#define RELAY_TOGGLE { if (relay_is_on) RELAY_OFF else RELAY_ON                 }

#define RELAY_TOPIC "relay"

#define ON_AT_STARTUP 0

Maison maison(Maison::WATCHDOG_24H);

bool relay_is_on;
bool switch_state;
bool switch_new_state;
byte switch_new_state_count;

void send_relay_state()
{
  maison.send_msg(MAISON_STATUS_TOPIC,
                  "{"
                  "\"device\":\"%s\","
                  "\"msg_type\":\"RELAY_STATE\","
                  "\"content\":\"%s\""
                  "}",
                  maison.get_device_name(),
                  relay_is_on ? "ON" : "OFF");
}

void sonoff_callback(const char * topic, byte * payload, unsigned int length)
{
  if (memcmp(payload, "TOGGLE!", 7) == 0) {
    RELAY_TOGGLE;
    send_relay_state();
  }
  else if (memcmp(payload, "ON!", 3) == 0) {
    RELAY_ON;
    send_relay_state();
  }
  else if (memcmp(payload, "OFF!", 4) == 0) {
    RELAY_OFF;
    send_relay_state();
  }
  else if (memcmp(payload, "STATE?", 6) == 0) {
    send_relay_state();
  }
  SHOW_RELAY;
}

void setup()
{
  delay(100);

  SERIAL_SETUP;

  pinMode(LED,    OUTPUT);
  pinMode(RELAY,  OUTPUT);
  pinMode(SWITCH, INPUT_PULLUP);

  #if ON_AT_STARTUP
    RELAY_ON;
  #else
    RELAY_OFF;
  #endif

  delay(100);

  maison.setup();

  switch_state = digitalRead(SWITCH) == LOW;
  switch_new_state_count = 0;

  static char buff[60];
  maison.my_topic(RELAY_TOPIC, buff, 60);

  maison.set_msg_callback(sonoff_callback, buff, 0);
}

void loop()
{
  maison.loop();

  bool new_state = digitalRead(SWITCH) == LOW;

  if (new_state != switch_state) {
    if (new_state != switch_new_state) {
      switch_new_state_count = 0;
      switch_new_state       = new_state;
    }
    if (++switch_new_state_count > MAX_DEBOUNCE_COUNT) {
      // We have a new switch state. Toggle the relay...
      switch_state = switch_new_state;
      #if WALL_SWITCH
        RELAY_TOGGLE;
        send_relay_state();
        SHOW_RELAY;
      #else
        if (switch_state) {
          RELAY_TOGGLE;
          send_relay_state();
          SHOW_RELAY;
        }
      #endif
    }
  }
  else {
    switch_new_state = switch_state;
  }

  delay(100);
}
