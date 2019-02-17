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

#if HOMIE
Maison::UserResult process(Maison::State state)
{
  switch (state) {

    case Maison::WAIT_FOR_EVENT:
      PRINTLN(F("==> WAIT_FOR_EVENT <=="));
      break;

    case Maison::PROCESS_EVENT:
      PRINTLN(F("==> PROCESS_EVENT <=="));
      break;

    case Maison::WAIT_END_EVENT:
      PRINTLN(F("==> WAIT_END_EVENT <=="));
      break;

    case Maison::END_EVENT:
      PRINTLN(F("==> END_EVENT <=="));
      break;

    case Maison::HOURS_24:
      PRINTLN(F("==> HOURS_24 <=="));
      break;

    case Maison::STARTUP:
      PRINTLN(F("==> STARTUP <=="));
      send_homie("environment",             "name",       true, F("Environment")         );
      send_homie("environment",             "type",       true, F("DHT")                 );
      send_homie("environment",             "properties", true, F("temperature,humidity"));
      send_homie("environment/temperature", "unit",       true, F("°C")                  );
      send_homie("environment/temperature", "datatype",   true, F("float")               );
      send_homie("environment/humidity",    "unit",       true, F("%")                   );
      send_homie("environment/humidity",    "datatype",   true, F("float")               );
      break;

    default:
      break;
  }

  return Maison::COMPLETED;
}
#endif

void setup()
{
  delay(100);
  SERIAL_SETUP;

  dht.begin();
  maison.setup("environment");
  period_start = millis();
}

void loop()
{
  maison.loop(process);

  if ((millis() - period_start) > WAIT_TIME) {
    period_start = millis();

    //sensors_event_t event;

    float humidity    = 31;   //dht.readHumidity();
    float temperature = 12.5; //dht.readTemperature();

    PRINT("Temperature: "); PRINTLN(temperature);
    PRINT("Humidity: ");    PRINTLN(humidity);

    #if HOMIE
      send_homie("environment/temperature", "", true, "%4.1f", temperature);
      send_homie("environment/humidity",    "", true, "%4.1f", humidity   );
    #else
      char buff[50];
      snprintf(buff, 50, "{\"T\":%4.1f,\"H\":%4.1f}", temperature, humidity);

      maison.send_msg(
        MAISON_CTRL_TOPIC,
        F("{\"device\":\"%s\""
          ",\"msg_type\":\"DHT_DATA\""
          ",\"content\":%s}"),
        maison.get_device_name(),
        buff);
    #endif
  }
}
