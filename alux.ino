#define FASTLED_ALLOW_INTERRUPTS 0

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <FastLED.h>
#include <NTPClient.h>

#define HOSTNAME "olp_bigclock"
#define NUM_LEDS 120
#define HOUR_OFFSET +1
#define ROTATION_OFFSET 0
#define REVERSE 1
#define DATA_PIN D4
#define BRIGHTNESS 255

#define ROLLOVER_LIMIT 12
#define MAX_INDICATORS 16

CRGB leds[NUM_LEDS];

#define LUX_UDP_PORT 2342
#define LUX_UDP_MAX 65507

char lux_data[LUX_UDP_MAX];

WiFiUDP lux_udp;

WiFiUDP ntp_udp;
NTPClient ntp_client(ntp_udp, "europe.pool.ntp.org", 3600, 360000);

typedef struct indicator_s indicator_t;

typedef CRGB (*indicator_falloff_fn)(indicator_t& self, int32_t i);

typedef struct indicator_s {
  uint32_t scale;
  uint32_t value;
  CRGB colour;
  uint16_t width;
  uint8_t id;
  bool rollover;
  indicator_falloff_fn falloff;
} indicator_t;

indicator_t indicators[MAX_INDICATORS];

CRGB falloff_12(indicator_t& self, int32_t i);
CRGB falloff_quad(indicator_t& self, int32_t i);

indicator_t ind_builtin_12 = { .scale = UINT32_MAX, .value = NUM_LEDS, .colour = CRGB( 24, 24, 24), .width = 1, .id = 255, .rollover = false, .falloff = &falloff_12 };
indicator_t ind_builtin_quad = { .scale = UINT32_MAX, .value = NUM_LEDS, .colour = CRGB( 50, 50, 50), .width = 1, .id = 254, .rollover = false, .falloff = &falloff_quad };

CRGB falloff_12(indicator_t& self, int32_t i) {
  int h12 = self.value / 12;
  if (!(i % h12))
    return self.colour;
  return CRGB::Black;
}

CRGB falloff_quad(indicator_t& self, int32_t i) {
  int quad = self.value / 4;
  if (!(i % quad))
    return self.colour;
  return CRGB::Black;
}

CRGB falloff_blend(indicator_t& self, int32_t i) {
  double s = NUM_LEDS / (double)self.scale;
  double v = self.value * s;
  double d = fabs((double)i - v);
  if (d < self.width / 2) {
    CRGB c = self.colour;
    int b = (d / ((double)self.width / 2)) * ((d / ((double)self.width / 2))*0.8f) * 3.8f * 255;
    c.fadeToBlackBy(min(b, 255));
    return c;
  }
  return CRGB::Black;
}

int walk_ribbon(CRGB ribbon[], uint16_t ribbon_len, indicator_t indicators[]) {
  for (int walk = -ROLLOVER_LIMIT; walk < ribbon_len + ROLLOVER_LIMIT; walk++) {
    for (int ind = 0; ind < MAX_INDICATORS; ind++) {
      if (indicators[ind].id > 0 && indicators[ind].falloff != NULL) {
        int w = walk;

        if(indicators[ind].rollover){
          if (w < 0)
            w = NUM_LEDS + w;
          if (w >= NUM_LEDS)
            w = w - NUM_LEDS;
        } else {
          if (w < 0 || w >= NUM_LEDS)
            continue;
        }

        w += ROTATION_OFFSET;
        if (w >= NUM_LEDS)
          w = w - NUM_LEDS;

        if (REVERSE) {
          w = -(w - NUM_LEDS);
          if (w == NUM_LEDS)
            w = 0;
        }

        ribbon[w] += indicators[ind].falloff(indicators[ind], walk);
      }
    }
  }
}

void ind_clear() {
  for (int ind = 0; ind < MAX_INDICATORS; ind++) {
    indicators[ind].id = 0;
  }
}

void ind_init() {
  ind_clear();
  indicators[0] = ind_builtin_quad;
  indicators[1] = ind_builtin_12;
  indicators[2] = {.scale = 6000, .value = 4000, .colour = CRGB::DarkCyan, .width = 3, .id = 2, .rollover = true, .falloff = &falloff_blend };
  indicators[3] = {.scale = 6000, .value = 4000, .colour = CRGB::Olive, .width = 7, .id = 3, .rollover = true, .falloff = &falloff_blend };
  indicators[4] = {.scale = 1200, .value = 600, .colour = CRGB::MediumVioletRed, .width = 9, .id = 4, .rollover = true, .falloff = &falloff_blend };
}

void setup() {
  Serial.begin(115200);

  WiFi.hostname(HOSTNAME);
  WiFiManager wifiManager;
  //reset saved settings
  //wifiManager.resetSettings();

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect(HOSTNAME);
  Serial.println("connected...yeey :)");

  ArduinoOTA.setHostname(HOSTNAME);
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
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  ind_init();

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setDither(0);

  lux_udp.begin(LUX_UDP_PORT);
  ntp_client.begin();
}

int offset_hours_12() {
  int h = ntp_client.getHours() % 12;
  h += HOUR_OFFSET;
  if (h < 0)
    h = 12 + h;
  if (h > 12) // 12 == 0, so this works correctly
    h = h - 12;
  return h;
}

void smooth_clock() {
  static int s_fn = ntp_client.getSeconds() * 100;
  static int m_fn = ntp_client.getMinutes() * 100;
  static int h_fn = (offset_hours_12()) * 100;
  static int ms = millis();

  int s_fn_ = ntp_client.getSeconds() * 100;
  int m_fn_ = ntp_client.getMinutes() * 100;
  int h_fn_ = (offset_hours_12()) * 100;
  int ms_ = millis();

  if (ms_ - ms < 20)
    return;

  ms = ms_;

  if (s_fn_ != s_fn) {
    s_fn += 12;
    if ((s_fn > s_fn_ && s_fn_ != 0 ) || s_fn >= 6000)
      s_fn = s_fn_;
  }
  if (m_fn_ != m_fn) {
    m_fn += 6;
    if ((m_fn > m_fn_ && m_fn_ != 0 ) || m_fn >= 6000)
      m_fn = m_fn_;
  }
  if (h_fn_ != h_fn) {
    h_fn += 1;
    if ((h_fn > h_fn_ && h_fn_ != 0 ) || h_fn >= 1200)
      h_fn = h_fn_;
  }

  indicators[2].value = s_fn;
  indicators[3].value = m_fn;
  indicators[4].value = h_fn;
}

void loop() {
  static unsigned long ms_last_packet = millis();
  static unsigned long idle_frame = 0;
  static IPAddress last_contact;
  static uint16_t last_contact_port = 0;
  static uint16_t remaining_size = 0;

  ArduinoOTA.handle();
  ntp_client.update();

  static uint16_t lux_data_index = 0;
  static uint16_t led_data_length = 0;

  if (lux_udp.parsePacket()) {
    lux_data_index += lux_udp.read(lux_data, LUX_UDP_MAX - lux_data_index)
  }

  if (lux_data_index >= 2 && led_data_length == 0) {
    led_data_length = lux_data[0] << 8;
    led_data_length = lux_data[1];

    if (led_data_length > LUX_UDP_MAX - 2 || led_data_length < 3) {
      led_data_length = 0;
      lux_data_index = 0;
    }
  }

  if (led_data_length + 2 <= lux_data_index) {
    memcpy((void*)leds, &lux_data[3], min(NUM_LEDS * sizeof(CRGB), led_data_length));
    FastLED.show();
    ms_last_packet = millis();
    last_contact = lux_udp.remoteIP();
    last_contact_port = lux_udp.remotePort();

    uint16_t overlength = lux_data_index - led_data_length + 2;
    
    if (overlength) {
      memmove(lux_data, &lux_data[led_data_length + 2], overlength);
      lux_data_index = overlength;
      led_data_length = 0;
    }
  }

  smooth_clock();

  if ((ms_last_packet + 2000) < millis()) {
    FastLED.clear();

    walk_ribbon(leds, NUM_LEDS, indicators);

    if (last_contact_port > 0) {
      lux_udp.beginPacket(last_contact, last_contact_port);
      lux_udp.write("\n");
      char l[2];
      for (int d = 0; d < NUM_LEDS; d++) {
        sprintf(l, "%02X", leds[d].getLuma());
        lux_udp.write(l);
      }
      lux_udp.endPacket();  
    }
    
    FastLED.show();
    #ifdef DEBUG_L
    char l[2];
    for (int d = 0; d < NUM_LEDS; d++) {
      sprintf(l, "%02X", leds[d].getLuma());
      Serial.print(l);
    }
    Serial.println();
    delay(20);
    #endif
  }
}
