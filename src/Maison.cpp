#include <Maison.h>

// Used by the maison_callback friend function
static Maison * maison;

Maison::Maison() :
  wifi_client(NULL),
  last_reconnect_attempt(0),
  connect_retry_count(0),
  first_connect_trial(true),
  user_cb(NULL),
  user_topic(NULL),
  user_qos(0),
  feature_mask(NONE),
  user_mem(NULL),
  user_mem_length(0),
  last_time_count(0),
  counting_lost_connection(true),
  wait_for_completion(false),
  reboot_now(false)
{
  maison = this;
}

Maison::Maison(uint8_t _feature_mask) :
  wifi_client(NULL),
  last_reconnect_attempt(0),
  connect_retry_count(0),
  first_connect_trial(true),
  user_cb(NULL),
  user_topic(NULL),
  user_qos(0),
  feature_mask(_feature_mask),
  user_mem(NULL),
  user_mem_length(0),
  last_time_count(0),
  counting_lost_connection(true),
  wait_for_completion(false),
  reboot_now(false)
{
  maison = this;
}

Maison::Maison(uint8_t _feature_mask, void * _user_mem, uint16_t _user_mem_length) :
  wifi_client(NULL),
  last_reconnect_attempt(0),
  connect_retry_count(0),
  first_connect_trial(true),
  user_cb(NULL),
  user_topic(NULL),
  user_qos(0),
  feature_mask(_feature_mask),
  user_mem(_user_mem),
  user_mem_length(_user_mem_length),
  last_time_count(0),
  counting_lost_connection(true),
  wait_for_completion(false),
  reboot_now(false)
{
  maison = this;
}

bool Maison::setup()
{
  SHOW("\nMaison::setup()\n");

  DO {
    if (!   load_mems()) ERROR("Unable to load states");
    if (! load_config()) ERROR("Unable to load config");

    if (is_hard_reset()) {
      mem.state = mem.return_state = STARTUP;
      mem.callbacks_initialized = false;
      mem.hours_24_count        = 0;
      mem.one_hour_step_count   = 0;
      mem.lost_count            = 0;
    }

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

void Maison::send_config_msg()
{
  File file = SPIFFS.open("/config.json", "r");
  if (!file) {
    DEBUGLN(" ERROR: Unable to open current config file");
  }
  else {
    mqtt_client.beginPublish(MAISON_STATUS_TOPIC,
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

void Maison::send_state_msg()
{
  static char vbat[15];
  static char   ip[20];
  static char  mac[20];
  byte ma[6];

  ip2str(WiFi.localIP(), ip, sizeof(ip));
  WiFi.macAddress(ma);
  mac2str(ma, mac, sizeof(mac));

  if (show_voltage()) {
    snprintf(vbat, 14, ",\"VBAT\":%3.1f", battery_voltage());
  }
  else {
    vbat[0] = 0;
  }

  send_msg(
    MAISON_STATUS_TOPIC,
    "{"
       "\"device\":\"%s\""
      ",\"msg_type\":\"STATE\""
      ",\"ip\":\"%s\""
      ",\"mac\":\"%s\""
      ",\"state\":%u"
      ",\"return_state\":%u"
      ",\"hours\":%u"
      ",\"millis\":%u"
      ",\"lost\":%u"
      ",\"rssi\":%ld"
      ",\"heap\":%u"
      #if MQTT_OTA
        ",\"app_name\":\"" APP_NAME "\""
        ",\"app_version\":\"" APP_VERSION "\""
      #endif
      "%s"
    "}",
    config.device_name,
    ip,
    mac,
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
  DynamicJsonBuffer jsonBuffer;
  JsonObject & root = jsonBuffer.parseObject(&buffer[7]);

  if (!root.success()) {
    DEBUGLN(F(" ERROR: Unable to parse JSON content"));
  }
  else {
    Config cfg;

    if (!retrieve_config(root, cfg)) {
      DEBUGLN(F(" ERROR: Unable to retrieve config from received message"));
    }
    else {
      if (cfg.version > config.version) {
        config = cfg;
        #if MAISON_TESTING
          show_config(config);
        #endif
        save_config();
      }
      else {
        DEBUGLN(F(" ERROR: New config with a wrong version number. Not saved."));
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
    bool running;
    bool completed;
    StreamString error; 

  public:
    bool begin(size_t _size, const char * _md5 = NULL) {
      length    = _size;
      completed = false;
      running   = Update.begin(_size);
      if (!running) showError();
      else if (_md5) Update.setMD5(_md5);
      return running;
    }

    size_t write(uint8_t b) {
      if (running) {
        if (--length < 0) running = false;
        else {
          Update.write(&b, 1);
          return 1;
        }
      }
      return 0;
    }

    bool end() {
      running = false;
      if (Update.end()) {
        completed = Update.isFinished();
        if (!completed) showError();
      } else showError();
      return completed;
    }

    OTAConsumer()   { running = false; }

    int available() { return 0; } // not used
    int      read() { return 0; } // not used
    int      peek() { return 0; } // not used

    bool isCompleted() { return completed;         }
    bool   isRunning() { return running;           }
    int     getError() { return Update.getError(); }
    
    StreamString & getErrorStr() {
      error.flush(); 
      Update.printError(error);
      error.trim();
      return error; 
    }

    void   showError() {  
      DEBUGLN(getErrorStr()); 
    }
  } cons;

#endif

void Maison::process_callback(const char * _topic, byte * _payload, unsigned int _length)
{
  SHOW("process_callback()");

  some_message_received = true;

  if (strcmp(_topic, my_topic(CTRL_SUFFIX_TOPIC, buffer, sizeof(buffer))) == 0) {
    int len;

    DEBUG(F(" Received MQTT Message: "));

    memcpy(buffer, _payload, len = (_length >= sizeof(buffer)) ? (sizeof(buffer) - 1) : _length);
    buffer[len] = 0;
    DEBUGLN(buffer);

    #if MQTT_OTA
      if (strncmp(buffer, "NEW_CODE:{", 10) == 0) {
        DynamicJsonBuffer jsonBuffer;
        JsonObject & root = jsonBuffer.parseObject(&buffer[9]);

        long size = root["SIZE"].as<long>();
        const char * name = root["APP_NAME"].as<const char *>();
        const char * md5  = root["MD5"].as<const char *>();

        if (size && name && md5) {
          DEBUG(F(" Receive size: ")); DEBUGLN(size);
          
          char tmp[33];

          if (strcmp(APP_NAME, name) == 0) {
            if (cons.begin(size, md5)) {
              mqtt_client.setStream(cons);
              // log uses buffer too...
              strncpy(tmp, md5, 32);
              log("Code update started with size %d and md5: %s.", 
                  size, tmp);
              wait_for_completion = true;
            }
            else {
              log("Error: Code upload not started: %s", 
                  cons.getErrorStr().c_str());            
            }
          }
          else {
            // log uses buffer too...
            strncpy(tmp, name, 32);
            log("Error: Code upload aborted. App name differ (%s vs %s)", 
                APP_NAME, 
                tmp);
          }
        }
        else {
          log("Error: SIZE, MD5 or APP_NAME not present");
        }
      }
      else if (cons.isRunning()) {
        // The transmission is complete. Check if the Updater is satisfied and if
        // so, restart the device
        if (cons.end() && cons.isCompleted()) {
          DEBUGLN(F(" Upload Completed. Rebooting..."));
          log("Code upload completed. Rebooting");
          reboot_now = true;
        }
        else {
          DEBUGLN(F(" ERROR: Upload not complete!"));
          log("Error: Code upload not completed: %s", 
              cons.getErrorStr().c_str());
        }
        wait_for_completion = false;
      }
      else 
    #endif

    if (strncmp(buffer, "CONFIG:", 7) == 0) {
      DEBUGLN(F(" New config received"));

      get_new_config();
    }
    else if (strncmp(buffer, "CONFIG?", 7) == 0) {
      DEBUGLN(F(" Config content requested"));

      send_config_msg();
    }
    else if (strncmp(buffer, "STATE?", 6) == 0) {
      DEBUGLN(F(" Config content requested"));

      send_state_msg();
    }
    else if (strncmp(buffer, "RESTART!", 8) == 0) {
      DEBUGLN("Device is restarting");
      restart();
    }
    else {
      log("Warning: Unknown message received.");
    }
  }
  else if (user_cb != NULL) {
    DEBUGLN(F(" Calling user callback"));
    (*user_cb)(_topic, _payload, _length);
  }
}

void Maison::set_msg_callback(Callback * _cb, const char * _topic, uint8_t _qos)
{
  user_cb    = _cb;
  user_topic = _topic;
  user_qos   = _qos;
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
      last_reconnect_attempt = millis();
      DEBUGLN(F("First Connection Trial"));
      mqtt_connect();
    }

    if (!mqtt_connected()) {

      if (counting_lost_connection) {
        mem.lost_count += 1;
        counting_lost_connection = false;
        DEBUG(F(" Connection Lost Count: "));
        DEBUGLN(mem.lost_count);
      }

      if (use_deep_sleep()) {
        DEBUGLN(F("Unable to connect to MQTT Server. Deep Sleep for 5 seconds."));
        deep_sleep(true, 5);
      }
      else {
        long now = millis();
        if ((now - last_reconnect_attempt) > 5000) {
          last_reconnect_attempt = now;
          if (!mqtt_connect()) return;
        }
        else {
          DEBUG("-");
          return;
        }
      }
    }

    counting_lost_connection = true;

    DEBUGLN(F("MQTT Connected."));

    // Consume all pending messages. For OTA updates, as the message is very long, 
    // it may require many calls to mqtt_loop to get it completed. The
    // wait_for_completion flag is set by the callback to signify the need
    // to wait until the complete new code has been received. The algorithm
    // below insure that if the code has not been received inside 2 minutes
    // of wait time, it will be aborted. This is to control battery drain.

    delay(100);
    long start = millis();
    do {
      some_message_received = false;
      yield();
      mqtt_loop();
    } while (some_message_received || 
             (wait_for_completion && ((millis() - start) < 120000)));
    if ((millis() - start) >= 120000) {
      wait_for_completion = false;
      log("Error: Wait for completion too long. Aborted.");
    }
    if (reboot_now) {
      delay(5000);
      ESP.restart();
      delay(10000);      
    }
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

  char vbat[15];

  switch (mem.state) {
    case STARTUP:
      if (show_voltage()) {
        snprintf(vbat, 14, ",\"VBAT\":%3.1f", battery_voltage());
      }
      else {
        vbat[0] = 0;
      }

      if (!send_msg(MAISON_STATUS_TOPIC,
                    "{"
                    "\"device\":\"%s\","
                    "\"msg_type\":\"%s\","
                    "\"reason\":%d"
                    "%s"
                    "}",
                    config.device_name,
                    "STARTUP",
                    reset_reason(),
                    vbat)) {
        ERROR("Unable to send startup message");
      }

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
        if (show_voltage()) {
          snprintf(vbat, 14, ",\"VBAT\":%3.1f", battery_voltage());
        }
        else {
          vbat[0] = 0;
        }

        if (!send_msg(MAISON_STATUS_TOPIC,
                      "{\"device\":\"%s\",\"msg_type\":\"%s\"%s}",
                      config.device_name,
                      "WATCHDOG",
                      vbat)) {
          ERROR("Unable to send watchdog message");
        }
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

#define GETS(dst, src, size) \
  if ((tmp = src)) { \
    strlcpy(dst, tmp, size); \
  } \
  else { \
    DEBUG(F(" ERROR: Unable to get ")); \
    DEBUGLN(STRINGIZE(src)); \
    break; \
  }

#define GETI(dst, src) \
  if (src) { \
    dst = src; \
  } \
  else { \
    DEBUG(F(" ERROR: Unable to get ")); \
    DEBUGLN(STRINGIZE(src)); \
    break; \
  }

#define GETA(dst, src, size) \
  if (src.as<JsonArray>().copyTo(dst) != size) \
    ERROR(" Copy To " STRINGIZE(dst) " with inconsistent size")

#define GETIP(dst, src) \
  if (!str2ip(src, &dst)) \
    ERROR(" Bad IP Address or Mask format for " STRINGIZE(dst))

bool Maison::retrieve_config(JsonObject & _root, Config & _config)
{
  SHOW("retrieve_config()");

  DO {
    const char * tmp;

    GETI (_config.version,          _root["version"         ]);
    GETS (_config.device_name,      _root["device_name"     ], sizeof(_config.device_name     ));
    GETS (_config.wifi_ssid,        _root["ssid"            ], sizeof(_config.wifi_ssid       ));
    GETS (_config.wifi_password,    _root["wifi_password"   ], sizeof(_config.wifi_password   ));
    GETS (_config.mqtt_server,      _root["mqtt_server_name"], sizeof(_config.mqtt_server     ));
    GETS (_config.mqtt_username,    _root["mqtt_user_name"  ], sizeof(_config.mqtt_username   ));
    GETS (_config.mqtt_password,    _root["mqtt_password"   ], sizeof(_config.mqtt_password   ));
    GETI (_config.mqtt_port,        _root["mqtt_port"       ]);
    GETA (_config.mqtt_fingerprint, _root["mqtt_fingerprint"], 20);
    GETIP(_config.ip,               _root["ip"              ]);
    GETIP(_config.subnet_mask,      _root["subnet_mask"     ]);
    GETIP(_config.gateway,          _root["gateway"         ]);
    GETIP(_config.dns,              _root["dns"             ]);

    OK_DO;
  }

  SHOW_RESULT("retrieve_config()");

  return result;
}

bool Maison::load_config(int _version)
{
  File file;
  char filename[32];
  char str[20];

  SHOW("load_config()");

  if (_version == 0) {
    strcpy(filename, "/config.json");
  }
  else {
    strcpy(filename, "/config_");
    strcat(filename, itoa(_version, str, 10));
    strcat(filename, ".json");
  }

  DEBUG(F(" Config filename: ")); DEBUGLN(filename);

  DO {
    if (!SPIFFS.begin())          ERROR("SPIFFS.begin() not working");
    if (!SPIFFS.exists(filename)) ERROR("Config file does not esists");

    file = SPIFFS.open(filename, "r");
    if (!file) ERROR("Unable to open file");

    DynamicJsonBuffer jsonBuffer;

    JsonObject & root = jsonBuffer.parseObject(file);

    if (!root.success()) ERROR("Unable to parse JSON content");

    if (!retrieve_config(root, config)) ERROR("Unable to read config elements");

    OK_DO;
  }

  file.close();
  SHOW_RESULT("load_config()");

  #if MAISON_TESTING
    if (result) show_config(config);
  #endif

  return result;
}

#define PUT(src, dst) dst = src
#define PUTA(src, dst, len) dst.copyFrom(src)
#define PUTIP(src, dst) ip2str(src, buffer, 50); dst = buffer;

bool Maison::save_config()
{
  File file;

  SHOW("save_config()");

  DO {
    if (!SPIFFS.begin()) ERROR(" SPIFFS.begin() not working");
    if (SPIFFS.exists("/config_5.json")) SPIFFS.remove("/config_5.json");
    if (SPIFFS.exists("/config_4.json")) SPIFFS.rename("/config_4.json", "/config_5.json");
    if (SPIFFS.exists("/config_3.json")) SPIFFS.rename("/config_3.json", "/config_4.json");
    if (SPIFFS.exists("/config_2.json")) SPIFFS.rename("/config_2.json", "/config_3.json");
    if (SPIFFS.exists("/config_1.json")) SPIFFS.rename("/config_1.json", "/config_2.json");
    if (SPIFFS.exists("/config.json"  )) SPIFFS.rename("/config.json",   "/config_1.json");

    file = SPIFFS.open("/config.json", "w");

    if (!file) ERROR("Unable to open file /config.json");

    DynamicJsonBuffer jsonBuffer;

    JsonObject & root = jsonBuffer.createObject();
    if (!root.success()) ERROR("Unable to create JSON root object");

    JsonArray & arr = root.createNestedArray("mqtt_fingerprint");
    if (!arr.success()) ERROR("Unable to create JSON array object");

    PUT  (config.version,          root["version"         ]);
    PUT  (config.device_name,      root["device_name"     ]);
    PUT  (config.wifi_ssid,        root["ssid"            ]);
    PUT  (config.wifi_password,    root["wifi_password"   ]);
    PUTIP(config.ip,               root["ip"              ]);
    PUTIP(config.subnet_mask,      root["subnet_mask"     ]);
    PUTIP(config.gateway,          root["gateway"         ]);
    PUTIP(config.dns,              root["dns"             ]);
    PUT  (config.mqtt_server,      root["mqtt_server_name"]);
    PUT  (config.mqtt_username,    root["mqtt_user_name"  ]);
    PUT  (config.mqtt_password,    root["mqtt_password"   ]);
    PUT  (config.mqtt_port,        root["mqtt_port"       ]);
    PUTA (config.mqtt_fingerprint, arr, 20);

    if (!root.printTo(file)) ERROR("Unable to send JSON content to file /config.json");

    OK_DO;
  }

  file.close();

  SHOW_RESULT("save_config()");

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
    strcpy(config.device_name, mac_to_str(mac, str));
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
  SHOW("wifi_connect()");

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
        DEBUG(F("."));
        if (++attempt >= 150) {
          ERROR("Unable to connect to WiFi");
        }
      }
    }

    OK_DO;
  }

  SHOW_RESULT("wifi_connect()");

  return result;
}

bool Maison::init_callbacks(bool subscribe)
{
  static char topic[40];

  SHOW("init_callbacks()");

  DO {
    mqtt_client.setCallback(maison_callback);
    if (subscribe) {
      if (!mqtt_client.subscribe(
                   my_topic(CTRL_SUFFIX_TOPIC, topic, sizeof(topic)),
                   use_deep_sleep() ? 1 : 0)) {
        DEBUG(F(" Hum... unable to subscribe to topic (State:"));
        DEBUG(mqtt_client.state());
        DEBUG(F("): "));
        DEBUGLN(topic);
        break;
      }
      else {
        DEBUG(F(" Subscription completed to topic "));
        DEBUGLN(topic);
      }
    }

    DEBUGLN(buffer);
    if ((user_topic != NULL) && subscribe) {
      if (!mqtt_client.subscribe(user_topic, user_qos)) {
        DEBUG(F(" Hum... unable to subscribe to user topic (State:"));
        DEBUG(mqtt_client.state());
        DEBUG(F("): "));
        DEBUGLN(user_topic);
        break;
      }
      else {
        DEBUG(F(" Subscription completed to user topic "));
        DEBUGLN(user_topic);
      }
    }
    
    OK_DO;
  }

  return result;
}

bool Maison::mqtt_connect()
{
  SHOW("mqtt_connect()");

  DO {
    if (!wifi_connect()) ERROR("WiFi");

    if (!mqtt_connected()) {

      if (wifi_client != NULL) {
        delete wifi_client;
        wifi_client = NULL;
      }

      wifi_client = new BearSSL::WiFiClientSecure;

      wifi_client->setFingerprint(config.mqtt_fingerprint);
      mqtt_client.setClient(*wifi_client);
      mqtt_client.setServer(config.mqtt_server, config.mqtt_port);

      char client_name[30];
      strcpy(client_name, "client-");
      strcat(client_name, config.device_name);

      DEBUG(F("Client name: ")); DEBUGLN(client_name);

      if (use_deep_sleep()) {
        DEBUGLN(F(" Connect with clean-session off."));
        mqtt_client.connect(client_name,
                            config.mqtt_username,
                            config.mqtt_password,
                            NULL, 0, 0, NULL,            // Will message not used
                            false);  // Permanent session
        if (mqtt_connected()) {
          if (!init_callbacks(!mem.callbacks_initialized)) break;
        }
      }
      else {
        DEBUGLN(F(" Connect with clean-session on."));
        mqtt_client.connect(client_name,
                            config.mqtt_username,
                            config.mqtt_password);
        if (mqtt_connected() && !init_callbacks(true)) break;
      }

      if (!mqtt_connected()) {
        DEBUG(F(" Unable to connect to mqtt. State: "));
        DEBUGLN(mqtt_client.state());

        if (++connect_retry_count >= 5) {
          DEBUGLN(F(" Too many trials, reconnecting WiFi..."));
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

  SHOW_RESULT("mqtt_connect()");

  return result;
}

bool Maison::send_msg(const char * _topic, const char * _format, ...)
{
  SHOW("send_msg()");

  va_list args;
  va_start (args, _format);

  vsnprintf(buffer, MQTT_MAX_PACKET_SIZE, _format, args);

  DO {
    DEBUG(F(" Sending msg to "));
    DEBUG(_topic);
    DEBUG(F(": "));
    DEBUGLN(buffer);

    if (!mqtt_connected()) {
      ERROR("Unable to connect to mqtt server");
    }
    else if (!mqtt_client.publish(_topic, buffer)) {
      ERROR("Unable to publish message");
    }

    OK_DO;
  }

  SHOW_RESULT("send_msg()");

  return result;
}

bool Maison::log(const char * _format, ...)
{
  SHOW("log()");

  va_list args;
  va_start (args, _format);

  strcpy(buffer, get_device_name());
  strcat(buffer, ": ");
  int len = strlen(buffer);

  vsnprintf(&buffer[len], MQTT_MAX_PACKET_SIZE-len, _format, args);

  DO {
    DEBUG(F(" Log msg : "));
    DEBUGLN(buffer);

    if (!mqtt_connected()) {
      ERROR("Unable to connect to mqtt server");
    }
    else if (!mqtt_client.publish(MAISON_LOG_TOPIC, buffer)) {
      ERROR("Unable to log message");
    }

    OK_DO;
  }

  SHOW_RESULT("log()");

  return result;
}

void Maison::deep_sleep(bool _back_with_wifi, uint16_t _sleep_time_in_sec)
{
  SHOW("deep_sleep()");

  DEBUG(" Sleep Duration: ");
  DEBUGLN(_sleep_time_in_sec);

  DEBUG(" Network enabled on return: ");
  DEBUGLN(_back_with_wifi ? F("YES") : F("NO"));

  delay(10);

  uint32_t sleep_time = 1e6 * _sleep_time_in_sec;

  mem.one_hour_step_count += millis() + (1000u * _sleep_time_in_sec);
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

  if (mem.one_hour_step_count >= (ONE_HOUR * 1000)) {
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

  mem.magic        = RTC_MAGIC;
  mem.state        = STARTUP;
  mem.return_state = WAIT_FOR_EVENT;

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

char * Maison::my_topic(const char * _topic_suffix, char * _buffer, uint16_t _length)
{
  if (_length > (strlen(MAISON_PREFIX_TOPIC) + strlen(config.device_name) + strlen(_topic_suffix) + 1)) {
    strcpy(_buffer, MAISON_PREFIX_TOPIC);
    strcat(_buffer, config.device_name);
    strcat(_buffer, "/");
    strcat(_buffer, _topic_suffix);
    DEBUG(F("my_topic() result: ")); DEBUGLN(_buffer);
  }
  else {
    DEBUGLN(F("ERROR: my_topic(): Buffer too small!"));
    _buffer[0] = 0;
  }

  return _buffer;
}

void Maison::restart()
{
  save_mems();
  log("Restart requested.");
  delay(5000);
  ESP.restart();
  delay(1000);
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

#if MAISON_TESTING

  void Maison::show_config(Config & _config)
  {
    DEBUGLN(F("\nConfiguration:\n-------------"));

    DEBUG(F("Version       : ")); DEBUGLN(_config.version         );
    DEBUG(F("Device Name   : ")); DEBUGLN(_config.device_name     );
    DEBUG(F("WiFi SSID     : ")); DEBUGLN(_config.wifi_ssid       );
    DEBUG(F("WiFi Password : ")); DEBUGLN(F("<Hidden>")           );

    DEBUG(F("IP            : ")); DEBUGLN(ip2str(config.ip,          buffer, 50));
    DEBUG(F("DNS           : ")); DEBUGLN(ip2str(config.dns,         buffer, 50));
    DEBUG(F("Gateway       : ")); DEBUGLN(ip2str(config.gateway,     buffer, 50));
    DEBUG(F("Subnet Mask   : ")); DEBUGLN(ip2str(config.subnet_mask, buffer, 50));

    DEBUG(F("MQTT Server   : ")); DEBUGLN(_config.mqtt_server     );
    DEBUG(F("MQTT Username : ")); DEBUGLN(_config.mqtt_username   );
    DEBUG(F("MQTT Password : ")); DEBUGLN(F("<Hidden>")           );
    DEBUG(F("MQTT Port     : ")); DEBUGLN(_config.mqtt_port       );

    DEBUG(F("MQTT Fingerprint : ["));
    for (int i = 0; i < 20; i++) {
      DEBUG(_config.mqtt_fingerprint[i]);
      if (i < 19) DEBUG(F(","));
    }
    DEBUGLN(F("]"));
    DEBUGLN(F("---- The End ----"));
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
