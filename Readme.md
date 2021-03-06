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
* Option: OTA over MQTT.
* Option: Automated Watchdog transmission every 24 hours.
* Option: Battery voltage transmission.
* Option: DeepSleep or continuous power.
* Option: Application specific MQTT topic subscription.
* Option: Application specific automatic state saving in RTC memory.
* Option: Verbose/Silent debugging output through compilation.

The MQTT based transmission architecture is specific to this implementation and is describe below.

The framework is to be used with the [PlarformIO](https://platformio.org/) ecosystem. Some examples can be found in the `examples` folder and shall be compiled through PlatformIO.

Note that the library maybe usable through the Arduino IDE, but this is not supported. It is known to be a challenge to set compiling options and access **Maison** defined types from `.ino` source code.

The **Maison** framework, to be functional, requires the following:

* Proper application setup parameters in file `platformio.ini`. Look at the [Building an Application](#2-building-an-application) section;
* Code in the user application to setup and use the framework. Look at the [Code Usage](#3-usage) section;
* Configuration parameters located in SPIFFS (file `data/config.json` in example folders). Look at the [Configuration Parameters](#5-configuration-parameters) section.

The sections below describe the specific of these requirements.

## 1. Overview

The **Maison** library supplies the usual algorithms required for an IOT device to interact within an event management architecture based on the use of a MQTT broker for message exchanges. It helps the programmer in the management of the various aspects of integrating the code responsible of the functionality of the IOT device with the intricacies of managing the lifespan inside the architecture.

(To be completed)

The following sequence diagram shows the automated interaction between the device and the MQTT broker.

![UML Sequence Diagram](./doc/sequence_uml.png)

## 2. Building an Application

The **Maison** framework is using the following libraries and, through its library configuration, automate their retrieval through the PlatformIO ecosystem:

* PubSubClient
* BearSSL
* ArduinoJSON

The PubSubClient library used is a modified version from the originator that adds the capability for message size greather than 64k. This is required to sustain OTA over MQTT.

The following options **shall** be added to the `plarformio.ini` file of your application to integrate the framework:

```yaml
lib_deps = https://github.com/turgu1/maison.git
build_flags = -D MQTT_MAX_PACKET_SIZE=1024
```

Note that *MQTT_MAX_PACKET_SIZE* can be larger depending of the application requirements.

The following options in `platformio.ini` **shall** also be used:

```yaml
framework = arduino
platform = espressif8266
```

### 2.1 Compilation Options

The **Maison** framework allow for some defined options to be modified through -D compilation parameters (PlatformIO: build_flags). The following are the compilation options available to change some library behavior:

Option              | Default   | Description
:------------------:|:---------:|------------------------------------------------------------------------
MAISON_TESTING      |    0      | If = 1, enable debugging output through the standard Serial port. The serial port must be initialized by the application (Serial.begin()) before calling any **Maison** function. This will put all other *XXX_TESTING* options to 1, unless they have been previously defined.
NET_TESTING         |    0      | If = 1, enable network related debugging output through the standard Serial port. The serial port must be initialized by the application (Serial.begin()) before calling any **Maison** function.
OTA_TESTING         |    0      | If = 1, enable OTA related debugging output through the standard Serial port. The serial port must be initialized by the application (Serial.begin()) before calling any **Maison** function.
JSON_TESTING        |    0      | If = 1, enable JSON related debugging output through the standard Serial port. The serial port must be initialized by the application (Serial.begin()) before calling any **Maison** function.
QUICK_TURN          |    0      | If = 1, HOURS_24 state is fired every 2 minutes instead of 24 hours. This is automatically the case when any *XXX_TESTING* is set to 1, unless QUICK_TURN is also defined in build_flags.
MAISON_PREFIX_TOPIC | maison | All topics used by the framework are prefixed with this text
MAISON_STATE_TOPIC  | state | Topic suffix where the framework state are sent
MAISON_EVENT_TOPIC  | event | Topic suffix where the framework events are sent
MAISON_CONFIG_TOPIC | config | Topic suffix where the framework configuration are sent
MAISON_LOG_TOPIC    | log | Topic suffix where free text log messages are sent
MAISON_CTRL_TOPIC   | ctrl | This is the topic suffix used to identify device-related control topic
DEFAULT_SHORT_REBOOT_TIME |  5  | This is the default reboot time in seconds when deep sleep is enable. This is used at the end of the following states: *PROCESS_EVENT*, *WAIT_END_EVENT*, *END_EVENT*. For the other states, the wait time is 60 minutes (3600 seconds).
MQTT_OTA            | 0 | Allow for Over the Air (OTA) code update through MQTT see section MQTT OTA for further details.
APP_NAME | UNKNOWN | Application name. Required for MQTT OTA as a mean to check the new binary to be compatible with the current.
APP_VERSION | 1.0.0 | Application version number.
MAISON_SECURE | 1 | If = 1 WiFi TLS encryption is used for all communications.

The framework will subscribe to MQTT messages coming from the server on a topic built using *MAISON_PREFIX_TOPIC*, the device MAC address and *MAISON_CTRL_TOPIC*. For example, if the device MAC address is "DE01F3003571", the subscribed topic would be `maison/DE01F3003571/ctrl`.

The *SERIAL_NEEDED* flag can be checked by the user application to verify if any of the *XXX_TESTING* options has been set to 1. Usefull to initialize the serial port through the Serial.begin() method.

## 3. Usage

The **Maison** framework is expecting the following aspects to be properly in place for its usage on a device:

1. [Application Source Code](#4-application-source-code) with the **Maison** framework integration.
2. A [Configuration Parameters](#5-configuration-parameters) file.
3. A [MQTT broker](#6-mqtt-broker) on a networked server.

The following sections explain each of this elements.

## 4. Application Source Code

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

This piece of code won't do much at the user application level, but it will set the scene to the automation of exchanges with a MQTT message broker, sending startup/watchdog messages, answering information requests, changes of configuration, completely automated through the **Maison** framework.

Here is a more complete example of code to be used to initialize the framework with optional features and integrate it in the loop() function. It shows both option parameters, calls to the framework with message callback and finite state machine functions:

```C++
ADC_MODE(ADC_VCC);

#include <Maison.h>

struct user_data {
  uint32_t crc;
  int my_data;
  ...
} my_state;

Maison maison(Maison::Feature::WATCHDOG_24H | Maison::Feature::VOLTAGE_CHECK,
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

### 4.1 Include File

The `#include <Maison.h>` integrates the **Maison** header into the user application. This will import the **Maison** class declaration and a bunch of definitions that are documented below. All required libraries needed by the framework are also included by this call.

### 4.2 **Maison** Declaration

The `Maison maison(...)` declaration creates an instance of the framework. This declaration accepts the following parameters:

* An optional [feature mask](#421-feature-mask), to enable some aspects of the framework (see table below).
* An optional [user application state structure](#422-user-application-state-structure) (here named `my_state`) and it's size to be automatically saved in non-volatile memory when DeepSleep is enabled.

#### 4.2.1 Feature Mask

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

#### 4.2.2 User Application State Structure

The user application state structure (here named `user_data`) **shall** have a `uint32_t` item as the first element in the structure. This is used by the framework to verify that the content saved in non-volatile memory is valid using a CRC-32 checksum. The whole content will be initialized (zeroed) if the checksum is bad. The checksum is computed by the framework, the user application just need to supplied the space in the structure.

This structure is optional and could be required by the application when the *DEEP_SLEEP* feature is selected. It will allow for the saving and retrieval of the current application state as the Deep Sleep feature induce processor resets that invalidate memory content.

### 4.3 maison.loop()

The Maison::loop() function must be called regularly in the user application main loop function to permit the execution of the finite state machine and the receiving of new MQTT messages. As a parameter, the Maison::loop() function accepts a processing function that will be called by **Maison** inside the finite state machine. The function will receive the current state value as a parameter. It must return a status value from the following list:

Value         | Description
:------------:|----------------
COMPLETED     |  Returned when the processing for the current state is considered completed. This is used mainly for all states.
NOT_COMPLETED | The reverse of *COMPLETED*. Mainly used with *PROCESS_EVENT* in the case that it must be fired again to complete the processing
ABORTED       | Return in the case of *PROCESS_EVENT* when the event vanished before processing, such that the finite state machine return to the *WAIT_FOR_EVENT* state instead of going to the *WAIT_END_EVENT* state.
NEW_EVENT     | Returned when processing a *WAIT_FOR_EVENT* state to indicate that an event must be processed.
RETRY         | When in state *WAIT_END_EVENT*, will return back to *PROCESS_EVENT* to check again for event processing

Note: if the *DEEP_SLEEP* feature was enabled, the loop will almost never return as the processor will wait for further processing through a call to ESP.deep_sleep function. The processor, after the wait time, will restart the code execution from the beginning.

## 5. Configuration Parameters

The **Maison** framework is automating access to the MQTT message broker through the WiFi connection. As such, parameters are required to link the device to the WiFi network and the MQTT broker server. A file named "/config.json" must be created on a SPIFFS file system in flash memory. This is a JSON structured file. Here is an example of such a file:

```json
{
  "version" : 1,
  "device_name" : "WATER_SPILL",
  "ssid" : "the wifi ssid",
  "wifi_password" : "the wifi password",
  "ip" : "192.168.1.71",
  "dns" : "192.168.1.1",
  "gateway" : "192.168.1.1",
  "subnet_mask" : "255.255.255.0",
  "mqtt_server_name" : "the MQTT server name or IP address",
  "mqtt_user_name" : "the MQTT user name",
  "mqtt_password" : "the MQTT user password",
  "mqtt_port" : 8883,
  "mqtt_fingerprint" : [13,217,75,226,184,245,80,117,113,43,18,251,39,75,237,77,35,65,10,19]
}
```

All parameters must be present in the file to be considered valid by the framework. Here is a description of each parameter:

Parameter | Description
:--------:|------------------------------
version | This is the sequential version number. This is the property of the Server responsible of transmitting new configuration files to the device. It must be incremented every time a new configuration file is sent to the device. The device will not update its configuration if the version number is not greater than the current one. Unsigned Integer value (16 bits).
device_name | A unique identifier for the device. This identifier is used inside messages sent through MQTT. It can be an empty string: the MAC address of the device WiFi interface will then be used as the identifier. Use letters, underscore, numbers to compose the identifier (no space or other special characters). Max length: 15 ASCII characters.
ssid / wifi_password | The WiFi SSID and password. Required to reach the network. Max length: 15 ASCII characters each.
ip | The ip address to set for the WiFi connection. If an empty string or equal to "0.0.0.0", the device will get its IP, dns, gateway adresses and subnet_mask from the network through DHCP.
dns | The dns server IP address. Can be set to an empty string.
gateway | The gateway (router) IP address. Can be set to an empty string.
subnet_mask | The subnet mask. Can be set to an empty string.
mqtt_server_name | This is the MQTT server name (SQDN) or IP address.  Max length: 31 ASCII characters.
mqtt_user_name / mqtt_password | These are the credentials to connect to the MQTT server. Max length: 15 ASCII characters for user_name, 31 ASCII characters for password.
mqtt_port | The TLS/SSL port number of the MQTT server. Unsigned Integer value (16 bits).
mqtt_fingerprint | This is the fingerprint associated with the MQTT service certificate. It must be a vector of 20 decimal values. Each value correspond to a byte part of the fingerprint. This is used to validate the MQTT server through the BearSSL library. Length: 20 bytes. If empty, no check will be done on the server validity. Not used if MAISON_SECURE=0.

### 5.1 PlatformIO configuration

A SPIFFS flash file system must be put in place on the targeted device. This can be accomplished through the following process, as described in the [platformio documentation](https://docs.platformio.org/en/latest/platforms/espressif8266.html#uploading-files-to-file-system-spiffs):

1. Put the **config.json** in a folder named `data` located at the same level as for the application `src` folder.

2. Add the following compilation options to the `build_flags` element of the **platformio.ini** file:

   ```ini
   -Wl,-Teagle.flash.4m1m.ld
   ```

3. Connect the device to the computer

4. Kill the PlatformIO Serial Monitor. The next step won't work if the Serial Monitor is working.

5. Initiate the flash memory preparation of the SPIFFS file system using the PlatformIO "Upload File System image" task from the IDE.

## 6. MQTT Broker

(To be completed)

## 7. Messages sent by the framework

All MQTT messages transmitted/received by the application are using a topic names composed with the following information:

* The MAISON_PREFIX_TOPIC value. Default is "maison". It can be redefined through a new #define definition.
* The device_id: This is the hexadecimal MAC address of the device.
* A suffix name as presented in the following sub-sections.

The **Maison** framework automate some messages that are sent to **maison/device_id/xxx** topics. All message contents, but log messages, are sent using a JSON formatted string. Log messages are sent as free text.

Here is a description of each message sent, namely:

* The Startup message
* The Status message
* The Watchdog message
* The Config message
* Log messages

### 7.1 The Startup message

This message is sent to the MQTT topic **maison/device_id/state** when the device is reset (Usually because of a Power-On action or a reset button being pressed). It is not sent when a DeepSleep wake-up action is taken by the device.

Parameter | Description
:--------:|------------------
device    | The device name as stated in the configuration parameters. If the configuration parameter is empty, the MAC address of the device WiFi interface is used.
msg_type  | This content the string "STARTUP".
ip        | The device WiFi IP adress.
mac       | The device MAC address.
reason    | The reason for startup (hardware reset type).
state     | The current state of the finite state machine, as a number. Look into the [Finite State Machine](#8-the-finite-state-machine) section for details.
return_state | The state to return to after *HOURS_24* processing.
hours     | Hours counter. Used to compute the next 24 hours period.
millis    | Milliseconds in the last hour.
lost      | Counter of the number of time the connection to the MQTT broker has been lost.
rssi      | The WiFi signal strength of the connection to the router, a relative signal quality measurement. -50 means a pretty good signal, -75 fearly reasonnable and -100 means no signal.
heap      | The current value of the free heap space available on the device
VBAT      | This is the Battery voltage. This parameter is optional. Its presence depends on the *VOLTAGE_CHECK* feature. See the description of the [Feature Mask](#421-feature-mask).
app_name | The name of the application. This is the functional name of the application, used for MQTT OTA updates. Will be showned only when MQTT_OTA is enabled.
app_version | The code version number. Will be showned only when MQTT_OTA is enabled.

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

```json
{"device":"WATER_SPILL","msg_type":"STARTUP","ip":"192.168.1.71","mac":"2B:1D:03:31:2A:54","state":32,"return_state":2,"hours":7,"millis":8001,"lost":0,"rssi":-63,"heap":16704,"app_name":"BITSENSOR","app_version":"1.0.1","VBAT":3.0}
```

### 7.2 The Status message

This message is sent to the MQTT topic **maison/device_id/state** when a message sent to the device control topic (e.g. **maison/device_id/ctrl**) containing the string "STATE?" is received. It is similar to the Startup message, with msg_type set to "STATE".

Example:

```json
{"device":"WATER_SPILL","msg_type":"STATE","ip":"192.168.1.71","mac":"2B:1D:03:31:2A:54","state":32,"return_state":2,"hours":7,"millis":8001,"lost":0,"rssi":-63,"heap":16704,"app_name":"BITSENSOR","app_version":"1.0.1","VBAT":3.0}
```

### 7.3 The Watchdog Message

This message is sent to the MQTT topic **maison/device_id/state** every 24 hours. Its transmission is enabled through the *WATCHDOG_24H* feature. See the description of the [Feature Mask](#421-feature-mask).  It is similar to the Startup message, with msg_type set to "WATCHDOG".

Example:

```json
{"device":"WATER_SPILL","msg_type":"WATCHDOG","ip":"192.168.1.71","mac":"2B:1D:03:31:2A:54","state":32,"return_state":2,"hours":7,"millis":8001,"lost":0,"rssi":-63,"heap":16704,"app_name":"BITSENSOR","app_version":"1.0.1","VBAT":3.0}
```

### 7.4 The Config message

This message is sent to the MQTT topic **maison/device_id/config** when a message sent to the device control topic (e.g. **maison/device_id/ctrl**) containing the string "CONFIG?" is received.

Parameter | Description
:--------:|------------------
device    | The device name as stated in the configuration parameters. If the configuration parameter is empty, the MAC address of the device WiFi interface is used.
msg_type  | This content the string "CONFIG".
content   | This is the configuration of the device in a JSON format. See the [Configuration Parameters](#5-configuration-parameters) section for the format details.

Example:

```json
{"device":"WATER_SPILL","msg_type":"CONFIG","content":{
  "version"          : 1,
  "device_name"      : "WATER_SPILL",
  "ssid"             : "the_ssid",
  "wifi_password"    : "the_password",
  "ip"               : "192.168.1.71",
  "dns"              : "192.168.1.1",
  "gateway"          : "192.168.1.1",
  "subnet_mask"      : "255.255.255.0",
  "mqtt_server_name" : "the_server_sqdn",
  "mqtt_user_name"   : "the_mqtt_user_name",
  "mqtt_password"    : "the_mqtt_password",
  "mqtt_port"        : 8883,
  "mqtt_fingerprint" : [13,217,75,226,184,245,80,117,113,43,18,251,39,75,237,77,35,65,10,19]
}}
```

### 7.5 Log messages

Log messages are sent to the MQTT topic **maison/device_id/log** as non-formatted text messages. They are mainly used for OTA code reception aknowledges for debugging purposes.

## 8. The Finite State Machine

The finite state machine is processed inside the `Maison::loop()` function.

When using the *DEEP_SLEEP* [feature](#421-feature-mask), networking is disabled for some of the states to minimize battery usage. If the *DEEP_SLEEP* feature is not used, networking is available all the time. The `Maison::network_is_available()` function can be used to check network availability.

State          | Value | Network | Description
:-------------:|:-----:|:-------:|-------------
STARTUP        |   1   |   YES   | The device has just been reset.
WAIT_FOR_EVENT |   2   |   NO    | This is the state waiting for an event to occur. The event is application specific.
PROCESS_EVENT  |   4   |   YES   | An event is being processed by the application. This will usually send a message to the MQTT broker.
WAIT_END_EVENT |   8   |   NO    | The device is waiting for the end of the event to occur.
END_EVENT      |  16   |   YES   | The end of an event has been detected. It's time to do an event rundown. This will usually send a message to the MQTT broker.
HOURS_24       |  32   |   YES   | This event occurs every 24 hours. It permits the transmission of a Watchdog message if enabled with the  *WATCHDOG_24H* feature. The state is required such that at least one wakeup per day occurs for which the network interface is energized to allow for the reception of configuration, new application code (if OTA is enabled) or control messages.

Here is a state diagram showing the inter-relationship between each state and the corresponding application process return values for witch state changes will be fired:

![UML State Diagram](./doc/state_uml.png)

A user defined processing function will be called by **Maison** inside the finite state machine. This function must be supplied as a parameter to the maison.loop() method. The function will receive the current state value as a parameter and take appropriate action considering the current state. It must return a status value from the following list:

Value         | Valid states |Description
:------------:|:----------------:|----------------
COMPLETED     | STARTUP PROCESS_EVENT WAIT_END_EVENT END_EVENT HOURS_24 | Returned when the processing for the current state is considered completed. This is used mainly for all states.
NOT_COMPLETED | all states | The reverse of *COMPLETED*. Mainly used with *PROCESS_EVENT* in the case that it must be fired again to complete the processing
ABORTED       | PROCESS_EVENT | Return in the case of *PROCESS_EVENT* when the event vanished before processing, such that the finite state machine return to the *WAIT_FOR_EVENT* state instead of going to the *WAIT_END_EVENT* state.
NEW_EVENT     | WAIT_FOR_EVENT | Returned when processing a *WAIT_FOR_EVENT* state to indicate that an event must be processed.
RETRY         | WAIT_END_EVENT | When in state *WAIT_END_EVENT*, will return back to *PROCESS_EVENT* to check again for event processing

Note that HOURS_24 is not taking any action on the received value. This state is entered automatically when it's the time for it to be fired. It returns back to the preceeding state once executed.

The HOURS_24 state exact time to have it fired is not selectable. The ESP8266 doesn't have any RTC and the internal timer is not accurate enough to ensure proper synchronization with the time of day.

## 9. Usage on battery power

The **Maison** framework can be tailored to use Deep Sleep when on battery power, through the *DEEP_SLEEP* [feature](#421-feature-mask).

In this context, the finite state machine will cause a call to the `ESP.deep_sleep()` function at the end of each of its processing cycle (function `Maison::loop()`) to put the processor in a dormant state. The deep sleep duration, by default, is set to 5 seconds before entry to the states *PROCESS_EVENT*, *WAIT_END_EVENT*, *END_EVENT* and *HOURS_24*; it is 3600 seconds for *WAIT_FOR_EVENT*.

If the deep sleep feature is enabled, the call to `Maison::loop()` never return to the caller as the processor will reset after the deep sleep period.

It is expected that a hardware interrupt will wake up the device to indicate the arrival of a new event. If it's not the case, it will be then be required to modulate the amount of time to wait for the next *WAIT_FOR_EVENT* state to occurs. This must be used with caution as it will have an impact on the battery capacity.

The application process can change the amount of seconds for the next deep sleep period using the `Maison::set_deep_sleep_wait_time()` function. This can be called inside the application `process_state()` function before returning control to the framework.

As the device will be in a deep sleep state almost all the time, it becomes more difficult for it to get messages from the MQTT broker. Messages to be read by the device must then be using Qos (quality of service) of 1 to have them delivered when the device will be ready to receive them (network is running and the message callback is in operation). When connecting to the broker, **Maison** will connect with the cleanup flag to false, indicating the need to keep what is in the queue for retrieval after sleep time. The MQTT broker uses the client_name as the id to manage persistency. As such, it is required to be different than any other device name. When no device name is supplied in the config file (empty string), **Maison** uses the mac address as the device name. Insure that when you set the device name, it is unique amongst your devices. **Maison** prefix it with "client-" and send it to the MQTT broker at connection time.

The ESP8266 does not allow for a sleep period longer than 4294967295 microseconds, that corresponds to around 4294 seconds or 71 minutes.

If *DEEP_SLEEP* is not used, there is no wait time other than the code processing time in the `Maison::loop()`. Internally, the framework compute the duration of execution for the next *HOURS_24* state to occur.

## 10. MQTT OTA

The **Maison** framework allows for code update through a MQTT firmware transmission protocol (Over The Air, or OTA). As such, the following aspects must be properly setup:

1. The compilation option MQTT_OTA must be set to 1

2. The APP_NAME compilation option must be set to the functional name of the code. This name will need to be present in the NEW_CODE command shown below.

3. The code must be updated on the chip through a serial (FTDI) connexion at least once. After that, it will be possible to upload new codes through MQTT. Do not omit to power reset the device after the update as a reset from an FTDI upload doesn't allow for OTA update.

To update the code, two messages in a single sequence must be sent to topic **maison/device_id/ctrl** with qos 1. The first one will contain a json structure prefixed with "NEW_CODE:" that will have the following fields:

Field Name | Description
-----------|------------
SIZE       | The size of the firmware to be sent as a number of bytes
APP_NAME   | The name of the application
MD5        | The MD5 message digest (fingerprint) of the file to be sent (string of 32 characters)

Here is an example of such a message:

```json
NEW_CODE:{"SIZE":412345,"APP_NAME":"BLINKER","MD5":"06fa77583b007464167bbba866d662c2"}
```

Once received, the device will send a log message. For example::

```text
DEVICE_NAME: Code update started with size 412345 and md5: 06fa77583b007464167bbba866d662c2.
```

The second message sent to the device will contain the binary code of the file. Its length must be the same indicated by the SIZE parameter shown above.

Once the code has been received, the device will send a log message. For example:

```text
DEVICE_NAME: Code upload completed. Rebooting
```

A shell script (located in the `tools/upload.sh` file) that help in the automated transmission of a new firmware is supplied with the framework. Some parameters must be modified according to the targetted MQTT broker configuration to make it usable.
