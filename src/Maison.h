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
  #define MAISON_PREFIX_TOPIC "maison/"
#endif

// This is the topic name to send status information to
//
// For example: maison/status

#ifndef MAISON_STATUS_TOPIC
  #define MAISON_STATUS_TOPIC MAISON_PREFIX_TOPIC "status"
#endif

// This is the topic name to send control information to
//
// For example: maison/ctrl

#ifndef MAISON_CTRL_TOPIC
  #define MAISON_CTRL_TOPIC   MAISON_PREFIX_TOPIC "ctrl"
#endif

// This is the topic name suffix where the device wait for control commands from site
// The completed topic is build using:
//
// 1) MAISON_PREFIX_TOPIC
// 2) The device name from config or MAC address if device name is empty
// 3) MAISON_DEVICE_CTRL_SUFFIX_TOPIC
//
// For example: maison/DEV_TEST/crtl

#ifndef MAISON_DEVICE_CTRL_SUFFIX_TOPIC
  #define MAISON_DEVICE_CTRL_SUFFIX_TOPIC   "/ctrl"
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

// Syntaxic sugar

#define DO        bool result = false; while (true)
#define OK_DO     { result = true; break; }
#define ERROR(m)  { DEBUGLN(F(" ERROR: " m)); break; }

// To get WATCHDOG Time faster during tests

#if QUICK_TURN
  #define ONE_HOUR 5   // In seconds. So HOURS_24 fired every 2 minutes.
#else
  #define ONE_HOUR 3600 // In seconds. Normal is one hour x 24 = 24 hours.
#endif

class Maison
{
  public:

    // Features in feature_mask
    //
    // Note: if VOLTAGE_CHECK is requested and the internal battery readout is targeted, the main
    //       user sketch must add the following line at the beginning of the sketch source code file:
    //
    //    ADC_MODE(ADC_VCC);

    enum Feature : uint8_t {
      NONE          = 0x00, // No special feature
      VOLTAGE_CHECK = 0x01, // Chip A2D voltage readout will be sent on status/watchdog messages
      BATTERY_POWER = 0x02, // Using batteries -> deep_sleep will be used then
      WATCHDOG_24H  = 0x04  // Watchdog status sent every 24 hours
    };

    // States of the finite state machine
    //
    // On battery power, only the following states will have networking capability:
    //
    //    STARTUP, PROCESS_EVENT, END_EVENT, HOURS_24

    enum State : uint8_t {
      STARTUP        =  1,
      WAIT_FOR_EVENT =  2,
      PROCESS_EVENT  =  4,
      WAIT_END_EVENT =  8,
      END_EVENT      = 16,
      HOURS_24       = 32
    };

    // The ProcessResult is returned by the user process function to indicate
    // how to proceed with the finite state machine transformation:
    //
    // COMPLETED: Returned when the processing for the current state is
    //            considered completed. This is used mainly for all states.
    //            
    // NOT_COMPLETED: The reverse of COMPLETED. Mainly used with PROCESS_EVENT in the
    //                case that it must be fired again to complete the processing
    //
    // ABORTED: Return in the case of PROCESS_EVENT when the event vanished before
    //          processing, such that the finite state machine return to the
    //          WAIT_FOR_EVENT state instead of going to the WAIT_END_EVENT state.
    //
    // NEW_EVENT: Returned when processing a WAIT_FOR_EVENT state to indicate that
    //            an event must be processed.

    enum UserResult : uint8_t { COMPLETED = 1, NOT_COMPLETED, ABORTED, NEW_EVENT };

    typedef UserResult Process(State state);
    typedef void Callback(const char * topic, byte * payload, unsigned int length);

    Maison();
    Maison(uint8_t _feature_mask);
    Maison(uint8_t _feature_mask, void * _user_mem, uint8_t _user_mem_length);

    bool setup();
    bool send_msg(const char * _topic, const char * format, ...);
    int  reset_reason();

    void deep_sleep(bool back_with_wifi, int sleep_time_in_sec);

    inline float battery_voltage() { return (ESP.getVcc() * (1.0 / 1024.0)); }    
    inline bool       hard_reset() { return reset_reason() != REASON_DEEP_SLEEP_AWAKE; }

    inline bool network_required() { 
      return (mem.state & (STARTUP|PROCESS_EVENT|END_EVENT|HOURS_24)) != 0;
    }

    char * my_topic(const char * topic, char * buffer, uint16_t buffer_length);
    uint32_t CRC32(const uint8_t * data, size_t length);
    bool setUserCallback(Callback * _cb, const char * _topic, uint8_t _qos = 0);
    void loop(Process * process = NULL);

  private:
    // The configuration is read at setup time from file "/config.json" in the SPIFFS flash
    // memory space. It is updated by the home management tool through mqtt topic 
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
      uint32_t one_hour_step_count; // Up to 3600 seconds in milliseconds
      uint32_t magic;
    } mem;

    static BearSSL::WiFiClientSecure wifi_client;
    static PubSubClient     mqtt_client;

    long         last_reconnect_attempt;
    Callback   * user_cb;
    const char * user_topic;
    uint8_t      user_qos;
    uint8_t      feature_mask;
    void       * user_mem;
    uint8_t      user_mem_length;

    bool wifi_connect();
    bool mqtt_connect();

    friend void maison_callback(const char * topic, byte * payload, unsigned int length);
    void process_callback(const char * topic, byte * payload, unsigned int length);

    inline bool   wifi_connected() { return WiFi.status() == WL_CONNECTED;             }
    inline bool   mqtt_connected() { return mqtt_client.connected();                   }

    inline void        mqtt_loop() { mqtt_client.loop();                               }

    inline bool     show_voltage() { return (feature_mask & VOLTAGE_CHECK) != 0;       }
    inline bool on_battery_power() { return (feature_mask & BATTERY_POWER) != 0;       }
    inline bool watchdog_enabled() { return (feature_mask & WATCHDOG_24H ) != 0;       }

    inline State check_if_24_hours_time(State default_state) {
      if (mem.one_hour_step_count >= (ONE_HOUR * 1000)) {
        mem.one_hour_step_count = 0;
        if (++mem.hours_24_count >= 24) {
          mem.hours_24_count = 0;
          return HOURS_24;
        }
      }
      return default_state;
    }

    inline bool is_short_reboot_time_needed() { 
      return (mem.state & (PROCESS_EVENT|WAIT_END_EVENT|END_EVENT)) != 0;
    }

    inline UserResult call_user_process(Process * process) {
      if (process == NULL) {
        return COMPLETED;
      }
      else {
        DEBUGLN("Calling user process...");
        return (*process)(mem.state);
      }
    }

    bool retrieve_config(JsonObject & root, Config & config);
    bool load_config(int version = 0);
    bool save_config();
    #if MAISON_TESTING
      void show_config(Config & config);
    #endif

    char * mac_to_str(uint8_t * mac, char * buff);
    bool update_device_name();
    bool     mqtt_reconnect();

    bool     load_mems();
    bool     save_mems();
    bool      init_mem();
    bool init_user_mem();
    bool      read_mem(uint32_t * data, uint16_t length, uint16_t addr);
    bool     write_mem(uint32_t * data, uint16_t length, uint16_t addr);
};

#endif