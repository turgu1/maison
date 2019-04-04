#ifndef _MAISON_
#define _MAISON_

#include <Arduino.h>

#include <FS.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <stdio.h>
#include <stdarg.h>

#ifndef APP_NAME
  #define APP_NAME "UNKNOWN"
#endif

#ifndef APP_VERSION
  #define APP_VERSION "1.0.0"
#endif

#ifndef MQTT_OTA
  #define MQTT_OTA 0
#endif

// Insure that MQTT Packet size is big enough for the needs of the framework
// This is an option of the PubSubClient library that can be set through platformio.ini

#if MQTT_MAX_PACKET_SIZE < 1024
  #error "MQTT_MAX_PACKET_SIZE MUST BE AT LEAST 1024 IN SIZE."
#endif

// ----- OPTIONS -----
//
// To be set in the platformio.ini file

// If MAISON_TESTING is != 0, the Serial port must be properly initialized by
// the user sketch before calling Maison::setup(). For example:
//
// Serial.begin(9600)
//
// If MAISON_TESTING is 0, the framework won't use the serial port at all.

#ifndef MAISON_TESTING
  #define MAISON_TESTING 0
#endif

#ifndef QUICK_TURN
  #if MAISON_TESTING
    #define QUICK_TURN 1
  #else
    #define QUICK_TURN 0
  #endif
#endif

// This is prepended to all topics used by the framework

#ifndef MAISON_PREFIX_TOPIC
  #define MAISON_PREFIX_TOPIC "maison" ///< Prefix for all Maison framework MQTT topics
#endif

// This is the topic name suffix to send state information to
//
// For example: maison/xxx/state

#ifndef MAISON_STATE_TOPIC
  #define MAISON_STATE_TOPIC "state" ///< maison server state MQTT topic suffix
#endif

// This is the topic name suffix to send configuration to
//
// For example: maison/xxxx/config

#ifndef MAISON_CONFIG_TOPIC
  #define MAISON_CONFIG_TOPIC "config" ///< maison server config MQTT topic suffix
#endif

// This is the topic name suffix to send event information to
//
// For example: maison/xxx/event

#ifndef MAISON_EVENT_TOPIC
  #define MAISON_EVENT_TOPIC "event" ///< maison server event MQTT topic suffix
#endif

// This is the topic name suffix to send log (free text) information to
//
// For example: maison/xxx/log

#ifndef MAISON_LOG_TOPIC
  #define MAISON_LOG_TOPIC "log" ///< maison server log MQTT topic suffix
#endif

// This is the topic name suffix where the device wait for control commands from site
// The completed topic is build using:
//
// 1) MAISON_PREFIX_TOPIC
// 2) The device name from config or MAC address if device name is empty
// 3) MAISON_CTRL_TOPIC
//
// For example: maison/DEV_TEST/crtl

#ifndef MAISON_CTRL_TOPIC
  #define MAISON_CTRL_TOPIC "ctrl" ///< Suffix for device control topic
#endif

#ifndef DEFAULT_SHORT_REBOOT_TIME
  #define DEFAULT_SHORT_REBOOT_TIME 5  ///< DeepSleep time in seconds for short time states
#endif

// ----- END OPTIONS -----

#if MAISON_TESTING
  #define       DEBUG(a) Serial.print(a)
  #define     DEBUGLN(a) Serial.println(a)
  #define        SHOW(f) DEBUGLN(F(f));
  #define SHOW_RESULT(f) DEBUG(F(" Result " f ": ")); DEBUGLN(result ? F("success") : F("FAILURE"))
#else
  #define       DEBUG(a)
  #define     DEBUGLN(a)
  #define        SHOW(f)
  #define SHOW_RESULT(f)
#endif

#define    STRINGIZE(a) #a
#define       DEBUG2(a) Serial.print(a)
#define     DEBUGLN2(a) Serial.println(a)

// Syntactic sugar

#define DO        bool result = false; while (true)    ///< Beginning of a DO loop
#define OK_DO     { result = true; break; }            ///< Successful exit of a DO loop
#define ERROR(m)  { DEBUGLN(F(" ERROR: " m)); break; } ///< Exit the loop with an ERROR message

// To get WATCHDOG Time faster during tests

#if QUICK_TURN
  #define ONE_HOUR 6   ///< In seconds. So *HOURS_24* fired every 2.2 minutes.
#else
  #define ONE_HOUR 3600 ///< In seconds. Normal is one hour x 24 = 24 hours.
#endif

/// Maison implements a framework that simplify the creation of IOT devices
/// using an ESP8266 processor. It implements a protocol of exchange of
/// information with a MQTT broker that serves as a bridge between a device
/// management server and a variety of embedded devices.
///
/// Please refer to the Main Page of information in this API documentation for
/// a detailed description.
///
/// The following sequence diagram show the interaction of devices implementing
/// the protocol and an MQTT broker:
///
/// @startuml{sequence_uml.png} "Maison Sequence Diagram" width=8cm
///
/// actor Device
/// actor device_ctrl as "maison/WATER_SPILL/ctrl"
/// actor server_status as "maison/status"
/// actor server_log as "maison/log"
///
/// group Device actions
///   group Reset
///     Device --> server_status : "{Startup}"
///   end
///   loop Every 24 hours
///     Device --> server_status : "{Watchdog}"
///   end
/// end
///
/// group Server Requests
///   group Device State
///     device_ctrl --> Device : "STATE?"
///     Device --> server_status : "{State}"
///   end
///
///   group Current Config
///     device_ctrl --> Device : "CONFIG?"
///     Device --> server_status : "{config}"
///   end
///
///   group Set Config
///     device_ctrl --> Device : "CONFIG: {config}"
///     Device --> server_status : "{config}"
///   end
///
///   group Reset device
///     device_ctrl --> Device : "RESTART!"
///   end
///
///   group OTA
///     device_ctrl --> Device : "NEW_CODE: {params}"
///     Device --> server_log : "OTA Start"
///     device_ctrl --> Device : "<binary>"
///     Device --> server_log : "Completed. Reboot."
///   end
/// end
/// @enduml

class Maison
{
  public:

    /// Features in feature_mask
    ///
    /// Note: if *VOLTAGE_CHECK* is requested and the internal battery readout is targeted, the main
    ///       user sketch must add the following line at the beginning of the sketch source code file:
    ///
    ///    ADC_MODE(ADC_VCC);
    enum Feature : uint8_t {
      NONE          = 0x00, ///< No special feature
      VOLTAGE_CHECK = 0x01, ///< Chip A2D voltage readout will be sent on status/watchdog messages
      DEEP_SLEEP    = 0x02, ///< Using batteries -> deep_sleep will be used then
      WATCHDOG_24H  = 0x04  ///< Watchdog status sent every 24 hours
    };

    /// States of the finite state machine
    ///
    /// On battery power (*DEEP_SLEEP* feature is set), only the following
    /// states will have networking capability:
    ///
    ///    *STARTUP*, *PROCESS_EVENT*, *END_EVENT*, *HOURS_24*
    ///
    /// @startuml{state_uml.png} "Maison State Diagram" width=5cm
    /// hide empty description
    /// [*] --> STARTUP
    /// STARTUP --> WAIT_FOR_EVENT : COMPLETED
    /// STARTUP --> STARTUP : NOT-COMPLETED
    /// WAIT_FOR_EVENT -right-> PROCESS_EVENT : NEW_EVENT
    /// WAIT_FOR_EVENT --> HOURS_24
    /// HOURS_24 --> WAIT_FOR_EVENT
    /// PROCESS_EVENT --> WAIT_END_EVENT : COMPLETED
    /// PROCESS_EVENT --> PROCESS_EVENT : NOT_COMPLETED
    /// PROCESS_EVENT -left-> WAIT_FOR_EVENT : ABORTED
    /// PROCESS_EVENT --> HOURS_24
    /// HOURS_24 --> PROCESS_EVENT
    /// WAIT_END_EVENT --> END_EVENT : COMPLETED
    /// WAIT_END_EVENT --> PROCESS_EVENT : RETRY
    /// WAIT_END_EVENT --> HOURS_24
    /// WAIT_END_EVENT --> WAIT_END_EVENT : NOT_COMPLETED
    /// HOURS_24 --> WAIT_END_EVENT
    /// END_EVENT --> WAIT_FOR_EVENT : COMPLETED
    /// END_EVENT --> END_EVENT : NOT_COMPLETED
    /// @enduml

    enum State : uint8_t {
      STARTUP        =  1, ///< The device has just been reset
      WAIT_FOR_EVENT =  2, ///< Wait for an event to occur
      PROCESS_EVENT  =  4, ///< An event is being processed
      WAIT_END_EVENT =  8, ///< The device is waiting for the end of the event to occur
      END_EVENT      = 16, ///< The end of an event has been detected
      HOURS_24       = 32  ///< This event occurs every 24 hours
    };

    /// A UserResult is returned by the user process function to indicate
    /// how to proceed with the finite state machine transformation. Please
    /// look at the State enumeration description where a state diagram show the
    /// relationship between states and values returned by the user process function.

    enum UserResult : uint8_t {
      COMPLETED = 1, ///< The processing for the current state is considered completed
      NOT_COMPLETED, ///< The current state still require some processing in calls to come
      ABORTED,       ///< The event vanished and requires no more processing (in a *PROCESS_EVENT* state)
      NEW_EVENT,     ///< A new event occurred (in a *WAIT_FOR_EVENT* state)
      RETRY          ///< From WAIT_END_EVENT, go back to PROCESS_EVENT
    };

    /// Application defined process function. To be supplied as a parameter
    /// to the Maison::loop() function.
    /// @param[in] _state The current state of the Finite State Machine.
    /// @return The status of the user process execution.

    typedef UserResult Process(State _state);

    /// Application defined MQTT message callback function. Will be called by the framework
    /// when receiving topics not managed by the framework itself.
    ///
    /// @param[in] _topic The topic related to the received payload
    /// @param[in] _payload The message content. May not have a zero byte at the end...
    /// @param[in] _length The payload size in bytes.

    typedef void Callback(const char * _topic, byte * _payload, unsigned int _length);

    Maison();
    Maison(uint8_t _feature_mask);
    Maison(uint8_t _feature_mask, void * _user_mem, uint16_t _user_mem_length);

    /// The Maison setup function. Normally to be called inside the application setup()
    /// function.
    /// @return True if setup completed successfully.

    bool setup();

    /// Send a MQTT message using printf like construction syntax.
    ///
    /// @param[in] _topic The message topic
    /// @param[in] _format The format string, as for printf
    /// @param[in] ... The arguments required by the format string
    /// @return True if the message was sent successfully

    bool send_msg(const char * _topic_suffix, const __FlashStringHelper * _format, ...);

    /// Send a MQTT log msg using printf like construction syntax.
    ///
    /// @param[in] _topic The message topic
    /// @param[in] _format The format string, as for printf (PROGMEM)
    /// @param[in] ... The arguments required by the format string
    /// @return True if the message was sent successfully

    bool log(const __FlashStringHelper * _format, ...);

    /// Returns the ESP8266 reason for reset. The following table lists the ESP8266
    /// potential reasons for reset:
    ///
    ///  value | description
    ///  :----:|------------
    ///     0  | Power Reboot
    ///     1  | Hardware WDT Reset
    ///     2  | Fatal Exception
    ///     3  | Software Watchdog Reset
    ///     4  | Software Reset
    ///     5  | Deep Sleep Reset
    ///     6  | Hardware Reset
    ///
    /// @return The reason of the reset as a number.

    int reset_reason();

    /// Will restart the ESP8266

    void restart();

    /// Initiates an `ESP.deep_sleep()` call. This function never suppose to return...
    ///
    /// @param[in] _back_with_wifi True if WiFi networking enabled on restart
    /// @param[in] _sleep_time_in_sec The number of second to wait before restart

    void deep_sleep(bool _back_with_wifi, uint16_t _sleep_time_in_sec);

    /// Enable a feature dynamically.
    ///
    /// @param[in] _feature The feature to be enabled.

    inline void  enable_feature(Feature _feature) {
      feature_mask |= _feature;
    }

    /// Disable a feature dynamically.
    ///
    /// @param[in] _feature The feature to be disabled.

    inline void disable_feature(Feature _feature) {
      feature_mask &= ~_feature;
    }

    /// Returns the battery voltage. Requires ADC_MODE(ADC_VCC); at the
    /// beginning of the application sketch.
    ///
    /// @return The battery voltage as read from the ESP8266 ESP.getVcc() call

    inline float battery_voltage() {
      return (ESP.getVcc() * (1.0 / 1024.0));
    }

    /// Check if a reset is due to something else than Deep Sleep return.
    ///
    /// @return True if a reset occurred that is not coming from a Deep Sleep return.

    inline bool is_hard_reset() {
      return reset_reason() != REASON_DEEP_SLEEP_AWAKE;
    }

    /// Set the deep_sleep period inside an application process function.
    ///
    /// @param[in] _seconds The number of seconds to wait inside the next Deep Sleep call.

    inline void set_deep_sleep_wait_time(uint16_t _seconds) {
      deep_sleep_wait_time = (_seconds > 4294) ? 4294 : _seconds;
    }

    /// Checks if networking is currently available. Always true if *DEEP_SLEEP*
    /// is not set in the features.
    ///
    /// @return True if the network is enabled.

    inline bool network_is_available() {
      return (!use_deep_sleep()) ||
             ((mem.state & (STARTUP|PROCESS_EVENT|END_EVENT|HOURS_24)) != 0);
    }

    /// Get elapsed time since the last call to user process in the preceding loop call.
    /// @return Elapsed time in microseconds.

    inline long last_loop_duration() {
      return mem.elapse_time;
    }

    /// Returns a complete device related topic name, built using the default prefix and
    /// the device_name. The string will contain a zero byte at the end. If the buffer is too
    /// small, it will return a zero-length string.
    ///
    /// For example, a call with topic_suffix = "hello" will result to the following topic:
    ///
    ///   ```
    ///   maison/device_name/hello
    ///   ```
    ///
    /// @param[in]  _topic_suffix The topic suffix portion to build the complete topic from.
    /// @param[out] _buffer Where the topic name will be built.
    /// @param[in]  _length The size of the buffer.
    /// @return pointer to the beginning of the buffer.

    char * build_topic(const char * _topic_suffix, char * _buffer, uint16_t _length);

    /// Compute a CRC-32 checksum
    ///
    /// @param[in] _data The data vector to compute the checksum on.
    /// @param[in] _length The size of the data vector.
    /// @return The computed CRC-32 checksum.

    uint32_t CRC32(const uint8_t * _data, size_t _length);

    /// Set the MQTT message callback for the user application.
    ///
    /// @param[in] _cb The Callback function address.
    /// @param[in] _sub_topic The sub_topic to subscribe to.
    /// @param[in] _qos The QOS for the subscription.

    void set_msg_callback(Callback * _cb, const char * _sub_topic, uint8_t _qos = 0);

    /// Get device name. The device name is retrieve from the configuration and
    /// sent back to the user as a constant string.
    ///
    /// @return The device name as a constant string

    const char * get_device_name() { return config.device_name; }

    /// Function to be called by the application in the main loop to insure
    /// proper actions by the framework.
    ///
    /// @param[in] _process The application processing function. This function will be
    ///                    called by the framework every time the Maison::loop() is called,
    ///                    just before processing the finite state machine.

    void loop(Process * _process = NULL);

  private:
    // The configuration is read at setup time from file "/config.json" in the SPIFFS flash
    // memory space. It is updated by the home management tool through MQTT topic
    // named "maison/" + device_name + "_ctrl"

    struct Config {
      uint16_t         version    ;
      uint16_t       mqtt_port    ;
      char         device_name[16];
      char           wifi_ssid[16];
      char       wifi_password[16];
      uint32_t                  ip;
      uint32_t         subnet_mask;
      uint32_t             gateway;
      uint32_t                 dns;
      char         mqtt_server[32];
      char       mqtt_username[16];
      char       mqtt_password[32];
      uint8_t mqtt_fingerprint[20];
    } config;

    struct mem_struct {
      uint32_t csum;
      State    state;
      State    return_state;
      uint16_t hours_24_count;      // Up to 24 hours
      uint16_t lost_count;          // How many MQTT lost connections since reset
      uint32_t one_hour_step_count; // Up to 3600 seconds in milliseconds
      uint32_t elapse_time;
      uint32_t magic;
    } mem;

    PubSubClient                mqtt_client;
    BearSSL::WiFiClientSecure * wifi_client;

    long         last_reconnect_attempt;
    int          connect_retry_count;
    bool         first_connect_trial;
    Callback   * user_callback;
    const char * user_sub_topic;
    uint8_t      user_qos;
    uint8_t      feature_mask;
    void       * user_mem;
    uint16_t     user_mem_length;
    long         last_time_count;
    bool         counting_lost_connection;
    uint16_t     deep_sleep_wait_time;
    uint32_t     loop_time_marker;
    bool         some_message_received;
    bool         wait_for_completion;
    bool         reboot_now;  // reboot after code update
    bool         restart_now; // restart after saving the state

    char         buffer[MQTT_MAX_PACKET_SIZE];
    char         topic[60];
    char         user_topic[60];

    bool wifi_connect();
    bool mqtt_connect();

    void wifi_flush();

    friend void maison_callback(const char * _topic, byte * _payload, unsigned int _length);
    void process_callback(const char * _topic, byte * _payload, unsigned int _length);

    inline bool   wifi_connected() { return WiFi.status() == WL_CONNECTED;             }
    inline bool   mqtt_connected() { return mqtt_client.connected();                   }

    inline void        mqtt_loop() { mqtt_client.loop();                               }

    inline bool     show_voltage() { return (feature_mask & VOLTAGE_CHECK) != 0;       }
    inline bool   use_deep_sleep() { return (feature_mask & DEEP_SLEEP)    != 0;       }
    inline bool watchdog_enabled() { return (feature_mask & WATCHDOG_24H ) != 0;       }

    inline bool is_short_reboot_time_needed() {
      return (mem.state & (PROCESS_EVENT|WAIT_END_EVENT|END_EVENT|HOURS_24)) != 0;
    }

    inline UserResult call_user_process(Process * _process) {
      if (_process == NULL) {
        return COMPLETED;
      }
      else {
        DEBUGLN("Calling user process...");
        return (*_process)(mem.state);
      }
    }

    State check_if_24_hours_time(State _default_state);
    bool retrieve_config(JsonObject & _root, Config & _config);
    bool load_config(int _version = 0);

    bool init_callbacks();

    bool     save_config();
    void send_config_msg();
    void  send_state_msg(const char * _msg_type);
    void  get_new_config();

    #if MAISON_TESTING
      void show_config(Config & _config);
    #endif

    char * mac_to_str(uint8_t * _mac, char * _buff);
    bool update_device_name();

    bool     load_mems();
    bool     save_mems();
    bool      init_mem();
    bool init_user_mem();
    bool      read_mem(uint32_t * _data, uint16_t _length, uint16_t _addr);
    bool     write_mem(uint32_t * _data, uint16_t _length, uint16_t _addr);

    char * ip2str(uint32_t, char *_str, int _length);
    char * mac2str(byte _mac[], char *_str, int _length);
    bool str2ip(const char * _str, uint32_t * _ip);

    void reboot();
};

#endif
