#include <Maison.h>

// Used by the maison_callback friend function
static Maison * maison;

Maison::Maison() :
               wifi_client(NULL),
    last_reconnect_attempt(0),
       connect_retry_count(0),
       first_connect_trial(true),
             user_callback(NULL),
            user_sub_topic(NULL),
                  user_qos(0),
              feature_mask(NONE),
                  user_mem(NULL),
           user_mem_length(0),
           last_time_count(0),
  counting_lost_connection(true),
   wait_for_ota_completion(false),
                reboot_now(false),
               restart_now(false)
{
  maison = this;
}

Maison::Maison(uint8_t _feature_mask) :
               wifi_client(NULL),
    last_reconnect_attempt(0),
       connect_retry_count(0),
       first_connect_trial(true),
             user_callback(NULL),
            user_sub_topic(NULL),
                  user_qos(0),
              feature_mask(_feature_mask),
                  user_mem(NULL),
           user_mem_length(0),
           last_time_count(0),
  counting_lost_connection(true),
   wait_for_ota_completion(false),
                reboot_now(false),
               restart_now(false)
{
  maison = this;
}

Maison::Maison(uint8_t _feature_mask, void * _user_mem, uint16_t _user_mem_length) :
               wifi_client(NULL),
    last_reconnect_attempt(0),
       connect_retry_count(0),
       first_connect_trial(true),
             user_callback(NULL),
            user_sub_topic(NULL),
                  user_qos(0),
              feature_mask(_feature_mask),
                  user_mem(_user_mem),
           user_mem_length(_user_mem_length),
           last_time_count(0),
  counting_lost_connection(true),
   wait_for_ota_completion(false),
                reboot_now(false),
               restart_now(false)
{
  maison = this;
}

bool Maison::setup()
{
  SHOW("\nMaison::setup()\n");

  DO {
    if (!   load_mems()) ERROR("Unable to load states");
    if (! load_config()) ERROR("Unable to load config");
    if (is_hard_reset()) init_mem();

    if (network_is_available()) {
      if (!wifi_connect()) ERROR("WiFi");
      update_device_name();
    }

    DEBUG(F("MQTT_MAX_PACKET_SIZE = ")); DEBUGLN(MQTT_MAX_PACKET_SIZE);

    OK_DO;
  }

  SHOW_RESULT("Maison::setup()");

  return result;
}

void maison_callback(const char * _topic, byte * _payload, unsigned int _length)
{
  SHOW("---------- maison_callback() ---------------");

  if (maison != NULL) maison->process_callback(_topic, _payload, _length);

  DEBUGLN(F(" End of maison_callback()"));
}

char * Maison::build_topic(const char * _topic_suffix, char * _buffer, uint16_t _length)
{
  if (_length > (strlen(MAISON_PREFIX_TOPIC) + 12 + strlen(_topic_suffix) + 3)) {
    strlcpy(_buffer, MAISON_PREFIX_TOPIC, _length);
    strlcat(_buffer, "/",                 _length);

    uint8_t mac[6];
    WiFi.macAddress(mac);

    mac_to_str(mac, &_buffer[strlen(_buffer)]);

    strlcat(_buffer, "/",           _length);
    strlcat(_buffer, _topic_suffix, _length);

    DEBUG(F("build_topic() result: ")); DEBUGLN(_buffer);
  }
  else {
    DEBUGLN(F("ERROR: build_topic(): Buffer too small!"));
    _buffer[0] = 0;
  }
  return _buffer;
}

void Maison::send_config_msg()
{
  File file = SPIFFS.open("/config.json", "r");
  if (!file) {
    DEBUGLN(" ERROR: Unable to open current config file");
  }
  else {
    char buff[50];
    mqtt_client.beginPublish(build_topic(MAISON_CONFIG_TOPIC, buff, sizeof(buff)),
                             file.size() + strlen(config.device_name) + 44,
                             false);
    mqtt_client.write((uint8_t *) "{\"device\":\"", 11);
    mqtt_client.write((uint8_t *) config.device_name, strlen(config.device_name));
    mqtt_client.write((uint8_t *) "\",\"msg_type\":\"CONFIG\",\"content\":", 32);
    file.read((uint8_t *) buffer, file.size());
    mqtt_client.write((uint8_t *) buffer, file.size());
    mqtt_client.write((uint8_t *) "}", 1);
    mqtt_client.endPublish();

    file.close();
  }
}

void Maison::send_state_msg(const char * _msg_type)
{
  static char vbat[15];
  static char   ip[20];
  static char  mac[20];
  byte ma[6];

  ip2str(WiFi.localIP(), ip, sizeof(ip));
  WiFi.macAddress(ma);
  mac2str(ma, mac, sizeof(mac));

  if (show_voltage()) {
    snprintf(vbat, 14, ",\"VBAT\":%4.2f", battery_voltage());
  }
  else {
    vbat[0] = 0;
  }

  send_msg(
    MAISON_STATE_TOPIC,
    F("{"
       "\"device\":\"%s\""
      ",\"msg_type\":\"%s\""
      ",\"ip\":\"%s\""
      ",\"mac\":\"%s\""
      ",\"reason\":%d"
      ",\"state\":%u"
      ",\"return_state\":%u"
      ",\"hours\":%u"
      ",\"millis\":%u"
      ",\"lost\":%u"
      ",\"rssi\":%ld"
      ",\"heap\":%u"
      ",\"app_name\":\"" APP_NAME "\""
      ",\"app_version\":\"" APP_VERSION "\""
      "%s"
    "}"),
    config.device_name,
    _msg_type,
    ip,
    mac,
    reset_reason(),
    mem.state,
    mem.return_state,
    mem.hours_24_count,
    mem.one_hour_step_count,
    mem.lost_count,
    wifi_connected() ? WiFi.RSSI() : 0,
    ESP.getFreeHeap(),
    vbat);
}

void Maison::get_new_config()
{
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, &buffer[7]);

  if (error) {
    JSON_DEBUGLN(F(" ERROR: Unable to parse JSON content"));
  }
  else {
    Config cfg;

    if (!retrieve_config(doc.as<JsonObject>(), cfg))
    {
      JSON_DEBUGLN(F(" ERROR: Unable to retrieve config from received message"));
    }
    else {
      if (cfg.version > config.version) {
        mqtt_client.unsubscribe(topic);
        if (user_sub_topic) mqtt_client.unsubscribe(user_topic);
        config = cfg;
        #if JSON_TESTING
          show_config(config);
        #endif
        save_config();
        init_callbacks();
      }
      else {
        JSON_DEBUGLN(F(" ERROR: New config with a wrong version number. Not saved."));
        log(F("Error: Received New Config with wrong version number."));
      }
      send_config_msg();
    }
  }
}

#if MQTT_OTA

  #include <StreamString.h>
  class OTAConsumer : public Stream
  {
  private:
    size_t length;
    int count;
    bool running;
    bool completed;
    StreamString error; 

  public:
    bool begin(size_t _size, const char * _md5 = NULL) {
      length    = _size;
      completed = false;
      count = 0;
      running   = Update.begin(_size);
      if (!running) showError(F("cons.begin()"));
      else if (_md5) Update.setMD5(_md5);
      return running;
    }

    size_t write(uint8_t b) {
      if (running) {
        if (--length < 0) running = false;
        else {
          return Update.write(&b, 1);
        }
      }
      if (++count > 10000) {
        count = 0;
        yield();
      }
      return 0;
    }

    bool end() {
      running = false;
      completed = Update.end();
      if (!completed) showError(F("cons.end()"));
      return completed;
    }

    OTAConsumer()      { running = false;          }

    int    available() { return 0;                 } // not used
    int         read() { return 0;                 } // not used
    int         peek() { return 0;                 } // not used

    bool isCompleted() { return completed;         }
    bool   isRunning() { return running;           }
    int     getError() { return Update.getError(); }
    
    StreamString & getErrorStr() {
      error.flush(); 
      Update.printError(error);
      error.trim();
      return error; 
    }

    void   showError(const __FlashStringHelper * prefix) {  
      OTA_DEBUG(prefix);
      OTA_DEBUG(F(" : "));
      OTA_DEBUGLN(getErrorStr()); 
    }
  } cons;

#endif

void Maison::process_callback(const char * _topic, byte * _payload, unsigned int _length)
{
  NET_SHOW("process_callback()");

  some_message_received = true;

  if (strcmp(_topic, build_topic(MAISON_CTRL_TOPIC, buffer, sizeof(buffer))) == 0) {
    int len;

    memcpy(buffer, _payload, len = (_length >= sizeof(buffer)) ? (sizeof(buffer) - 1) : _length);
    buffer[len] = 0;

    if (!cons.isRunning()) {
      NET_DEBUG(F(" Received MQTT Message: "));
      NET_DEBUGLN(buffer);
    }

    #if MQTT_OTA
      if (strncmp(buffer, "NEW_CODE:{", 10) == 0) {
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, &buffer[9]);

        if (error) {
          OTA_DEBUGLN(F("Error: JSON content is in a wrong format"));
          log(F("Error: JSON content is in a wrong format"));
        }
        else {
          long         size = doc["SIZE"].as<long>();
          const char * name = doc["APP_NAME"].as<const char *>();
          const char * md5  = doc["MD5"].as<const char *>();

          if (size && name && md5) {
            OTA_DEBUG(F(" Receive size: "));
            OTA_DEBUGLN(size);

            char tmp[33];

            if (strcmp(APP_NAME, name) == 0) {
              if (cons.begin(size, md5)) {
                mqtt_client.setStream(cons);
                // log uses buffer too...
                memcpy(tmp, md5, 32);
                tmp[32] = 0;
                OTA_DEBUG(F("Code update started with size "));
                OTA_DEBUG(size); 
                OTA_DEBUG(F(" and ")); 
                OTA_DEBUGLN(tmp);
                log(F("Code update started with size %d and md5: %s."), size, tmp);
                wait_for_ota_completion = true;
              }
              else {
                OTA_DEBUG(F("Error: Code upload not started: "));
                OTA_DEBUGLN(cons.getErrorStr().c_str());
                log(F("Error: Code upload not started: %s"),
                    cons.getErrorStr().c_str());
              }
            }
            else {
              // log uses buffer too...
              strlcpy(tmp, name, sizeof(tmp));
              OTA_DEBUG(F("Error: Code upload aborted. App name differ ("));
              OTA_DEBUG(APP_NAME);
              OTA_DEBUG(F(" vs "));
              OTA_DEBUG(tmp);
              OTA_DEBUGLN(F(")"));
              log(F("Error: Code upload aborted. App name differ (%s vs %s)"), APP_NAME, tmp);
            }
          }
          else {
            OTA_DEBUGLN(F("Error: SIZE, MD5 or APP_NAME not present"));
            log(F("Error: SIZE, MD5 or APP_NAME not present"));
          }
        }
      }
      else if (cons.isRunning()) {
        // The transmission is expected to be complete. Check if the 
        // Updater is satisfied and if so, restart the device

        yield();
        
        if (cons.end()) {
          OTA_DEBUGLN(F(" Upload Completed. Rebooting..."));
          log(F("Code upload completed. Rebooting"));
          reboot_now = true;
        }
        else {
          OTA_DEBUG(F("Error: Code upload not completed: "));
          OTA_DEBUGLN(cons.getErrorStr().c_str());
          log(F("Error: Code upload not completed: %s"), 
              cons.getErrorStr().c_str());
        }
        wait_for_ota_completion = false;
      }
      else 
    #endif

    if (strncmp(buffer, "CONFIG:", 7) == 0) {
      NET_DEBUGLN(F(" New config received"));
      get_new_config();
    }
    else if (strncmp(buffer, "CONFIG?", 7) == 0) {
      NET_DEBUGLN(F(" Config content requested"));
      send_config_msg();
    }
    else if (strncmp(buffer, "STATE?", 6) == 0) {
      NET_DEBUGLN(F(" Config content requested"));
      send_state_msg("STATE");
    }
    else if (strncmp(buffer, "RESTART!!", 9) == 0) {
      NET_DEBUGLN("Device is restarting");
      restart_now = true;
    }
    else if (strncmp(buffer, "REBOOT!", 7) == 0) {
      NET_DEBUGLN("Device is rebooting");
      reboot_now = true;
    }
    #if NET_TESTING
      else if (strncmp(buffer, "TEST!", 5) == 0) {
        log(F("This is a test..."));

        bool res = wifi_client->flush(10);
        NET_DEBUG(F("Result: "));
        NET_DEBUGLN(res);
      }
    #endif
    else {
      NET_DEBUGLN(F(" Warning: Unknown message received."));
      log(F("Warning: Unknown message received."));
    }
  }
  else if (user_callback != NULL) {
    DEBUGLN(F(" Calling user callback"));
    (*user_callback)(_topic, _payload, _length);
  }
}

void Maison::set_msg_callback(Callback * _cb, const char * _sub_topic, uint8_t _qos)
{
  user_callback  = _cb;
  user_sub_topic = _sub_topic;
  user_qos       = _qos;
}

void Maison::loop(Process * _process)
{
  State new_state, new_return_state;

  DEBUG(F("Maison::loop(): Current state: "));
  DEBUGLN(mem.state);

  yield();

  if (network_is_available()) {

    if (first_connect_trial) {
      first_connect_trial    = false;
      NET_DEBUGLN(F("First Connection Trial"));
      mqtt_connect();
      last_reconnect_attempt = millis();
    }

    if (!mqtt_connected()) {

      // We have not been able to connect to the MQTT server.
      // Wait for an hour before trying again. In a deep sleep enabled
      // situation, this will minimize battery drain.

      if (counting_lost_connection) {
        // This will count lost connection only once between successfull connexion.
        mem.lost_count += 1;
        counting_lost_connection = false;
        NET_DEBUG(F(" Connection Lost Count: "));
        NET_DEBUGLN(mem.lost_count);
      }

      if (use_deep_sleep()) {
        NET_DEBUGLN(F("Unable to connect to MQTT Server. Deep Sleep for 1 hour."));
        deep_sleep(true, ONE_HOUR);
      }
      else {
        long now = millis();
        if ((now - last_reconnect_attempt) > (1000L * ONE_HOUR)) {
          NET_DEBUG(F("\r\nBeen waiting for "));
          NET_DEBUG(ONE_HOUR);
          NET_DEBUGLN(F(" Seconds. Trying again..."));
          if (!mqtt_connect()) {
            last_reconnect_attempt = millis();
            return;
          }
        }
        else {
          NET_DEBUG("-");
          return;
        }
      }
      #if NET_TESTING
        if (mqtt_connected()) NET_DEBUGLN(F("MQTT Connected."));
      #endif
    }

    counting_lost_connection = true;

    // Consume all pending messages. For OTA updates, as the request
    // is composed of 2 messages, 
    // it may require many calls to mqtt_loop to get it completed. The
    // wait_for_ota_completion flag is set by the callback to signify the need
    // to wait until the new code has been received. The algorithm
    // below insure that if the code has not been received inside 2 minutes
    // of wait time, it will be aborted. This is to control battery drain.

    uint32_t start = millis();
    NET_DEBUGLN(F("Check for new coming messages..."));
    do {
      some_message_received = false;

      // As with deep_sleep is enable, we must wait if there is
      // messages to be retrieved. 
      int count = use_deep_sleep() ? 2000 : 1;
      
      for (int i = 0; i < count; i++) {
        yield();
        mqtt_loop();
        if (some_message_received) {
          NET_DEBUG(F("Message received after "));
          NET_DEBUG(i);
          NET_DEBUGLN(F(" loops."));
          break;
        }
      }
    } while (some_message_received || 
             (wait_for_ota_completion && ((millis() - start) < 120000)));

    if (wait_for_ota_completion) {
      wait_for_ota_completion = false;
      OTA_DEBUGLN(F("Error: Wait for completion too long. Aborted."));
      log(F("Error: Wait for completion too long. Aborted."));
    }

    if (restart_now) restart();
    if (reboot_now) reboot();
  }

  new_state        = mem.state;
  new_return_state = mem.return_state;

  set_deep_sleep_wait_time(
    is_short_reboot_time_needed() ? DEFAULT_SHORT_REBOOT_TIME : ONE_HOUR);

  if (use_deep_sleep()) {
    mem.elapse_time += micros();
  }
  else {
    mem.elapse_time  = micros() - loop_time_marker;
  }
  loop_time_marker = micros();

  UserResult res = call_user_process(_process);

  DEBUG(F("User process result: ")); DEBUGLN(res);

  switch (mem.state) {
    case STARTUP:
      send_state_msg("STARTUP");
      if (res != NOT_COMPLETED) {
        new_state        = WAIT_FOR_EVENT;
        new_return_state = WAIT_FOR_EVENT;
      }
      break;

    case WAIT_FOR_EVENT:
      if (res == NEW_EVENT) {
        new_state        = PROCESS_EVENT;
        new_return_state = PROCESS_EVENT;
      }
      else {
        new_return_state = WAIT_FOR_EVENT;
        new_state        = check_if_24_hours_time(WAIT_FOR_EVENT);
      }
      break;

    case PROCESS_EVENT:
      if (res == ABORTED) {
        new_state = new_return_state = WAIT_FOR_EVENT;
      }
      else if (res != NOT_COMPLETED) {
        new_state = new_return_state = WAIT_END_EVENT;
      }
      else {
        new_return_state = PROCESS_EVENT;
        new_state        = check_if_24_hours_time(PROCESS_EVENT);
      }
      break;

    case WAIT_END_EVENT:
      if (res == RETRY) {
        new_state = new_return_state = PROCESS_EVENT;
      }
      else if (res != NOT_COMPLETED) {
        new_state = new_return_state = END_EVENT;
      }
      else {
        new_return_state = WAIT_END_EVENT;
        new_state        = check_if_24_hours_time(WAIT_END_EVENT);
      }
      break;

    case END_EVENT:
      if (res != NOT_COMPLETED) {
        new_state = new_return_state = WAIT_FOR_EVENT;
      }
      break;

    case HOURS_24:
      delay(100);
      if (mqtt_connected()) mqtt_loop(); // Second chance to process received msgs
      if (watchdog_enabled()) {
        send_state_msg("WATCHDOG");
      }

      new_state = new_return_state;
      break;
  }

  mem.state        = new_state;
  mem.return_state = new_return_state;

  DEBUG(" Next state: "); DEBUGLN(mem.state);

  if (use_deep_sleep()) {
    deep_sleep(network_is_available(), deep_sleep_wait_time);
  }
  else {
    mem.one_hour_step_count += millis() - last_time_count;
    last_time_count = millis();
  }

  DEBUG(F(" One hour step count ("));
  DEBUG(mem.hours_24_count);
  DEBUG("): ");
  DEBUGLN(mem.one_hour_step_count);

  DEBUGLN("End of Maison::loop()");
}

#define GETS(dst, src, size)                 \
  if ((tmp = src)) {                         \
    strlcpy(dst, tmp, size);                 \
  }                                          \
  else {                                     \
    JSON_DEBUG(F(" ERROR: Unable to get ")); \
    JSON_DEBUGLN(STRINGIZE(src));            \
    break;                                   \
  }

#define GETI(dst, src)                       \
  if (src.as<int>()) {                       \
    dst = src.as<int>();                     \
  }                                          \
  else {                                     \
    JSON_DEBUG(F(" ERROR: Unable to get ")); \
    JSON_DEBUGLN(STRINGIZE(src));            \
    break;                                   \
  }

#define GETA(dst, src, size) copyArray(src, dst)

#define GETIP(dst, src) \
  if (!str2ip(src, &dst)) \
    JSON_ERROR(" Bad IP Address or Mask format for " STRINGIZE(dst))

bool Maison::retrieve_config(JsonObject _doc, Config & _config)
{
  JSON_SHOW("retrieve_config()");

  DO {
    const char * tmp;

    GETI (_config.version,          _doc["version"         ]);
    GETS (_config.device_name,      _doc["device_name"     ], sizeof(_config.device_name     ));
    GETS (_config.wifi_ssid,        _doc["ssid"            ], sizeof(_config.wifi_ssid       ));
    GETS (_config.wifi_password,    _doc["wifi_password"   ], sizeof(_config.wifi_password   ));
    GETS (_config.mqtt_server,      _doc["mqtt_server_name"], sizeof(_config.mqtt_server     ));
    GETS (_config.mqtt_username,    _doc["mqtt_user_name"  ], sizeof(_config.mqtt_username   ));
    GETS (_config.mqtt_password,    _doc["mqtt_password"   ], sizeof(_config.mqtt_password   ));
    GETI (_config.mqtt_port,        _doc["mqtt_port"       ]);
    GETA (_config.mqtt_fingerprint, _doc["mqtt_fingerprint"], 20);
    GETIP(_config.ip,               _doc["ip"              ]);
    GETIP(_config.subnet_mask,      _doc["subnet_mask"     ]);
    GETIP(_config.gateway,          _doc["gateway"         ]);
    GETIP(_config.dns,              _doc["dns"             ]);

    OK_DO;
  }

  JSON_SHOW_RESULT("retrieve_config()");

  return result;
}

bool Maison::load_config(int _file_version)
{
  File file;
  char the_filename[32];
  char str[20];

  JSON_SHOW("load_config()");

  if (_file_version == 0) {
    strlcpy(the_filename, "/config.json", sizeof(the_filename));
  }
  else {
    strlcpy(the_filename, "/config_",                   sizeof(the_filename));
    strlcat(the_filename, itoa(_file_version, str, 10), sizeof(the_filename));
    strlcat(the_filename, ".json",                      sizeof(the_filename));
  }

  JSON_DEBUG(F(" Config filename: "));
  JSON_DEBUGLN(the_filename);

  DO {
    if (!SPIFFS.begin())              JSON_ERROR("SPIFFS.begin() not working");
    if (!SPIFFS.exists(the_filename)) JSON_ERROR("Config file does not esists");

    file = SPIFFS.open(the_filename, "r");
    if (!file) JSON_ERROR("Unable to open file");

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, file);

    if (error) JSON_ERROR("Unable to parse JSON content");

    if (!retrieve_config(doc.as<JsonObject>(), config)) {
      JSON_ERROR("Unable to read config elements");
    }

    OK_DO;
  }

  file.close();

  #if JSON_TESTING
    if (result) show_config(config);
  #endif

  JSON_SHOW_RESULT("load_config()");

  return result;
}

#define PUT(src, dst) dst = src
#define PUTA(src, dst, len) copyArray(src, dst)
#define PUTIP(src, dst) ip2str(src, buffer, 50); dst = buffer;

bool Maison::save_config()
{
  File file;

  JSON_SHOW("save_config()");

  DO {
    if (!SPIFFS.begin()) ERROR(" SPIFFS.begin() not working");
    if (SPIFFS.exists("/config_5.json")) SPIFFS.remove("/config_5.json");
    if (SPIFFS.exists("/config_4.json")) SPIFFS.rename("/config_4.json", "/config_5.json");
    if (SPIFFS.exists("/config_3.json")) SPIFFS.rename("/config_3.json", "/config_4.json");
    if (SPIFFS.exists("/config_2.json")) SPIFFS.rename("/config_2.json", "/config_3.json");
    if (SPIFFS.exists("/config_1.json")) SPIFFS.rename("/config_1.json", "/config_2.json");
    if (SPIFFS.exists("/config.json"  )) SPIFFS.rename("/config.json",   "/config_1.json");

    file = SPIFFS.open("/config.json", "w");

    if (!file) JSON_ERROR("Unable to open file /config.json");

    DynamicJsonDocument doc(2048);

    JsonArray arr = doc.createNestedArray("mqtt_fingerprint");
    //if (!arr.success()) ERROR("Unable to create JSON array object");

    PUT  (config.version,          doc["version"         ]);
    PUT  (config.device_name,      doc["device_name"     ]);
    PUT  (config.wifi_ssid,        doc["ssid"            ]);
    PUT  (config.wifi_password,    doc["wifi_password"   ]);
    PUTIP(config.ip,               doc["ip"              ]);
    PUTIP(config.subnet_mask,      doc["subnet_mask"     ]);
    PUTIP(config.gateway,          doc["gateway"         ]);
    PUTIP(config.dns,              doc["dns"             ]);
    PUT  (config.mqtt_server,      doc["mqtt_server_name"]);
    PUT  (config.mqtt_username,    doc["mqtt_user_name"  ]);
    PUT  (config.mqtt_password,    doc["mqtt_password"   ]);
    PUT  (config.mqtt_port,        doc["mqtt_port"       ]);
    PUTA (config.mqtt_fingerprint, arr, 20);

    serializeJson(doc, file);

    OK_DO;
  }

  file.close();

  JSON_SHOW_RESULT("save_config()");

  return result;
}

char * Maison::mac_to_str(uint8_t * _mac, char * _buff)
{
  const char * hex = "0123456789ABCDEF";
  char * ptr = _buff;
  for (int i = 0; i < 6; i++) {
    *ptr++ = hex[_mac[i] >> 4];
    *ptr++ = hex[_mac[i] & 0x0F];
  }
  *ptr = 0;

  return _buff;
}

bool Maison::update_device_name()
{
  if (config.device_name[0] == 0) {
    uint8_t mac[6];
    char str[20];
    WiFi.macAddress(mac);
    strlcpy(config.device_name, mac_to_str(mac, str), sizeof(config.device_name));
    return true;
  }
  return false;
}

int Maison::reset_reason()
{
  rst_info * reset_info = ESP.getResetInfoPtr();

  DEBUG(F("Reset reason: ")); DEBUGLN(reset_info->reason);

  return reset_info->reason;
}

bool Maison::wifi_connect()
{
  NET_SHOW("wifi_connect()");

  DO {
    if (!wifi_connected()) {
      delay(200);
      WiFi.mode(WIFI_STA);
      if (config.ip != 0) {
        WiFi.config(config.ip,
                    config.dns,
                    config.gateway,
                    config.subnet_mask);
      }
      WiFi.begin(config.wifi_ssid, config.wifi_password);
      delay(100);

      int attempt = 0;
      while (!wifi_connected()) {
        delay(200);
        NET_DEBUG(F("."));
        if (++attempt >= 50) { // 10 seconds
          NET_ERROR("Unable to connect to WiFi");
        }
      }
    }

    break;
  }

  result = wifi_connected();
  
  NET_SHOW_RESULT("wifi_connect()");

  return result;
}

bool Maison::init_callbacks()
{
  NET_SHOW("init_callbacks()");

  DO {
    if (!mqtt_client.subscribe(
                 build_topic(MAISON_CTRL_TOPIC, topic, sizeof(topic)),
                 1)) {
      NET_DEBUG(F(" Hum... unable to subscribe to topic (State:"));
      NET_DEBUG(mqtt_client.state());
      NET_DEBUG(F("): "));
      NET_DEBUGLN(topic);
      break;
    }
    else {
      NET_DEBUG(F(" Subscription completed to topic "));
      NET_DEBUGLN(topic);
    }

    if (user_sub_topic != NULL) {
      if (!mqtt_client.subscribe(user_topic, user_qos)) {
        NET_DEBUG(F(" Hum... unable to subscribe to user topic (State:"));
        NET_DEBUG(mqtt_client.state());
        NET_DEBUG(F("): "));
        NET_DEBUGLN(user_topic);
        break;
      }
      else {
        NET_DEBUG(F(" Subscription completed to user topic "));
        NET_DEBUGLN(user_topic);
      }
    }
    
    OK_DO;
  }

  NET_SHOW_RESULT("init_callbacks()");

  return result;
}

static char tmp_buff[50]; // Shared by mqtt_connect(), send_msg() and log()

bool Maison::mqtt_connect()
{
  NET_SHOW("mqtt_connect()");

  DO {
    if (!wifi_connect()) NET_ERROR("WiFi");

    if (!mqtt_connected()) {

      if (wifi_client != NULL) {
        delete wifi_client;
        wifi_client = NULL;
      }

      #if MAISON_SECURE
        wifi_client = new BearSSL::WiFiClientSecure;
        if (config.mqtt_fingerprint[0]) {
          wifi_client->setFingerprint(config.mqtt_fingerprint);
        }
        else {
          wifi_client->setInsecure();
        }
      #else
        wifi_client = new WiFiClient;
      #endif

      mqtt_client.setClient(*wifi_client);
      mqtt_client.setServer(config.mqtt_server, config.mqtt_port);
      mqtt_client.setCallback(maison_callback);

      if (user_sub_topic != NULL) {
        build_topic(user_sub_topic, user_topic, sizeof(user_topic));
      }

      strlcpy(tmp_buff, "client-",          sizeof(tmp_buff));
      strlcat(tmp_buff, config.device_name, sizeof(tmp_buff));

      NET_DEBUG(F(" Client name: ")); NET_DEBUGLN(tmp_buff            );
      NET_DEBUG(F(" Username: "   )); NET_DEBUGLN(config.mqtt_username);
      NET_DEBUG(F(" Clean session: ")); NET_DEBUGLN(use_deep_sleep() ? F("No") : F("Yes"));

      mqtt_client.connect(tmp_buff,
                          config.mqtt_username,
                          config.mqtt_password,
                          NULL, 0, 0, NULL,    // Will message not used
                          !use_deep_sleep());  // Permanent session if deep sleep

      if (mqtt_connected()) {
        if (!init_callbacks()) break;
      }
      else {
        NET_DEBUG(F(" Unable to connect to mqtt. State: "));
        NET_DEBUGLN(mqtt_client.state());
        #if MAISON_SECURE
          NET_DEBUG(F(" Last SSL Error: "));
          NET_DEBUGLN(wifi_client->getLastSSLError());
        #endif

        if (++connect_retry_count >= 5) {
          NET_DEBUGLN(F(" Too many trials, reconnecting WiFi..."));
          mqtt_client.disconnect();
          wifi_client->stop();
          WiFi.disconnect();
          connect_retry_count = 0;
        }
        break;
      }
    }

    connect_retry_count = 0;
    OK_DO;
  }

  NET_SHOW_RESULT("mqtt_connect()");

  return result;
}

bool Maison::send_msg(const char * _topic_suffix, const __FlashStringHelper * _format, ...)
{
  NET_SHOW("send_msg()");

  va_list args;
  va_start (args, _format);

  vsnprintf_P(buffer, MQTT_MAX_PACKET_SIZE, (const char *) _format, args);

  DO {
    NET_DEBUG(F(" Sending msg to "));
    NET_DEBUG(build_topic(_topic_suffix, tmp_buff, sizeof(tmp_buff)));
    NET_DEBUG(F(": "));
    NET_DEBUGLN(buffer);

    if (!mqtt_connected()) {
      NET_ERROR("Unable to connect to mqtt server");
    }
    else if (!mqtt_client.publish(build_topic(_topic_suffix, 
                                              tmp_buff, 
                                              sizeof(tmp_buff)), 
                                  buffer)) {
      NET_ERROR("Unable to publish message");
    }

    OK_DO;
  }

  NET_SHOW_RESULT("send_msg()");

  return result;
}

bool Maison::log(const __FlashStringHelper * _format, ...)
{
  NET_SHOW("log()");

  va_list args;
  va_start (args, _format);

  strlcpy(buffer, config.device_name, 50);
  strlcat(buffer, ": ",               50);

  int len = strlen(buffer);

  vsnprintf_P(&buffer[len], MQTT_MAX_PACKET_SIZE-len, (const char *) _format, args);

  DO {
    NET_DEBUG(F(" Log msg : "));
    NET_DEBUGLN(buffer);

    if (!mqtt_connected()) {
      NET_ERROR("Unable to connect to mqtt server");
    }
    else if (!mqtt_client.publish(build_topic(MAISON_LOG_TOPIC, 
                                              tmp_buff, 
                                              sizeof(tmp_buff)), 
                                  buffer)) {
      NET_ERROR("Unable to log message");
    }

    OK_DO;
  }

  NET_SHOW_RESULT("log()");

  return result;
}

void Maison::deep_sleep(bool _back_with_wifi, uint16_t _sleep_time_in_sec)
{
  SHOW("deep_sleep()");

  DEBUG(" Sleep Duration: ");
  DEBUGLN(_sleep_time_in_sec);

  DEBUG(" Network enabled on return: ");
  DEBUGLN(_back_with_wifi ? F("YES") : F("NO"));

  wifi_flush();

  uint32_t sleep_time = 1000000U * _sleep_time_in_sec;

  // When _sleep_time_in_sec is 0, will sleep only for 100ms
  if (sleep_time == 0) {
    sleep_time = 100000U;
    mem.one_hour_step_count += millis() + 100;
  }
  else {
    mem.one_hour_step_count += millis() + (1000U * _sleep_time_in_sec);
  }

  mem.elapse_time = micros() - loop_time_marker + sleep_time;

  save_mems();

  ESP.deepSleep(
    sleep_time,
    _back_with_wifi ? WAKE_RF_DEFAULT : WAKE_RF_DISABLED);

  delay(1000);
  DEBUGLN(" HUM... Not suppose to come here after deep_sleep call...");
}

Maison::State Maison::check_if_24_hours_time(Maison::State _default_state)
{
  DEBUG("24 hours wait time check: ");
  DEBUG(mem.hours_24_count);
  DEBUG(", ");
  DEBUGLN(mem.one_hour_step_count);

  if (mem.one_hour_step_count >= (1000U * ONE_HOUR)) {
    mem.one_hour_step_count = 0;
    if (++mem.hours_24_count >= 24) {
      mem.hours_24_count = 0;
      DEBUGLN(F("HOURS_24 reached..."));
      return HOURS_24;
    }
  }
  return _default_state;
}

// ---- RTC Memory Data Management ----

#define RTC_MAGIC     0x55aaaa55
#define RTC_BASE_ADDR         66

bool Maison::load_mems()
{
  SHOW("load_mems()");

  DO {
    if ((!read_mem((uint32_t *) &mem, sizeof(mem), 0)) || (mem.magic != RTC_MAGIC)) {
      DEBUGLN(F(" Maison state initialization"));
      if (!init_mem()) ERROR("Unable to initialize Maison state in rtc memory");
    }

    if (user_mem != NULL) {
      if (!read_mem((uint32_t *) user_mem, user_mem_length, sizeof(mem))) {
        DEBUGLN(F(" User state initialization"));
        if (!init_user_mem()) ERROR("Unable to initialize user state in rtc memory");
      }
    }

    OK_DO;
  }

  SHOW_RESULT("load_mems()");

  return result;
}

bool Maison::save_mems()
{
  SHOW("save_mems()");

  DO {
    if (!write_mem((uint32_t *) &mem, sizeof(mem), 0)) {
      ERROR("Unable to update Maison state in rtc memory");
    }

    if (user_mem != NULL) {
      if (!write_mem((uint32_t *) user_mem, user_mem_length, sizeof(mem))) {
        ERROR("Unable to update user state in rtc memory");
      }
    }

    OK_DO;
  }

  SHOW_RESULT("save_mems()");

  return result;
}

bool Maison::init_mem()
{
  SHOW("init_mem()");

  mem.magic                    = RTC_MAGIC;
  mem.state = mem.return_state = STARTUP;
  mem.hours_24_count           = 0;
  mem.one_hour_step_count      = 0;
  mem.lost_count               = 0;

  DEBUG("Sizeof mem_struct: ");
  DEBUGLN(sizeof(mem_struct));

  bool result = write_mem((uint32_t *) &mem, sizeof(mem), 0);

  SHOW_RESULT("init_mem()");

  return result;
}

bool Maison::init_user_mem()
{
  if (!user_mem) return true;

  SHOW("init_user_mem()");

  memset(user_mem, 0, user_mem_length);

  bool result = write_mem((uint32_t *) user_mem, user_mem_length, sizeof(mem));

  SHOW_RESULT("init_user_mem()");

  return result;
}

bool Maison::read_mem(uint32_t * _data, uint16_t _length, uint16_t _addr)
{
  SHOW("read_mem()");

  DEBUG(F("  data addr: "));  DEBUGLN((int)_data);
  DEBUG(F("  length: "));     DEBUGLN(_length);
  DEBUG(F("  pos in rtc: ")); DEBUGLN(_addr);

  DO {
    if (!ESP.rtcUserMemoryRead((_addr + 3) >> 2, (uint32_t *) _data, _length)) {
      ERROR("Unable to read from rtc memory");
    }

    uint32_t csum = CRC32((uint8_t *)(&_data[1]), _length - 4);

    if (_data[0] != csum) ERROR("Data in RTC memory with bad checksum!");

    OK_DO;
  }

  SHOW_RESULT("read_mem()");

  return result;
}

bool Maison::write_mem(uint32_t * _data, uint16_t _length, uint16_t _addr)
{
  SHOW("write_mem()");

  DEBUG(F("  data addr: "));  DEBUGLN((int)_data);
  DEBUG(F("  length: "));     DEBUGLN(_length);
  DEBUG(F("  pos in rtc: ")); DEBUGLN(_addr);

  _data[0] = CRC32((uint8_t *)(&_data[1]), _length - 4);

  DO {
    if (!ESP.rtcUserMemoryWrite((_addr + 3) >> 2, (uint32_t *) _data, _length)) {
      ERROR("Unable to write to rtc memory");
    }

    OK_DO;
  }

  SHOW_RESULT("write_mem()");

  return result;
}

uint32_t Maison::CRC32(const uint8_t * _data, size_t _length)
{
  uint32_t crc = 0xffffffff;

  DEBUG(F("Computing CRC: data addr: "));
  DEBUG((int)_data);
  DEBUG(F(", length: "));
  DEBUGLN(_length);

  while (_length--) {
    uint8_t c = *_data++;
    for (uint32_t i = 0x80; i > 0; i >>= 1) {
      bool bit = crc & 0x80000000;
      if (c & i) {
        bit = !bit;
      }
      crc <<= 1;
      if (bit) {
        crc ^= 0x04c11db7;
      }
    }
  }

  DEBUG(F(" Computed CRC: "));
  DEBUGLN(crc);

  return crc;
}

void Maison::wifi_flush()
{
  if (mqtt_connected()) {
    mqtt_client.disconnect();
  }

  if (wifi_client != NULL) {
    while (!wifi_client->flush(100)) delay(10);
    while (!wifi_client->stop(100)) delay(10);
    while (wifi_client->connected()) delay(10);
    delay(10);
  }
}

void Maison::reboot()
{
  if (mqtt_connected()) {
    log(F("Info: Restart requested."));
  }
  wifi_flush();
  ESP.restart();
  delay(5000);
}

void Maison::restart()
{
  save_mems();
  reboot();
}

char * Maison::ip2str(uint32_t _ip, char *_str, int _length)
{
  union {
    uint32_t ip;
    byte bip[4];
  } ip;

  ip.ip = _ip;

  snprintf(_str, _length, "%d.%d.%d.%d", ip.bip[0], ip.bip[1], ip.bip[2], ip.bip[3]);
  return _str;
}

char * Maison::mac2str(byte _mac[], char *_str, int _length)
{
  char * str = _str;
  static char hex[17] = "0123456789ABCDEF";

  for (int idx = 0; idx < 6; idx++) {
    if (--_length < 0) {
      *str = 0;
      return _str;
    }
    *str++ = hex[(_mac[idx] >> 4) & 0x0F];

    if (--_length < 0) {
      *str = 0;
      return _str;
    }
    *str++ = hex[_mac[idx] & 0x0F];

    if (idx != 5) {
      if (--_length < 0) {
        *str = 0;
        return _str;
      }
      *str++ = ':';
    }
  }
  return _str;
}

bool Maison::str2ip(const char * _str, uint32_t * _ip)
{
  int idx = 0;

  union {
    uint32_t ip;
    byte bip[4];
  } ip;

  ip.ip = 0;
  *_ip = 0;

  if (*_str ==  0 ) return true;
  if (*_str == '.') return false;

  while (*_str) {
    if (*_str == '.') {
      if (*++_str == 0) return false;
      if (++  idx  > 3) return false;
    }
    else if ((*_str >= '0') && (*_str <= '9')) {
      ip.bip[idx] = (ip.bip[idx] * 10) + (*_str++ - '0');
    }
    else {
      return false;
    }
  }

  *_ip = ip.ip;
  return (*_str == 0) && (idx == 3);
}

#if JSON_TESTING

  void Maison::show_config(Config & _config)
  {
    JSON_DEBUGLN(F("\nConfiguration:\n-------------"));

    JSON_DEBUG(F("Version       : ")); JSON_DEBUGLN(_config.version         );
    JSON_DEBUG(F("Device Name   : ")); JSON_DEBUGLN(_config.device_name     );
    JSON_DEBUG(F("WiFi SSID     : ")); JSON_DEBUGLN(_config.wifi_ssid       );
    JSON_DEBUG(F("WiFi Password : ")); JSON_DEBUGLN(F("<Hidden>")           );

    JSON_DEBUG(F("IP            : ")); JSON_DEBUGLN(ip2str(config.ip,          buffer, 50));
    JSON_DEBUG(F("DNS           : ")); JSON_DEBUGLN(ip2str(config.dns,         buffer, 50));
    JSON_DEBUG(F("Gateway       : ")); JSON_DEBUGLN(ip2str(config.gateway,     buffer, 50));
    JSON_DEBUG(F("Subnet Mask   : ")); JSON_DEBUGLN(ip2str(config.subnet_mask, buffer, 50));

    JSON_DEBUG(F("MQTT Server   : ")); JSON_DEBUGLN(_config.mqtt_server     );
    JSON_DEBUG(F("MQTT Username : ")); JSON_DEBUGLN(_config.mqtt_username   );
    JSON_DEBUG(F("MQTT Password : ")); JSON_DEBUGLN(F("<Hidden>")           );
    JSON_DEBUG(F("MQTT Port     : ")); JSON_DEBUGLN(_config.mqtt_port       );

    JSON_DEBUG(F("MQTT Fingerprint : ["));
    for (int i = 0; i < 20; i++) {
      JSON_DEBUG(_config.mqtt_fingerprint[i]);
      if (i < 19) JSON_DEBUG(F(","));
    }
    JSON_DEBUGLN(F("]"));
    JSON_DEBUGLN(F("---- The End ----"));
  }

#endif

#if 0
// Load Certificates
void load_certs()
{
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }

  // Load client certificate file from SPIFFS
  File cert = SPIFFS.open("/esp.der", "r"); //replace esp.der with your uploaded file name
  if (!cert) {
    Serial.println("Failed to open cert file");
  }
  else {
    Serial.println("Success to open cert file");
  }

  delay(1000);

  // Set client certificate
  if (wifi_client.loadCertificate(cert)) {
    Serial.println("Cert loaded");
  }
  else {
    Serial.println("Cert not loaded");
  }

  // Load client private key file from SPIFFS
  File private_key = SPIFFS.open("/espkey.der", "r"); //replace espkey.der with your uploaded file name
  if (!private_key) {
    Serial.println("Failed to open private cert file");
  }
  else {
    Serial.println("Success to open private cert file");
  }

  delay(1000);

  // Set client private key
  if (wifi_client.loadPrivateKey(private_key)) {
    Serial.println("private key loaded");
  }
  else {
    Serial.println("private key not loaded");
  }

  // Load CA file from SPIFFS
  File ca = SPIFFS.open("/ca.der", "r"); //replace ca.der with your uploaded file name
  if (!ca) {
    Serial.println("Failed to open CA");
  }
  else {
    Serial.println("Success to open CA");
  }
  delay(1000);

  // Set server CA file
  if (wifi_client.loadCACert(ca)) {
    Serial.println("CA loaded");
  }
  else {
    Serial.println("CA failed");
  }
}
#endif
