// LED BLINK
//
// A very simple example of using the Maison framework to
// flash the LED of an ESP-12E processor board every second.
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

#if ESP32
  #define LED 23
#else
  #define LED 2
#endif

int  lit_wait_time;
bool led_lit;

Maison maison(Maison::WATCHDOG_24H);

Maison::UserResult process(Maison::State state)
{
  switch (state) {
  
    case Maison::WAIT_FOR_EVENT:
      lit_wait_time += maison.last_loop_duration();

      // Generate an event every 500 mS

      if (lit_wait_time > (1e3 * 500)) {
        lit_wait_time = 0;
        return Maison::NEW_EVENT;
      }
      break;

    case Maison::PROCESS_EVENT:
      digitalWrite(LED, led_lit ? HIGH : LOW);
      led_lit = !led_lit;
      break;

    default:
      break;
  }

  return Maison::COMPLETED;
}

void setup() 
{
  delay(100);
  Serial.begin(74880);
  
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH); // Turn LED off

  maison.setup();
  
  lit_wait_time = 0;
  led_lit = false;

  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address:");
  Serial.println(ip);
}

void loop() 
{
  maison.loop(process);
  delay(100);
}