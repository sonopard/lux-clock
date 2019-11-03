// disable interrupts while the FastLED data pump is running to prevent flickering during WiFi activity
#define FASTLED_ALLOW_INTERRUPTS 0

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager

// NOTE:  FastLED does not have any support for 4-color strips. 
// this code uses a hack as outlined in https://www.partsnotincluded.com/programming/fastled-rgbw-neopixels-sk6812/
// meanwhile, an alternative fork of FastLED exists and may be preferable: https://github.com/coryking/FastLED

#include <FastLED.h>
#include "FastLED_RGBW.h"

// a unique hostname is automatically generated from the MACID following this prefix, e.g. luxEB75DD40
#define HOSTNAME_PREFIX "lux"
// output pin. only one output channel is currently supported, multiple strips can be forked or daisy-chained.
#define DATA_PIN D4
// lower this value to limit strip brightness, also affects power consumption. power management should be performed through FastLED's builtin mechanism.
#define BRIGHTNESS_LIMIT 255
// affects memory use and update speed.
#define MAX_LEDS ((144*4)+(60*5))
// port to listen on
#define LUX_UDP_PORT 2342

// no user-configurable values beyond this point!

// packet size
#define LUX_UDP_MAX (MAX_LEDS *  4)
// FastLED structures
CRGBW leds[MAX_LEDS];
CRGB *ledsRGB = (CRGB *) &leds[0];

// packet receive buffer
uint8_t lux_data[LUX_UDP_MAX];
// arduino UDP socket
WiFiUDP lux_udp;
// general purpose buffer for sprintf etc.
char cb[200];

void setup()
{
  Serial.begin(115200);
  
  // construct hostname string
  String hostname = String(strlen(HOSTNAME_PREFIX) + 6 + 1);
  hostname = HOSTNAME_PREFIX;
  hostname += WiFi.macAddress().substring(6,8);
  hostname += WiFi.macAddress().substring(9,11);
  hostname += WiFi.macAddress().substring(12,14);
  hostname += WiFi.macAddress().substring(15,17);
  hostname.toCharArray(cb, sizeof(cb));
  
  WiFi.hostname(hostname);
  WiFiManager wifiManager;
  // uncomment to reset saved settings
  //wifiManager.resetSettings();

  // fetches ssid and key from eeprom/flash and tries to connect
  // if it does not connect it starts an access point with the specified name
  // and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect(cb);
  Serial.println("connected...yeey :)");
  
  // initialize over-the-air update features
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

  // print this client's IP on the UART for debugging purposes
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // initialize FastLED with RGBW hack
  FastLED.addLeds<WS2812B, DATA_PIN, RGB>(ledsRGB, getRGBWsize(MAX_LEDS));
  FastLED.setBrightness(BRIGHTNESS_LIMIT);
  FastLED.setDither(0);

  // open and listen on UDP 
  lux_udp.begin(LUX_UDP_PORT);
}

// a simple idle animation, used when no data is received.
void rainbow()
{
	static uint8_t hue;
 
	for(int i = 0; i < MAX_LEDS; i++){
		leds[i] = CHSV((i * 256 / MAX_LEDS) + hue, 255, 255);
	}
	FastLED.show();
	hue++;
}

void loop()
{
  // timestamp of last received data packet
  static unsigned long ms_last_packet = millis();
  // frame counter for idle animation
  static unsigned long idle_frame = 0;
  // IP of host that sent the last packet
  static IPAddress last_contact;
  // originating port that sent the last packet
  static uint16_t last_contact_port = 0;
  // expected packet data length counter
  static uint16_t remaining_size = 0;
  // receive buffer write position
  static uint16_t lux_data_index = 0;
  // payload length
  static uint16_t led_data_length = 0;

  // handle over-the-air update requests
  ArduinoOTA.handle();

  // handle incoming UDP packets
  if (lux_udp.parsePacket()) {
    // copy data into receive buffer
    // increment receive buffer write position by amount of data received
    // only consume as many bytes from the packet buffer as the receive buffer can contain.
    lux_data_index += lux_udp.read(&(lux_data[lux_data_index]), LUX_UDP_MAX - lux_data_index);
  }
  
  // check if we should have the payload length bytes
  if (lux_data_index >= 2 && led_data_length == 0) {
    // calculate payload length from length bytes
    led_data_length = lux_data[0] << 8;
    led_data_length += lux_data[1];

    // if payload length invalid, reset all counters
    if ((led_data_length > LUX_UDP_MAX - 2) || (led_data_length < 3)) {
      led_data_length = 0;
      lux_data_index = 0;
    }
  }

  // if we have a full data payload,
  if (led_data_length + 2 <= lux_data_index) {
    // copy the contens of the receive buffer to the FastLED buffer
    memcpy((char*)leds, &(lux_data[2]), min(sizeof(leds), (unsigned int) led_data_length));
    // and drive the DATA_PIN output to feed the data to the LEDs
    FastLED.show();
    // set last valid receive timestamp, host, and port.
    ms_last_packet = millis();
    last_contact = lux_udp.remoteIP();
    last_contact_port = lux_udp.remotePort();

    // are there extra bytes at the end of the receive buffer,
    uint16_t overhang = lux_data_index - (led_data_length + 2);
    // indicating a partially received or buffered packet
    if (overhang) {
      // then move those bytes to the start of the receive buffer
      memmove(lux_data, &(lux_data[led_data_length + 2]), overhang);
      // and initially the counters accordingly
      lux_data_index = overhang;
      led_data_length = 0;
    } else {
      // if no "overhang" was detected, re-initialize all counters to zero
      lux_data_index = 0;
      led_data_length = 0;
    }
  }

  if ((ms_last_packet + 20000) < millis()) {
    // add your idle animation here, if one is desired. for use with OLP, this is usually left disabled.
  }
}
