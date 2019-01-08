# Maison - Secure ESP8266 IOT MQTT based Framework

Note: This is still work in progress. This code is working on an ESP8266 and under heavy development. The documentation is also a work in progress... ESP32 support soon to be added.

This library implement a small, secure, IOT Framework for embedded ESP8266 / (soon to be added) ESP32 devices to serve into a Home IOT environment. It diminishes the amount of code to be put in the targeted app source code. The app interact with the framework through a finite state machine algorithm allowing for specific usage at every stage. 

Here are the main characteristics:

* Framework configuration saved in file through SPIFFS
* WiFi MQTT based communications
* TLS encryption: All communication with encryption
* Server authentification through fingerprinting
* User/Password identification
* JSON based data transmission with server
* Configuration updates through MQTT
* Option: Automated Watchdog transmission every 24 hours
* Option: Battery voltage transmission
* Option: DeepSleep or continuous power
* Option: Application specific mqtt topic subscription
* Option: Application specific automatic state saving in RTC memory
* Option: Verbose/Silent debugging output through compilation

The MQTT based transmission architecture is specific to this implementation and is describe below.

The framework is to be used with the [PlarformIO](https://platformio.org/) ecosystem. Some (soon to be supplied) examples are to be compiled through PlatformIO. Note that the library maybe useable through the Arduino IDE, but this is not supported. It is known to be a challenge to set compiling options and access Maison defined types from .ino source code.

The Maison framework, to be functional, requires the following:

* Proper application setup parameters in file `platformio.ini`. Look at the [Building an application](#building-an-application) section;
* Code in the user application to setup and use the framework. Look at the [Code usage](#code-usage) section;
* Configuration parameters located in SPIFFS (file `/config.json`). Look at the [Configuration parameters](#configuration-parameters) section.

The sections below describe the specific of these requirements.

## Building an application

The Maison framework is using the following libraries and, through its library configuration, automate their retrieval through the PlatformIO ecosystem:

* PubSubClient
* BearSSL
* ArduinoJSON

The following options **shall** be added to the `plarformio.ini` file of your application to integrate the framework:

```
lib_deps = https://github.com/turgu1/maison.git
build_flags = -D MQTT_MAX_PACKET_SIZE=512
```

Note that *MQTT_MAX_PACKET_SIZE* can be larger depending of the application requirements.

The following options in `platformio.ini` **shall** also be used:

```
framework = arduino
platform = espressif8266
```
### Compilation Options

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

## Code Usage

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

This piece of code won't do much at the user application level, but it will set the scene to the automation of exchanges with a MQTT message broker, sending startup/watchdog messages, answering information requests, changes of configuration, completly automated through Maison framework.

Here is a more complete example of code to be used to initialize the framework with optional features and integrate it in the loop() function. It shows both option parameters, calls to the framework with message callback and finite state machine functions:

```C++
#include <Maison.h>

struct user_data {
  uint32_t crc;
  int my_data;
  ...
} my_state;

Maison maison(Maison::Feature::WATCHDOG_24H|
              Maison::Feature::VOLTAGE_CHECK, 
              &my_state, sizeof(my_state));

void mqtt_msg_callback(const char  * topic, 
                       byte        * payload, 
                       unsigned int  length)
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

In the following sections, we describe the specific aspects of this code example.

### Include file

The `#include <Maison.h>` integrates the Maison header into the user application. This will import the Maison class declaration and a bunch of definitions that are documentated below. All required libraries needed by the framework are also imported by this call.

### Maison declaration

The `Maison maison(...)` declaration create an instance of the framework. This declaration accepts the following parameters:

* An optional feature mask, to enable some aspects of the framework (see table below).
* An optional user application memory structure (here named `my_state`) and it's size to be automatically saved in non-volatile memory when DeepSleep is enabled. 

The following table show the current features supported through the feature mask:

Feature Name  | Description
--------------|------------------
NONE          | No feature selected
VOLTAGE_CHECK | Chip A2D voltage readout will be sent on status/watchdog messages
BATTERY_POWER | Using batteries -> deep_sleep will then be used by the framework to limit power usage
WATCHDOG_24H  | A Watchdog message will be sent every 24 hours

The user application memory structure **shall** have a `uint32_t` item as the first element in the structure. This is used by the framework to verify that the content saved in non-volatile memory is valid using a CRC-32 checksum. The content will be initialized (zeroed) if the checksum is bad. The checksum is computed by the framework, the user application just need to supplied the space in the structure.

