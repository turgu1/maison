#include <Maison.h>

BearSSL::WiFiClientSecure Maison::wifi_client;
PubSubClient  Maison::mqtt_client;

static Maison * maison;

Maison::Maison() :
  last_reconnect_attempt(0),
  user_cb(NULL),
  user_topic(NULL),
  user_qos(0),
  feature_mask(NONE),
  user_mem(NULL),
  user_mem_length(0)
{
  maison = this;
}

Maison::Maison(uint8_t _feature_mask) :
  last_reconnect_attempt(0),
  user_cb(NULL),
  user_topic(NULL),
  user_qos(0),
  feature_mask(_feature_mask),
  user_mem(NULL),
  user_mem_length(0)
{
  maison = this;
}

Maison::Maison(uint8_t _feature_mask, void * _user_mem, uint8_t _user_mem_length) :
  last_reconnect_attempt(0),
  user_cb(NULL),
  user_topic(NULL),
  user_qos(0),
  feature_mask(_feature_mask),
  user_mem(_user_mem),
  user_mem_length(_user_mem_length)
{
  maison = this;
}

bool Maison::setup()
{
  SHOW("\nMaison::setup()\n");

  DO {
    if (! load_mems()) ERROR("Unable to load states");
    if (! load_config()) ERROR("Unable to load config");

    if (hard_reset()) {
      mem.state = mem.sub_state = STARTUP;
      mem.hours_24_count = 0;
      mem.one_hour_step_count = 0;
    }

    if (network_required()) {
      if (!wifi_connect()) ERROR("WiFi");
      update_device_name();
    }
    
    DEBUG(F("MQTT_MAX_PACKET_SIZE = ")); DEBUGLN(MQTT_MAX_PACKET_SIZE);

    OK_DO;
  }

  SHOW_RESULT("Maison::setup()");

  return result;
}

void maison_callback(const char * topic, byte * payload, unsigned int length)
{
  SHOW("---------- maison_callback() ---------------");

  if (maison != NULL) maison->process_callback(topic, payload, length);
  
  DEBUGLN(F(" End of maison_callback()"));
}

void Maison::process_callback(const char * topic, byte * payload, unsigned int length)
{
  static char buffer[1024];

  SHOW("process_callback()");

  if (strcmp(topic, my_topic(MAISON_DEVICE_CTRL_SUFFIX_TOPIC, buffer, 40)) == 0) {
    int len;

    DEBUG(" Received MQTT Message: ");
    
    memcpy(buffer, payload, len = (length > 1023) ? 1023 : length);
    buffer[len] = 0;
    DEBUGLN(buffer);

    if (strncmp(buffer, "CONFIG?", 7) == 0) {
      DEBUGLN(F(" Config content requested by Maison"));

      File file = SPIFFS.open("/config.json", "r");
      if (!file) {
        DEBUGLN(" ERROR: Unable to open current config file");
      }
      else {
        mqtt_client.beginPublish(MAISON_CTRL_TOPIC, 
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
    else if (strncmp(buffer, "CONFIG:", 7) == 0) {
      DEBUGLN(F(" New config received"));

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
        }
      }
    }
    else if (strncmp(buffer, "STATE?", 6) == 0) {
      send_msg(
        MAISON_STATUS_TOPIC, 
        "{\"device\":\"%s\",\"msg_type\":\"STATE\",\"state\":%u,\"hours\":%u,\"millis\":%u}",
        config.device_name,
        mem.state,
        mem.hours_24_count,
        mem.one_hour_step_count);
    }
    else if (strncmp(buffer, "RESTART!", 8) == 0) {
      DEBUGLN("Device is restarting");
      ESP.reset();
      delay(200);
    }
  }
  else if (user_cb != NULL) {
    DEBUGLN(F(" Calling user callback"));
    (*user_cb)(topic, payload, length);
  }
}

bool Maison::setUserCallback(Callback * _cb, const char * _topic, uint8_t _qos)
{
  user_cb    = _cb;
  user_topic = _topic;
  user_qos   = _qos;

  bool result = false;

  SHOW("setUserCallback()");

  if (mqtt_client.connected()) {
    if (!mqtt_client.subscribe(user_topic, user_qos)) {
      DEBUG(F("Hum... unable to subscribe to user topic (State:"));
      DEBUG(mqtt_client.state());
      DEBUG("): ");
    }
    else {
      DEBUG(F("Subscription completed to user topic "));
      result = true;
    }
    DEBUGLN(user_topic);            
  }
  else {
    result = true;
  }

  SHOW_RESULT("setUserCallback()");

  return result;
}

void Maison::loop(Process * process) 
{ 
  State new_state, new_sub_state;
  static long last_time_count = 0;

  DEBUG(F("Maison::loop(): Current state: "));
  DEBUGLN(mem.state);

  if (network_required()) {
    if (!mqtt_connected()) {
      if (!mqtt_connect()) {
        DEBUGLN(F("No connecton to mqtt server. Waiting 5 seconds to retry..."));
        if (on_battery_power()) {
          deep_sleep(true, 5);
          delay(1000);
        } 
        else {
          delay(5000);
          return;
        }
      }
    }
  }
 
  if (mqtt_connected()) mqtt_loop();
  
  DEBUG(F("MQTT Connected: ")); DEBUGLN(mqtt_connected() ? "Yes" : "No");

  new_state     = mem.state;
  new_sub_state = mem.sub_state;

  UserResult res = call_user_process(process);

  DEBUG(F("User process result: ")); DEBUGLN(res);

  switch (mem.state) {
    case STARTUP:
      if (show_voltage()) {
        if (!send_msg(MAISON_STATUS_TOPIC, 
                      "{\"device\":\"%s\",\"msg_type\":\"%s\",\"VBAT\":%3.1f}", 
                      config.device_name, 
                      "STARTUP", 
                      battery_voltage())) {
          ERROR("Unable to send startup message");
        }
      }
      else {
        if (!send_msg(MAISON_STATUS_TOPIC, 
                      "{\"device\":\"%s\",\"msg_type\":\"%s\"}", 
                      config.device_name,
                      "STARTUP")) {
          ERROR("Unable to send startup message");
        }
      }
      if (res != NOT_COMPLETED) {
        new_state     = WAIT_FOR_EVENT;
        new_sub_state = WAIT_FOR_EVENT;
      }
      break;

    case WAIT_FOR_EVENT:
      if (res == NEW_EVENT) {
        new_state     = PROCESS_EVENT;
        new_sub_state = PROCESS_EVENT;
      }
      else {
        new_sub_state = WAIT_FOR_EVENT;
        new_state = check_if_24_hours_time(WAIT_FOR_EVENT);
      }
      break;

    case PROCESS_EVENT:
      if (res == ABORTED) {
        new_state = new_sub_state = WAIT_FOR_EVENT;
      }
      else if (res != NOT_COMPLETED) {
        new_state = new_sub_state = WAIT_END_EVENT;
      }
      else {
        new_sub_state = PROCESS_EVENT;
        new_state = check_if_24_hours_time(PROCESS_EVENT);
      }
      break;

    case WAIT_END_EVENT:
      if (res != NOT_COMPLETED) {
        new_state = new_sub_state = WAIT_FOR_EVENT;
      }
      else {
        new_sub_state = WAIT_END_EVENT;
        new_state = check_if_24_hours_time(WAIT_END_EVENT);
      }
      break;

    case END_EVENT:
      if (res != NOT_COMPLETED) {
        new_state = new_sub_state = WAIT_FOR_EVENT;
      }
      break;

    case HOURS_24:
      delay(100);
      if (mqtt_connected()) mqtt_loop(); // Second chance to process received msgs
      if (watchdog_enabled()) {
        if (show_voltage()) {
          if (!send_msg(MAISON_STATUS_TOPIC, 
                        "{\"device\":\"%s\",\"msg_type\":\"%s\",\"VBAT\":%3.1f}", 
                        config.device_name, 
                        "WATCHDOG",
                        battery_voltage())) {
            ERROR("Unable to send watchdog message");
          }
        }
        else {
          if (!send_msg(MAISON_STATUS_TOPIC, 
                        "{\"device\":\"%s\",\"msg_type\":\"%s\"}", 
                        config.device_name,
                        "WATCHDOG")) {
            ERROR("Unable to send watchdog message");
          }
        }
      }

      new_state = new_sub_state;
      break;
  }

  mem.state     = new_state;
  mem.sub_state = new_sub_state;

  DEBUG(" Next state: "); DEBUGLN(mem.state);

  if (on_battery_power()) {
    uint16_t wait_count = is_short_reboot_time_needed() ? 5 : ONE_HOUR;

    mem.one_hour_step_count += (wait_count * 1000) + millis();
    
    DEBUGLN(" Prepare for deep sleep");
    save_mems();
    deep_sleep(network_required(), wait_count);
    delay(1000);
    DEBUGLN(" HUM... Not suppose to come here after deep_sleep call...");
  }
  else {
    mem.one_hour_step_count += millis() - last_time_count;
    last_time_count = millis();
  }

  DEBUG(F(" One hour step count (")); 
  DEBUG(mem.hours_24_count); 
  DEBUG("): "); 
  DEBUGLN(mem.one_hour_step_count);
  
  DEBUGLN("End of loop()");

  delay(10);
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

bool Maison::retrieve_config(JsonObject & root, Config & config)
{
  SHOW("retrieve_config()");

  DO {
    const char * tmp;

    GETI(config.version,          root["version"         ]);
    GETS(config.device_name,      root["device_name"     ], sizeof(config.device_name     ));
    GETS(config.wifi_ssid,        root["ssid"            ], sizeof(config.wifi_ssid       ));
    GETS(config.wifi_password,    root["wifi_password"   ], sizeof(config.wifi_password   ));
    GETS(config.mqtt_server,      root["mqtt_server_name"], sizeof(config.mqtt_server     ));
    GETS(config.mqtt_username,    root["mqtt_user_name"  ], sizeof(config.mqtt_username   ));
    GETS(config.mqtt_password,    root["mqtt_password"   ], sizeof(config.mqtt_password   ));
    GETI(config.mqtt_port,        root["mqtt_port"       ]);
    GETA(config.mqtt_fingerprint, root["mqtt_fingerprint"], 20);

    OK_DO;
  }

  SHOW_RESULT("retrieve_config()");

  return result;
}

bool Maison::load_config(int version)
{
  File file;
  char filename[32];
  char str[20];

  SHOW("load_config()");

  if (version == 0) {
    strcpy(filename, "/config.json");
  }
  else {
    strcpy(filename, "/config_");
    strcat(filename, itoa(version, str, 10));
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
// #define PUTA(src, dst, len) for (int i = 0; i < len; i++) dst.add(src[i])

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

    PUT (config.version,          root["version"         ]    );
    PUT (config.device_name,      root["device_name"     ]    );
    PUT (config.wifi_ssid,        root["ssid"            ]    );
    PUT (config.wifi_password,    root["wifi_password"   ]    );
    PUT (config.mqtt_server,      root["mqtt_server_name"]    );
    PUT (config.mqtt_username,    root["mqtt_user_name"  ]    );
    PUT (config.mqtt_password,    root["mqtt_password"   ]    );
    PUT (config.mqtt_port,        root["mqtt_port"       ]    );
    PUTA(config.mqtt_fingerprint, arr, 20);

    if (!root.printTo(file)) ERROR("Unable to send JSON content to file /config.json");

    OK_DO;
  }

  file.close();

  SHOW_RESULT("save_config()");
  
  return result;
}

char * Maison::mac_to_str(uint8_t * mac, char * buff)
{ 
  const char * hex = "0123456789ABCDEF";
  char * ptr = buff;
  for (int i = 0; i < 6; i++) {
    *ptr++ = hex[mac[i] >> 4];
    *ptr++ = hex[mac[i] & 0x0F];
  }
  *ptr = 0;

  return buff; 
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

bool Maison::mqtt_reconnect()
{
  SHOW("mqtt_reconnect()");

  if (!mqtt_connected()) {

    DEBUGLN(F("setClient..."));
    mqtt_client.setClient(wifi_client);
    
    DEBUGLN(F("setServer..."));
    mqtt_client.setServer(config.mqtt_server, config.mqtt_port);

    wifi_client.setFingerprint(config.mqtt_fingerprint);

    DEBUGLN(F("connect..."));
    
    if (!mqtt_client.connect(config.device_name, config.mqtt_username, config.mqtt_password)) {
      DEBUG(F("Unable to connect to mqtt. State: "));
      DEBUGLN(mqtt_client.state());
    }
    if (mqtt_connected()) {
      static char buffer[40];
      mqtt_client.setCallback(maison_callback);
      if (!mqtt_client.subscribe(my_topic(MAISON_DEVICE_CTRL_SUFFIX_TOPIC, buffer, 40))) {
        DEBUG(F("Hum... unable to subscribe to topic (State:"));
        DEBUG(mqtt_client.state());
        DEBUG("): ");
      }
      else {
        DEBUG(F("Subscription completed to topic "));
      }
      DEBUGLN(buffer);
      if (user_topic != NULL) {
        if (!mqtt_client.subscribe(user_topic, user_qos)) {
          DEBUG(F("Hum... unable to subscribe to user topic (State:"));
          DEBUG(mqtt_client.state());
          DEBUG("): ");
        }
        else {
          DEBUG(F("Subscription completed to user topic "));
        }
        DEBUGLN(user_topic);        
      }
    }
  }
  
  bool result = mqtt_connected();

  SHOW_RESULT("mqtt_reconnect()");

  return result;
}

bool Maison::mqtt_connect()
{
  SHOW("mqtt_connect()");

  uint8_t retry_count = 0;

  DO {
    if (wifi_connected()) {
      if (mqtt_connected()) {
        OK_DO;
      }
      else { 
        long now = millis();
        if ((now - last_reconnect_attempt) > 2000) {
          last_reconnect_attempt = now;
          if (mqtt_reconnect()) {
            last_reconnect_attempt = 0;
            OK_DO;
          }
          else {
            if (++retry_count > 5) {
              DEBUGLN(F(" Too many trials, resetting WiFi..."));
              WiFi.disconnect();
              retry_count = 0;
              continue;
            }
            DEBUG("!");
          }
        }
        else {
          delay(100);
          DEBUG(F("-"));
        }
      }
    } 
    else {
      if (!wifi_connect()) ERROR("WiFi");
    }
    // Loop
  }

  SHOW_RESULT("mqtt_connect()");

  return result;
}

bool Maison::send_msg(const char * _topic, const char * format, ...)
{
  SHOW("send_msg()");

  static char msg[512];

  va_list args;
  va_start (args, format);

  vsnprintf(msg, 512, format, args);
  
  DO {
    DEBUG(F(" Sending msg to ")); DEBUG(_topic); DEBUG(F(": ")); DEBUGLN(msg);

    if (!mqtt_connect()) ERROR("Unable to connect to mqtt server"); 
    
    if (!mqtt_client.publish(_topic, msg)) {
      ERROR("Unable to publish message");
    }

    OK_DO;
  }

  SHOW_RESULT("send_msg()");

  return result;
}

void Maison::deep_sleep(bool back_with_wifi, int sleep_time_in_sec)
{
  mqtt_client.disconnect();
  WiFi.mode(WIFI_OFF);
  
  delay(10);
  
  ESP.deepSleep(
    sleep_time_in_sec * 1e6, 
    back_with_wifi ? WAKE_RF_DEFAULT : WAKE_RF_DISABLED);  
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
      if (!read_mem((uint32_t *) &user_mem, user_mem_length, sizeof(mem))) {
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
      if (!write_mem((uint32_t *) &user_mem, user_mem_length, sizeof(mem))) {
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

  mem.magic     = RTC_MAGIC;
  mem.state     = STARTUP;
  mem.sub_state = WAIT_FOR_EVENT;

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

  bool result = write_mem((uint32_t *) &user_mem, user_mem_length, (sizeof(mem) + 3) >> 2);

  SHOW_RESULT("init_user_mem()");

  return result;
}

bool Maison::read_mem(uint32_t * data, uint16_t length, uint16_t addr)
{
  SHOW("read_mem()");

  DEBUG(F("  data addr: "));  DEBUGLN((int)data);
  DEBUG(F("  length: "));     DEBUGLN(length);
  DEBUG(F("  pos in rtc: ")); DEBUGLN(addr);

  DO {
    if (!ESP.rtcUserMemoryRead(addr, (uint32_t *) data, length)) {
      ERROR("Unable to read from rtc memory");
    }

    uint32_t csum = CRC32((uint8_t *)(data + 4), length - 4);
    
    if (data[0] != csum) ERROR("Data in RTC memory with bad checksum!");

    OK_DO;
  }

  SHOW_RESULT("read_mem()");

  return result;
}

bool Maison::write_mem(uint32_t * data, uint16_t length, uint16_t addr)
{
  SHOW("write_mem()");

  DEBUG(F("  data addr: "));  DEBUGLN((int)data);
  DEBUG(F("  length: "));     DEBUGLN(length);
  DEBUG(F("  pos in rtc: ")); DEBUGLN(addr);

  data[0] = CRC32((uint8_t *)(data + 4), length - 4);

  DO {     
    if (!ESP.rtcUserMemoryWrite(addr, (uint32_t *) data, length)) {
      ERROR("Unable to write to rtc memory");
    }

    OK_DO;
  }
    
  SHOW_RESULT("write_mem()");

  return result;
}

uint32_t Maison::CRC32(const uint8_t * data, size_t length)
{
  uint32_t crc = 0xffffffff;

  while (length--) {
    uint8_t c = *data++;
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

  return crc;
}

char * Maison::my_topic(const char * topic, char * buffer, uint16_t buffer_length) 
{
  if (buffer_length > (strlen(MAISON_PREFIX_TOPIC) + strlen(config.device_name) + strlen(topic))) {
    strcpy(buffer, MAISON_PREFIX_TOPIC);
    strcat(buffer, config.device_name);
    strcat(buffer, topic);
    DEBUG(F("my_topic() result: ")); DEBUGLN(buffer);
  }
  else {
    DEBUGLN(F("ERROR: my_topic(): Buffer too small!"));
    buffer[0] = 0;
  }

  return buffer;
}


#if MAISON_TESTING

  void Maison::show_config(Config & config)
  {
    DEBUGLN(F("\nConfiguration:\n-------------"));
    DEBUG(F("Version          : ")); DEBUGLN(config.version         );
    DEBUG(F("Device Name      : ")); DEBUGLN(config.device_name     );
    DEBUG(F("WiFi SSID        : ")); DEBUGLN(config.wifi_ssid       );
    DEBUG(F("WiFi Password    : ")); DEBUGLN(F("<Hidden>")          );
    DEBUG(F("MQTT Server      : ")); DEBUGLN(config.mqtt_server     );
    DEBUG(F("MQTT Username    : ")); DEBUGLN(config.mqtt_username   );
    DEBUG(F("MQTT Password    : ")); DEBUGLN(F("<Hidden>")          );
    DEBUG(F("MQTT Port        : ")); DEBUGLN(config.mqtt_port       );

    DEBUG(F("MQTT Fingerprint : [")); 
    for (int i = 0; i < 20; i++) { 
      DEBUG(config.mqtt_fingerprint[i]); 
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
