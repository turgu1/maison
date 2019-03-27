#ifndef _UTILS_H
#define _UTILS_H

#ifndef DEBUGGING
  #define DEBUGGING 0
#endif

#if DEBUGGING
  #define SERIAL_SETUP Serial.begin(74880)
  #define PRINT(msg)   Serial.print(msg)
  #define PRINTLN(msg) Serial.println(msg)
#else
  #define SERIAL_SETUP
  #define PRINT(msg)
  #define PRINTLN(msg)
#endif

/*
 * ESP8266 pins need wired are below:
 * DIN (data in) on Matrix ---> 13 or MOSI on ESP8266
 * Clock(CLK) on Matrix --> 14 or SCK on ESP8266
 * CS pin on Matrix define below  --->( pick 15 on esp8266)
 */
    
const uint8_t LEDMATRIX_CS_PIN = 15;

// Define LED Matrix dimensions (0-n) - eg: 32x8 = 31x7

const int LEDMATRIX_WIDTH = 39;
const int LEDMATRIX_HEIGHT = 7;
const int LEDMATRIX_SEGMENTS = 5;

const int ANIM_DELAY = 50;

const uint8_t LED = 2;
const uint8_t BUZZER = 5;

void init_display();
void init_buzzer();
void init_blink();

void buzz_small();
void buzz1();
void buzz3();
void blink(int count);
bool displayText(char * theText, int len);

#undef PUBLIC
#endif