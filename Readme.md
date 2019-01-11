# Maison - Minimally Secure ESP8266 IOT MQTT based Framework

Note: This is still work in progress. The documentation is also a work in progress. The code is working and must be considered of a Beta level.

This library implements a small, minimally secure, IOT Framework for embedded ESP8266 devices to serve into a Home IOT environment. It diminishes the amount of code to be put in the targeted application source code. The application interacts with the framework through a Finite State Machine algorithm allowing for specific usage at every stage.

Here are the main characteristics:

* Framework configuration saved in file through SPIFFS.
* WiFi MQTT based communications. Nothing else.
* TLS encryption: All MQTT communication with encryption.
* Server authentication through fingerprinting.
* User/Password identification.
* JSON based data transmission with server.
* Configuration updates through MQTT.
* Option: Automated Watchdog transmission every 24 hours.
* Option: Battery voltage transmission.
* Option: DeepSleep or continuous power.
* Option: Application specific MQTT topic subscription.
* Option: Application specific automatic state saving in RTC memory.
* Option: Verbose/Silent debugging output through compilation.

The MQTT based transmission architecture is specific to this implementation and is describe below.

The framework is to be used with the [PlarformIO](https://platformio.org/) ecosystem. Some (soon to be supplied) examples are to be compiled through PlatformIO.

Note that the library maybe usable through the Arduino IDE, but this is not supported. It is known to be a challenge to set compiling options and access Maison defined types from .ino source code.

The Maison framework, to be functional, requires the following:

* Proper application setup parameters in file `platformio.ini`. Look at the [Building an Application](#building-an-application) section;
* Code in the user application to setup and use the framework. Look at the [Code Usage](#code-usage) section;
* Configuration parameters located in SPIFFS (file `/config.json`). Look at the [Configuration Parameters](#configuration-parameters) section.

The sections below describe the specific of these requirements.

## Overview

The Maison library supplies the usual algorithms required for an IOT device to interact within an event management architecture based on the use of a MQTT broker for message exchanges. It helps the programmer in the management of the various aspects of integrating the code responsible of the functionality of the IOT device with the intricacies of managing the lifespan inside the architecture.

...

## Building an Application

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
:------------------:|:---------:|------------------------------------------------------------------------
MAISON_TESTING      |    0      | If = 1, enable debuging output through the standard Serial port. The serial port must be initialized by the application (Serial.begin()) before calling any Maison function.
QUICK_TURN          |    0      | If = 1, WATCHDOG messages are sent every 2 minutes instead of 24 hours. This is automatically the case when *MAISON_TESTING* is set to 1.
MAISON_PREFIX_TOPIC | maison/   | All topics used by the framework are prefixed with this text   
MAISON_STATUS_TOPIC | maison/status | Topic where the framework status are sent
MAISON_CTRL_TOPIC   | maison/ctrl   | Topic where the framework event controls are sent
CTRL_SUFFIX_TOPIC   | ctrl          | This is the topic suffix used to identify device-related control topic
DEFAULT_SHORT_REBOOT_TIME |  5  | This is the default reboot time in seconds when deep sleep is enable. This is used at the end of the following states: *PROCESS_EVENT*, *WAIT_END_EVENT*, *END_EVENT*. For the other states, the wait time is 60 minutes (3600 seconds).

Note that for *MAISON_STATUS_TOPIC* and *MAISON_CTRL_TOPIC*, they will be modified automatically if *MAISON_PREFIX_TOPIC* is changed. For example, if you change *MAISON_PREFIX_TOPIC* to be `home/`, *MAISON_STATUS_TOPIC* will become `home/status` and *MAISON_CTRL_TOPIC* will become `maison/ctrl`.

The framework will subscribe to MQTT messages coming from the server on a topic built using *MAISON_PREFIX_TOPIC*, the device name and *CTRL_SUFFIX_TOPIC*. For example, if the device name is "WATER_SPILL", the subscribed topic would be `maison/WATER_SPILL/ctrl`.

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

This piece of code won't do much at the user application level, but it will set the scene to the automation of exchanges with a MQTT message broker, sending startup/watchdog messages, answering information requests, changes of configuration, completely automated through Maison framework.

Here is a more complete example of code to be used to initialize the framework with optional features and integrate it in the loop() function. It shows both option parameters, calls to the framework with message callback and finite state machine functions:

```C++
ADC_MODE(ADC_VCC);

#include <Maison.h>

struct user_data {
  uint32_t crc;
  int my_data;
  ...
} my_state;

Maison maison(Maison::Feature::WATCHDOG_24H|
              Maison::Feature::VOLTAGE_CHECK,
              &my_state, sizeof(my_state));

void msg_callback(const char  * topic,
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
  maison.set_msg_callback("my_topic", msg_callback, 0);
}

void loop()
{
  maison.loop(process_state);
}
```

The use of `process_state` and `set_msg_callback` is optional.

In the following sections, we describe the specific aspects of this code example.

### Include File

The `#include <Maison.h>` integrates the Maison header into the user application. This will import the Maison class declaration and a bunch of definitions that are documented below. All required libraries needed by the framework are also included by this call.

### Maison Declaration

The `Maison maison(...)` declaration create an instance of the framework. This declaration accepts the following parameters:

* An optional [feature mask](#feature-mask), to enable some aspects of the framework (see table below).
* An optional [user application memory structure](#user-application-memory-structure) (here named `my_state`) and it's size to be automatically saved in non-volatile memory when DeepSleep is enabled.

#### Feature Mask

The following table show the current features supported through the feature mask (They are part of the Maison::Feature enum definition):

Feature Name  | Description
:------------:|------------------
NONE          | No feature selected.
VOLTAGE_CHECK | Chip A2D voltage readout will be sent on status/watchdog messages.
DEEP_SLEEP    | deep_sleep will be used by the framework to limit power usage (e.g. on batteries). RESET/RST and WAKE/GPIO16 pins need to be wired together.
WATCHDOG_24H  | A Watchdog message will be sent every 24 hours.

To use them, you have to prefix them with `Maison::` or `Maison::Feature::` as shown in the code example.

Note: if the *VOLTAGE_CHECK* feature is selected, the following line is required to be put at the beginning of the main application sketch:

```C++
ADC_MODE(ADC_VCC);
```

#### User Application Memory Structure

The user application memory structure **shall** have a `uint32_t` item as the first element in the structure. This is used by the framework to verify that the content saved in non-volatile memory is valid using a CRC-32 checksum. The whole content will be initialized (zeroed) if the checksum is bad. The checksum is computed by the framework, the user application just need to supplied the space in the structure.

#### maison.loop

The Maison::loop function must be called regularly in your main loop function to permit the execution of the finite state machine and receiving new MQTT messages. As a parameter the Maison::loop function accepts a processing function that will be called by Maison inside the finite state machine. The function will receive the current state value as a parameter. It must return a status value from the following list:

Value         | Description
:------------:|----------------
COMPLETED     |  Returned when the processing for the current state is considered completed. This is used mainly for all states.           
NOT_COMPLETED | The reverse of *COMPLETED*. Mainly used with *PROCESS_EVENT* in the case that it must be fired again to complete the processing
ABORTED       | Return in the case of *PROCESS_EVENT* when the event vanished before processing, such that the finite state machine return to the *WAIT_FOR_EVENT* state instead of going to the *WAIT_END_EVENT* state.
NEW_EVENT     | Returned when processing a *WAIT_FOR_EVENT* state to indicate that an event must be processed.

Note: if the *DEEP_SLEEP* feature was enabled, the loop will almost never return as the processor will wait for further processing through a call to ESP.deep_sleep function. The processor, after the wait time, will restart the code from the beginning. 

## Configuration Parameters

The Maison framework is automating access to the MQTT message broker through the WiFi connection. As such, parameters are required to link the device to the WiFi network and the MQTT broker server. A file named "/config.json" must be created on a SPIFFS file system in flash memory. This is a JSON structured file. Here is an example of such a file:

```json
{
  "version" : 1,
  "device_name" : "WATER_SPILL",
  "ssid" : "the wifi ssid",
  "wifi_password" : "the wifi password",
  "mqtt_server_name" : "the server name or IP address",
  "mqtt_user_name" : "the user name",
  "mqtt_password" : "password",
  "mqtt_port" : 8883,
  "mqtt_fingerprint" : [12,24,126,43,13,42,125,75,76,34,21,53,66,152,173,23,63,47,221,23]
}
```

All parameters must be present in the file to be considered valid by the framework. Here is a description of each parameter:

Parameter | Description
:--------:|------------------------------
version | This is the sequential version number. This is the property of the Server responsible of transmitting new configuration files to the device. It must be incremented every time a new configuration file is sent to the device. The device will not update its configuration if the version number is not greater than the current one. 
device_name | A unique identifier for the device. This identifier is used inside messages sent through MQTT. It is also used to generate the topics related to the device. It can be an empty string: the MAC address of the device WiFi interface will then be used as the identifier. Use letters, underscore, numbers to compose the identifier (no space or other special characters).
ssid / wifi_password | The WiFi SSID and password. Required to reach the network.
mqtt_server_name | This is the MQTT server name (SQDN) or IP address.
mqtt_user_name / mqtt_password | These are the credentials to connect to the MQTT server.
mqtt_port | The TLS/SSL port number of the MQTT server.
mqtt_fingerprint | This is the fingerprint associated with the MQTT service certificate. It must be a vector of 20 decimal values. Each value correspond to a byte part of the fingerprint. This is used to validate the MQTT server by the BearSSL library.

## Messages sent by the framework

The Maison framework automate some messages that are sent to the **maison/status** topic. All messages are sent using a JSON formatted string. 

Here is a description of each message sent, namely:

* The Startup message
* The Status message
* The Watchdog message
* The Config message

### The Startup message

This message is sent to the MQTT topic **maison/status** when the device is reset (Usually because of a Power-On action or a reset button being pressed). It is not sent when a DeepSleep wake-up action is taken by the device.

Parameter | Description
:--------:|------------------
device    | The device name as stated in the configuration parameters. If the configuration parameter is empty, the MAC address of the device WiFi interface is used.
msg_type  | This content the string "STARTUP".
reason    | The reason for startup (hardware reset type).
VBAT      | This is the Battery voltage. This parameter is optional. Its presence depend on the *VOLTAGE_CHECK* feature. See the description of the [Feature Mask](#feature-mask).

The hardware reset reason come from the ESP8266 reset information:

value | description
:----:|------------
   0  | Power Reboot
   1  | Hardware WDT Reset
   2  | Fatal Exception
   3  | Software Watchdog Reset
   4  | Software Reset
   5  | Deep Sleep Reset
   6  | Hardware Reset

Example:

```
{"device":"WATER_SPILL","msg_type":"STARTUP","reason":6,"VBAT":3.0}
```

### The Status message

This message is sent to the MQTT topic **maison/status** when a message sent to the device control topic (e.g. **maison/device_name/ctrl**) containing the string "STATE?" is received.

Parameter | Description
:--------:|------------------
device    | The device name as stated in the configuration parameters. If the configuration parameter is empty, the MAC address of the device WiFi interface is used.
msg_type  | This content the string "STATE".
state     | The current state of the finite state machine, as a number. Look into the [Finite State Machine](#the-finite-state-machine) section for details.
hours     | Hours counter. Used to compute the next 24 hours period.
millis    | Milliseconds in the last hour.
lost      | Counter of the number of time the connection to the MQTT broker has been lost.
rssi      | The WiFi signal strength of the connection to the router, in dBm.
heap      | The current value of the free heap space available on the device
VBAT      | This is the Battery voltage. This parameter is optional. Its presence depends on the *VOLTAGE_CHECK* feature. See the description of the [Feature Mask](#feature-mask).

Example:

```
{"device":"WATER_SPILL","msg_type":"STATE","state":2,"hours":7,"millis":8001,"lost":0,"rssi":-63,"heap":16704,"VBAT":3.0}
```

### The Watchdog Message

This message is sent to the MQTT topic **maison/status** every 24 hours. Its transmission is enabled through the *WATCHDOG_24H* feature. See the description of the [Feature Mask](#feature-mask).

Parameter | Description
:--------:|------------------
device    | The device name as stated in the configuration parameters. If the configuration parameter is empty, the MAC address of the device WiFi interface is used.
msg_type  | This content the string "WATCHDOG".
VBAT      | This is the Battery voltage. This parameter is optional. Its presence depend on the *VOLTAGE_CHECK* feature. See the description of the [Feature Mask](#feature-mask).

Example:

```
{"device":"WATER_SPILL","msg_type":"WATCHDOG","VBAT":3.0}
```

### The Config message

This message is sent to the MQTT topic **maison/status** when a message sent to the device control topic (e.g. **maison/device_name/ctrl**) containing the string "CONFIG?" is received.

Parameter | Description
:--------:|------------------
device    | The device name as stated in the configuration parameters. If the configuration parameter is empty, the MAC address of the device WiFi interface is used.
msg_type  | This content the string "CONFIG".
content   | This is the configuration of the device in a JSON format. See the [Configuration Parameters](#configuration-parameters) section for the format details.

Example:

```
{"device":"WATER_SPILL","msg_type":"CONFIG","content":{
  "version"          : 1,
  "device_name"      : "TEST_DEV",
  "ssid"             : "the_ssid",
  "wifi_password"    : "the_password",
  "mqtt_server_name" : "the_server_sqdn",
  "mqtt_user_name"   : "the_mqtt_user_name",
  "mqtt_password"    : "the_mqtt_password",
  "mqtt_port"        : 8883,
  "mqtt_fingerprint" : [13,217,75,226,184,245,80,117,113,43,18,251,39,75,237,77,35,65,10,19]
}}
```

## The Finite State Machine

The finite state machine is processed inside the `Maison::loop()` function.

When using the *DEEP_SLEEP* [feature](#feature-mask), networking is disabled for some of the states to minimize battery usage. If the *DEEP_SLEEP* feature is not used, networking is available all the time. The `Maison::network_is_available()` function can be used to check network availability.

State          | Value | Network | Description
:-------------:|:-----:|:-------:|-------------
STARTUP        |   1   |   YES   | The device has just been reset.
WAIT_FOR_EVENT |   2   |   NO    | This is the state waiting for an event to occur. The event is application specific.
PROCESS_EVENT  |   4   |   YES   | An event is being processed by the application. This will usually send a message to the MQTT broker.
WAIT_END_EVENT |   8   |   NO    | The device is waiting for the end of the event to occur.
END_EVENT      |  16   |   YES   | The end of an event has been detected. It's time to do an event rundown. This will usually send a message to the MQTT broker.
HOURS_24       |  32   |   YES   | This event occurs every 24 hours. It permits the transmission of a Watchdog message if enabled with the  *WATCHDOG_24H* feature. The state is required to have at least one state per day for which the network interface is energized to allow for the reception of configuration and control messages.

## Usage on battery power

The Maison framework can be tailored to use Deep Sleep when on battery power, through the *DEEP_SLEEP* [feature](#feature-mask). 

In this context, the finite state machine will cause a call to the `ESP.deep_sleep()` function at the end of each of its processing cycle (function `Maison::loop()`) to put the processor in a dormant state. The deep sleep duration, by default, is set to 5 seconds before entry to the states *PROCESS_EVENT*, *WAIT_END_EVENT*, *END_EVENT* and *HOURS_24*; it is 3600 seconds for *WAIT_FOR_EVENT*.

If the deep sleep feature is enabled, the call to `Maison::loop()` never return to the caller as the processor will reset after the deep sleep period.

It is expected that a hardware interrupt will wake up the device to indicate the arrival of a new event. If it's not the case, it will be then be required to modulate the amount of time to wait for the next *WAIT_FOR_EVENT* state to occurs. This must be used with caution as it will have an impact on the battery capacity.

The application process can change the amount of seconds for the next deep sleep period using the `Maison::set_deep_sleep_wait_time()` function. This can be called inside the application `process_state()` function before returning control to the framework. 

The ESP8266 does not allow for a sleep period longer than 4294967295 microseconds, that corresponds to around 4294 seconds or 71 minutes.

If *DEEP_SLEEP* is not used, there is no wait time other than the code processing time in the `Maison::loop()`. Internally, the framework compute the duration of execution for the next *HOURS_24* state to occur.