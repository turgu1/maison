#ifndef _MAISON_
#define _MAISON_

#include <Arduino.h>

#include <FS.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <stdio.h>
#include <stdarg.h>

// Insure that MQTT Packet size is big enough for the needs of the framework
// This is an option of the PubSubClient library that can be set through platformio.ini

#if MQTT_MAX_PACKET_SIZE < 512
  #error "MQTT_MAX_PACKET_SIZE MUST BE AT LEAST 512 IN SIZE."
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
  #define MAISON_PREFIX_TOPIC "maison/" ///< Prefix for all Maison framework MQTT topics
#endif

// This is the topic name to send status information to
//
// For example: maison/status

#ifndef MAISON_STATUS_TOPIC
  #define MAISON_STATUS_TOPIC MAISON_PREFIX_TOPIC "status" ///< maison server status MQTT topic
#endif

// This is the topic name to send control information to
//
// For example: maison/ctrl

#ifndef MAISON_CTRL_TOPIC
  #define MAISON_CTRL_TOPIC   MAISON_PREFIX_TOPIC "ctrl" ///< maison server control MQTT topic
#endif

// This is the topic name suffix where the device wait for control commands from site
// The completed topic is build using:
//
// 1) MAISON_PREFIX_TOPIC
// 2) The device name from config or MAC address if device name is empty
// 3) CTRL_SUFFIX_TOPIC
//
// For example: maison/DEV_TEST/crtl

#ifndef CTRL_SUFFIX_TOPIC
  #define CTRL_SUFFIX_TOPIC   "ctrl" ///< Suffix for device control topic
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
  #define   STRINGIZE(a) #a
#else
  #define       DEBUG(a)
  #define     DEBUGLN(a)
  #define        SHOW(f)
  #define SHOW_RESULT(f)
#endif

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

/// Maison Framework Class

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
    enum State : uint8_t {
      STARTUP        =  1, ///< The device has just been reset
      WAIT_FOR_EVENT =  2, ///< Wait for an event to occur
      PROCESS_EVENT  =  4, ///< An event is being processed
      WAIT_END_EVENT =  8, ///< The device is waiting for the end of the event to occur
      END_EVENT      = 16, ///< The end of an event has been detected
      HOURS_24       = 32  ///< This event occurs every 24 hours
    };

    /// A UserResult is returned by the user process function to indicate
    /// how to proceed with the finite state machine transformation.
    ///
    enum UserResult : uint8_t { 
      COMPLETED = 1, ///< The processing for the current state is considered completed
      NOT_COMPLETED, ///< The current state still require some processing in calls to come
      ABORTED,       ///< The event vanished and requires no more processing (in a *PROCESS_EVENT* state)
      NEW_EVENT      ///< A new event occurred (in a *WAIT_FOR_EVENT* state)
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
    Maison(uint8_t _feature_mask, void * _user_mem, uint8_t _user_mem_length);

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
    bool send_msg(const char * _topic, const char * _format, ...);

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
    int  reset_reason();

    /// Will restart the ESP8266
    void restart();

    /// Initiates a ESP.deep_sleep() call. This function never suppose to return...
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
    char * my_topic(const char * _topic_suffix, char * _buffer, uint16_t _length);

    /// Compute a CRC-32 checksum
    ///
    /// @param[in] _data The data vector to compute the checksum on.
    /// @param[in] _length The size of the data vector.
    /// @return The computed CRC-32 checksum.
    uint32_t CRC32(const uint8_t * _data, size_t _length);

    /// Set the MQTT message callback for the user application.
    ///
    /// @param[in] _cb The Callback function address.
    /// @param[in] _topic The topic to subscribe to.
    /// @param[in] _qos The QOS for the subscription.
    void set_msg_callback(Callback * _cb, const char * _topic, uint8_t _qos = 0);

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
      char         mqtt_server[16];
      char       mqtt_username[16];
      char       mqtt_password[30];
      uint8_t mqtt_fingerprint[20];
    } config;

    struct mem_struct {
      uint32_t csum;
      State    state;
      State    sub_state;
      uint16_t hours_24_count;      // Up to 24 hours
      uint16_t lost_count;          // How many MQTT lost connections since reset
      uint32_t one_hour_step_count; // Up to 3600 seconds in milliseconds
      uint32_t magic;
    } mem;

    PubSubClient                mqtt_client;
    BearSSL::WiFiClientSecure * wifi_client;
    
    long         last_reconnect_attempt;
    int          connect_retry_count;
    bool         first_connect_trial;
    Callback   * user_cb;
    const char * user_topic;
    uint8_t      user_qos;
    uint8_t      feature_mask;
    void       * user_mem;
    uint8_t      user_mem_length;
    long         last_time_count;
    bool         counting_lost_connection;
    uint16_t     deep_sleep_wait_time;

    char         buffer[MQTT_MAX_PACKET_SIZE];

    bool wifi_connect();
    bool mqtt_connect();

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
    bool save_config();
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
};

#endif