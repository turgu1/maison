# Maison - Secure ESP8266 IOT MQTT based Framework

Note: This is still work in progress. This code is working on an ESP8266 but under heavy development. ESP32 support soon to be added.

This library implement a small, secure, IOT Framework for embedded ESP8266/ESP32 devices to serve into a Home IOT environment. It diminishes the amount of code to be put in the targeted app source code. The app interact with the framework through a finite state machine algorithm allowing for specific usage at every stage. 

Here are the main characteristics:

* WiFi MQTT based communications
* TLS encryption: All communication with encryption
* Server authentification through fingerprinting
* User/Password identification
* JSON based data transmission with server
* Configuration updates through MQTT
* Configuration saved in file through SPIFFS
* Option: Automated Watchdog transmission every 24 hours
* Option: Battery voltage transmission
* Option: DeepSleep or continuous power
* Option: Application specific mqtt topic subscription
* Option: Application specific automatic state saving in RTC memory
* Option: Verbose/Silent debugging output through compilation

The MQTT based transmission architecture is specific to this implementation and is describe below.

The framework is to be used with the [PlarformIO](https://platformio.org/) ecosystem. Some (soon to be supplied) examples are to be compiled through PlatformIO. Note that the library maybe useable through the Arduino IDE, but this is not supported. It is known to be a challenge to set compiling options and access Maison defined types from .ino source code.

## Building an application with the Maison framework

The Maison framework is using the following libraries and, through its library configuration, automate their retrieval through the PlatformIO ecosystem:

* PubSubClient
* BearSSL
* ArduinoJSON

The following options must be added to the `plarformio.ini` file of your application to integrate the framework:

```
lib_deps = https://github.com/turgu1/maison.git
build_flags = -D MQTT_MAX_PACKET_SIZE=512
```

Note that *MQTT_MAX_PACKET_SIZE* can be larger depending of the application requirements.

The following options in `platformio.ini` must also be used:

```
framework = arduino
platform = espressif8266
```
## Usage

Here is a minimal piece of code to initialize and start the framework:

```C++
#include <Maison.h>

Maison maison();

void setup()
{
  maison.setup();
}

void loop()
{
  maison.loop();
}
```

This piece of code won't do much at the user application level, but it will set the scene to the automation of exchanges with a MQTT message broker, sending startup/watchdog messages, answering information requests, changes of configuration, etc.

Here is an example of code to be used to initialize the framework and integrate it in the loop() function. It shows both option parameters, calls to the framework and callback functions:

```C++
#include <Maison.h>

Maison maison(Maison::Feature::WATCHDOG_24H|
              Maison::Feature::VOLTAGE_CHECK);

void mqtt_msg_callback(const char * topic, byte * payload, unsigned int length)
{

}

Maison::UserResult process_state(Maison::State state)
{
  switch (state) {
    case ...
  }

  return Maison::UserResult::COMPLETED;
}

void setup()
{
  maison.setup();
  maison.setUserCallback("my_topic", mqtt_msg_callback, 0);
}

void loop()
{
  maison.loop(process_state);
}
```

The use of `process_state` and `mqtt_msg_callback` is optional.

## Compilation Options

The Maison framework allow for some defined options to be modified through -D compilation parameters (PlatformIO: build_flags). The following are the compilation options available to change some library behavior:

Option              | Default   | Description
--------------------|-----------|------------------------------------------------------------------------
MAISON_TESTING      |    0      | If = 1, enable debuging output through the standard Serial port. The serial port must be initialized by the application (Serial.begin()) before calling any Maison function. 
QUICK_TURN          |    0      | If = 1, WATCHDOG messages are sent every 2 minutes instead of 24 hours. This is automatically the case when *MAISON_TESTING* is set to 1.
MAISON_PREFIX_TOPIC | maison/   | All topics used by the framework are prefixed with this text   
MAISON_STATUS_TOPIC | maison/status | Topic where the framework status are sent
MAISON_CTRL_TOPIC   | maison/ctrl   | Topic where the framework config control are sent
CTRL_SUFFIX_TOPIC   | /ctrl         | This is the topic suffix used to identify device-related control topic

Note that for *MAISON_STATUS_TOPIC* and *MAISON_CTRL_TOPIC*, they will be modifified automatically if *MAISON_PREFIX_TOPIC* is changed. For example, if you change *MAISON_PREFIX_TOPIC* to be `home/`, *MAISON_STATUS_TOPIC* will become `home/status` and *MAISON_CTRL_TOPIC* will become `maison/ctrl`.

The framework will subscribe to MQTT messages coming from the server on a topic built using *MAISON_PREFIX_TOPIC*, the device name and *CTRL_SUFFIX_TOPIC*. For example, if the device name is "WATER_SPILL", the subsribed topic would be `maison/WATER_SPILL/ctrl`.
