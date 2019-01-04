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

The MQTT based transmission architecture is specific to this implementation. Further documentation to come soon...

Stay tuned...
