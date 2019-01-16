// # DHT-22 Sensor
// 
// A very simple example of using the Maison framework to
// get temperature and humidity from DHT22 connected to an ESP-12E board.
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
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

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

#if DEBUGGING
  #define WAIT_TIME (1 * 60 * 1000)
#else
  #define WAIT_TIME (15 * 60 * 1000)
#endif

#define DHTPIN 5

DHT dht(DHTPIN, DHT22);

Maison maison(Maison::WATCHDOG_24H);

long period_start;

void setup() 
{
  delay(100);
  SERIAL_SETUP;
  
  dht.begin();
  maison.setup();
  period_start = millis();
}

void loop() 
{
  maison.loop();

  if ((millis() - period_start) > WAIT_TIME) {
    period_start = millis();

    //sensors_event_t event;

    float humidity    = dht.readHumidity();
    float temperature = dht.readTemperature();

    PRINT("Temperature: "); PRINTLN(temperature);
    PRINT("Humidity: ");    PRINTLN(humidity);

    char buff[50];
    snprintf(buff, 50, "{\"T\":%4.1f,\"H\":%4.1f}", temperature, humidity);

    maison.send_msg(
      MAISON_CTRL_TOPIC, 
      "{\"device\":\"%s\","
      "\"msg_type\":\"DHT_DATA\","
      "\"content\":%s}",
      maison.get_device_name(),
      buff);
  }
}