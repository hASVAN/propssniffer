#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <Ticker.h>
#include <ESP8266HTTPClient.h>
#include <vector>
// External library files of Espressif SDK
extern "C" {
#include "user_interface.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "mem.h"
#include "user_config.h"
}
HTTPClient http;

String json = "";

int isWork = 0;
uint16_t itemIndex = 0;
uint16_t maxItemIndex = 10;
const int ledPin = 2;
const char* clientSsid     = "probs";
const char* clientPassword = "0123456789987";

#define DATA_LENGTH           112

#define TYPE_MANAGEMENT       0x00
#define TYPE_CONTROL          0x01
#define TYPE_DATA             0x02
#define SUBTYPE_PROBE_REQUEST 0x04


struct RxControl {
  signed rssi: 8; // signal intensity of packet
  unsigned rate: 4;
  unsigned is_group: 1;
  unsigned: 1;
  unsigned sig_mode: 2; // 0:is 11n packet; 1:is not 11n packet;
  unsigned legacy_length: 12; // if not 11n packet, shows length of packet.
  unsigned damatch0: 1;
  unsigned damatch1: 1;
  unsigned bssidmatch0: 1;
  unsigned bssidmatch1: 1;
  unsigned MCS: 7; // if is 11n packet, shows the modulation and code used (range from 0 to 76)
  unsigned CWB: 1; // if is 11n packet, shows if is HT40 packet or not
  unsigned HT_length: 16; // if is 11n packet, shows length of packet.
  unsigned Smoothing: 1;
  unsigned Not_Sounding: 1;
  unsigned: 1;
  unsigned Aggregation: 1;
  unsigned STBC: 2;
  unsigned FEC_CODING: 1; // if is 11n packet, shows if is LDPC packet or not.
  unsigned SGI: 1;
  unsigned rxend_state: 8;
  unsigned ampdu_cnt: 8;
  unsigned channel: 4; //which channel this packet in.
  unsigned: 12;
};

// Delay times
// Delay of loop function in milli seconds
# define __delay__ 10
// Delay of channel changing in seconds
# define __dlay_ChannelChange__ 0.1
//Ticker for channel hopping


Ticker ts; 

struct SnifferPacket {
  struct RxControl rx_ctrl;
  uint8_t data[DATA_LENGTH];
  uint16_t cnt;
  uint16_t len;
};




static void showMetadata(SnifferPacket *snifferPacket) {
  if (isWork == 1) {
    unsigned int frameControl = ((unsigned int)snifferPacket->data[1] << 8) + snifferPacket->data[0];

    uint8_t version      = (frameControl & 0b0000000000000011) >> 0;
    uint8_t frameType    = (frameControl & 0b0000000000001100) >> 2;
    uint8_t frameSubType = (frameControl & 0b0000000011110000) >> 4;
    uint8_t toDS         = (frameControl & 0b0000000100000000) >> 8;
    uint8_t fromDS       = (frameControl & 0b0000001000000000) >> 9;

    // Only look for probe request packets
    if (frameType != TYPE_MANAGEMENT ||
        frameSubType != SUBTYPE_PROBE_REQUEST)
      return;

    if (itemIndex <= maxItemIndex) {

      uint8_t SSID_length = snifferPacket->data[25];
      char ssiddd[] = "";
      getssid(ssiddd, 26, SSID_length, snifferPacket->data);

      if (String(ssiddd) != "") {
        Serial.print("RSSI: ");
        Serial.print(snifferPacket->rx_ctrl.rssi, DEC);

        Serial.print(" Ch: ");
        Serial.print(wifi_get_channel());

        char addr[] = "00:00:00:00:00:00";
        getMAC(addr, snifferPacket->data, 10);
        Serial.print(" Peer MAC: ");
        Serial.print(addr);

        Serial.print(" SSID:");
        Serial.print(ssiddd);

       
        json = json + "{\"ch\":\""+String(wifi_get_channel())+"\",\"rssi\":\""+String(snifferPacket->rx_ctrl.rssi)+"\",\"MAC\":\""+String(addr)+"\",\"SSID\":\""+String(ssiddd)+"\"}";
        if(itemIndex < maxItemIndex){
          json = json + ",";
          }
        itemIndex++;
        Serial.println();
      }
    } else {
      isWork = 2;
      Serial.println("Fifo Full,turn to sending status\n");

    }
  }
}

/**
   Callback for promiscuous mode
*/
static void ICACHE_FLASH_ATTR sniffer_callback(uint8_t *buffer, uint16_t length) {
  struct SnifferPacket *snifferPacket = (struct SnifferPacket*) buffer;
  showMetadata(snifferPacket);
}

static void printDataSpan(uint16_t start, uint16_t size, uint8_t* data) {
  for (uint16_t i = start; i < DATA_LENGTH && i < start + size; i++) {
    Serial.write(data[i]);
  }
}

static void getssid(char *ssiddd, uint16_t start, uint16_t size, uint8_t* data) {
  for (uint16_t i = start; i < DATA_LENGTH && i < start + size; i++) {
    sprintf(ssiddd, "%s%c", ssiddd, data[i]);

  }
}

static void getMAC(char *addr, uint8_t* data, uint16_t offset) {
  sprintf(addr, "%02x:%02x:%02x:%02x:%02x:%02x", data[offset + 0], data[offset + 1], data[offset + 2], data[offset + 3], data[offset + 4], data[offset + 5]);
}

#define CHANNEL_HOP_INTERVAL_MS   1000
static os_timer_t channelHop_timer;

/**
   Callback for channel hoping
*/
// Change the WiFi channel
void channelCh(void) 
{ 
  if(isWork==1)
  {
    // Change the channels by modulo operation
    uint8 new_channel = wifi_get_channel()%12 + 1; 
    //Serial.printf("** Hop to %d **\n", new_channel); 
    wifi_set_channel(new_channel); 
  }
} 

#define DISABLE 0
#define ENABLE  1

void setup_promisc()
{
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(sniffer_callback);
  wifi_promiscuous_enable(1);
  Serial.println("Promisc mode");
  ts.attach(__dlay_ChannelChange__, channelCh);
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);
  isWork = 0;
}

void loop() {

  if (isWork == 0)
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(clientSsid, clientPassword);
    Serial.print("Connecting to wifi ");
    Serial.println(clientSsid);
    if(WiFi.status() != WL_CONNECTED) {
        Serial.println("");
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
    }
    http.begin("http://192.168.43.1:8080/new.php");
    http.addHeader("Content-Type", "application/json");
    http.POST("connected");
    http.end();
    Serial.println("props started");
    wifi_set_opmode(STATION_MODE);
    wifi_promiscuous_enable(0);
    isWork = 1;
    setup_promisc();
    itemIndex = 0;
    json = "";
    json = "{\"probes\":[";
  }
  if (isWork == 2)
  {
    wifi_set_opmode(STATION_MODE);
    wifi_promiscuous_enable(0);
    if (WiFi.isConnected())
    {
      Serial.print("Already connected!  ");
      Serial.println(WiFi.localIP());
      delay(10);
    }
    else
    {
      WiFi.mode(WIFI_STA);
      WiFi.begin(clientSsid, clientPassword);
      Serial.print("Connecting to wifi ");
      Serial.println(clientSsid);
      while (WiFi.status() != WL_CONNECTED)
      {
        delay(500);
        Serial.print(".");
      }
      Serial.print("Connected, IP address: ");
      Serial.println(WiFi.localIP());
    }

    json = json + "]}";
  Serial.println("json:" + json);
  
    http.begin("http://192.168.43.1:8080/wifi.php");
    http.addHeader("Content-Type", "application/json");
    http.POST(json);
    http.end();
    
    Serial.println("json Communication End");
    isWork = 1;
    setup_promisc();
    itemIndex = 0;
    json = "";
    json = "{\"probes\":[";



  }
}
