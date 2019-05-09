#define FASTLED_ALLOW_INTERRUPTS 0

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <FastLED.h>
#include <NTPClient.h>

//#define DEBUG_L

#define NUM_LEDS 64
#define DATA_PIN D4
#define MAX_INDICATORS 16
CRGB leds[NUM_LEDS];

#define LUX_UDP_PORT 2342
#define LUX_UDP_MAX 1024
char lux_data[LUX_UDP_MAX];
WiFiUDP lux_udp;

WiFiUDP ntp_udp;
NTPClient ntp_client(ntp_udp, "europe.pool.ntp.org", 3600, 60000);

typedef struct indicator_s indicator_t;

typedef CRGB (*indicator_falloff_fn)(indicator_t& self, int32_t i);

typedef struct indicator_s{
    uint32_t scale;
    uint32_t value;
    CRGB colour;
    uint16_t width;
    uint8_t id;
    bool rollover;
    indicator_falloff_fn falloff;
} indicator_t;

indicator_t indicators[MAX_INDICATORS];

CRGB falloff_quad(indicator_t& self, int32_t i);
indicator_t ind_builtin_quad = { .scale = UINT32_MAX, .value = NUM_LEDS-1, .colour = CRGB::AliceBlue, .width = 1, .id = 255, .rollover = false, .falloff = &falloff_quad };

CRGB falloff_quad(indicator_t& self, int32_t i){
    int quad = self.value / 4;
    if (i < 0 || i > NUM_LEDS)
        return CRGB::Black;
    if (!(i % quad))
        return self.colour;
    return CRGB::Black;
}

CRGB falloff_blend(indicator_t& self, int32_t i){
    double s = NUM_LEDS / (double)self.scale;
    double v = self.value * s;
    double d = fabs((double)i - v);
    if(d < self.width/2){
        CRGB c = self.colour;
        c.fadeToBlackBy((d / ((double)self.width/2)) * 255);
        return c;
    }
    return CRGB::Black;
}

int walk_ribbon(CRGB ribbon[], uint16_t ribbon_len, indicator_t indicators[]){
    FastLED.clear();
    for(int walk = -12; walk < ribbon_len+12; walk++){
        for(int ind = 0; ind < MAX_INDICATORS; ind++){
            if(indicators[ind].id > 0 && indicators[ind].falloff != NULL){
                int w = walk;
                if (w < 0)
                    w = NUM_LEDS + w;
                if (w >= NUM_LEDS)
                    w = w - NUM_LEDS;
                ribbon[w] += indicators[ind].falloff(indicators[ind], walk);
            }
        }
    }
}

void ind_clear(){
    for(int ind = 0; ind < MAX_INDICATORS; ind++){
        indicators[ind].id = 0;
    }
}

void ind_init(){
    ind_clear();
    indicators[0] = ind_builtin_quad;
    indicators[1] = {.scale = 30000, .value = 500, .colour = CRGB::Red, .width = 12, .id = 1, .rollover = true, .falloff = &falloff_blend };
    indicators[2] = {.scale = 6000, .value = 4000, .colour = CRGB::Blue, .width = 5, .id = 1, .rollover = true, .falloff = &falloff_blend };
    indicators[3] = {.scale = 6000, .value = 4000, .colour = CRGB::Yellow, .width = 7, .id = 1, .rollover = true, .falloff = &falloff_blend };
    indicators[4] = {.scale = 1200, .value = 600, .colour = CRGB::Green, .width = 11, .id = 1, .rollover = true, .falloff = &falloff_blend };
}

void setup() {
    // put your setup code here, to run once:
    Serial.begin(115200);

    //WiFiManager
    WiFi.hostname("olp_clock");
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;
    //reset saved settings
    //wifiManager.resetSettings();
    
    //set custom ip for portal
    //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

    //fetches ssid and pass from eeprom and tries to connect
    //if it does not connect it starts an access point with the specified name
    //here  "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration
    wifiManager.autoConnect("OLPClockAP");
    //or use this for auto generated name ESP + ChipID
    //wifiManager.autoConnect();

    
    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
    
    ArduinoOTA.setHostname("olp_clock");
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

    delay(2000);
    FastLED.addLeds<WS2812B, DATA_PIN, RGB>(leds, NUM_LEDS);
    FastLED.setBrightness(24);
    
    lux_udp.begin(LUX_UDP_PORT);
    ntp_client.begin();
    ntp_client.update();
    indicators[2].value = ntp_client.getSeconds()*100;
}

void smooth_clock(){
    static int s_fn = ntp_client.getSeconds()*100;
    static int m_fn = ntp_client.getMinutes()*100;
    static int h_fn = (ntp_client.getHours()%12)*100;
    
    s_fn = ntp_client.getSeconds()*100;
    m_fn = ntp_client.getMinutes()*100;
    h_fn = (ntp_client.getHours()%12)*100;
    
    indicators[2].value = s_fn;
    indicators[3].value = m_fn;
    indicators[4].value = h_fn;
}

void loop() {
    static unsigned long arduino_ms_last_packet = millis();
    static unsigned long idle_frame = 0;
    
    ArduinoOTA.handle();
    ntp_client.update();

    int received_size = lux_udp.parsePacket();
    if (received_size)
    {
        int len = lux_udp.read(lux_data, LUX_UDP_MAX);
        if (len > 0)
        {
            memcpy(leds, lux_data, len);
            FastLED.show();
            arduino_ms_last_packet = millis();
        }
    }

    if ((arduino_ms_last_packet + 2000) < millis()) {
            
        smooth_clock();

        walk_ribbon(leds,NUM_LEDS,indicators);
        #ifndef DEBUG_L
        FastLED.show();
        #else
        Serial.println();
        char l[2];
        for(int d=0;d<NUM_LEDS;d++){
            sprintf(l,"%02X",leds[d].getLuma());
            Serial.print(l);
        }
        #endif
        delay(20);
    }
}
