#define FASTLED_ALLOW_INTERRUPTS 0

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <FastLED.h>
#include "FastLED_RGBW.h"

#define HOSTNAME_PREFIX "lux"
#define DATA_PIN D4
#define BRIGHTNESS_LIMIT 255
#define MAX_LEDS ((144*4)+(60*5))
#define LUX_UDP_PORT 2342

#define LUX_UDP_MAX (MAX_LEDS *  4)
CRGBW leds[MAX_LEDS];
CRGB *ledsRGB = (CRGB *) &leds[0];

uint8_t lux_data[LUX_UDP_MAX];
WiFiUDP lux_udp;
char cb[200];

void setup() {
  Serial.begin(115200);
  String hostname = String(strlen(HOSTNAME_PREFIX) + 6 + 1);
  hostname = HOSTNAME_PREFIX;
  hostname += WiFi.macAddress().substring(6,8);
  hostname += WiFi.macAddress().substring(9,11);
  hostname += WiFi.macAddress().substring(12,14);
  hostname += WiFi.macAddress().substring(15,17);
  WiFi.hostname(hostname);
  WiFiManager wifiManager;
  //reset saved settings
  //wifiManager.resetSettings();
//luxEB75DD40
  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //and goes into a blocking loop awaiting configuration
  hostname.toCharArray(cb, sizeof(cb));
  wifiManager.autoConnect(cb);
  Serial.println("connected...yeey :)");

  hostname.toCharArray(cb, sizeof(cb));
  ArduinoOTA.setHostname(cb);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  FastLED.addLeds<WS2812B, DATA_PIN, RGB>(ledsRGB, getRGBWsize(MAX_LEDS));
  FastLED.setBrightness(BRIGHTNESS_LIMIT);
  FastLED.setDither(0);

  lux_udp.begin(LUX_UDP_PORT);
}

void rainbow(){
	static uint8_t hue;
 
	for(int i = 0; i < MAX_LEDS; i++){
		leds[i] = CHSV((i * 256 / MAX_LEDS) + hue, 255, 255);
	}
	FastLED.show();
	hue++;
}

void loop() {
  static unsigned long ms_last_packet = millis();
  static unsigned long idle_frame = 0;
  static IPAddress last_contact;
  static uint16_t last_contact_port = 0;
  static uint16_t remaining_size = 0;

  ArduinoOTA.handle();

  static uint16_t lux_data_index = 0;
  static uint16_t led_data_length = 0;

  if (lux_udp.parsePacket()) {
    lux_data_index += lux_udp.read(&(lux_data[lux_data_index]), LUX_UDP_MAX - lux_data_index);
  }

  if (lux_data_index >= 2 && led_data_length == 0) {
    led_data_length = lux_data[0] << 8;
    led_data_length += lux_data[1];

    if ((led_data_length > LUX_UDP_MAX - 2) || (led_data_length < 3)) {
      led_data_length = 0;
      lux_data_index = 0;
    }
  }

  if (led_data_length + 2 <= lux_data_index) {
    memcpy((char*)leds, &(lux_data[2]), min(sizeof(leds), (unsigned int) led_data_length));
    FastLED.show();
    ms_last_packet = millis();
    last_contact = lux_udp.remoteIP();
    last_contact_port = lux_udp.remotePort();

    uint16_t overhang = lux_data_index - (led_data_length + 2);
    
    if (overhang) {
      memmove(lux_data, &(lux_data[led_data_length + 2]), overhang);
      lux_data_index = overhang;
      led_data_length = 0;
    } else {
      lux_data_index = 0;
      led_data_length = 0;
    }
  }

  if ((ms_last_packet + 20000) < millis()) {
    
  }
}
