#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Wire.h>
#include <jm_LCM2004A_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 2
#define SERVER_PORT 12333

jm_LCM2004A_I2C lcd;
bool lcdState;

EthernetUDP Udp;
IPAddress clientAddr;
uint16_t clientPort;
bool clientConnected = false;
unsigned char cliPackCount;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

int resolution = 12;
uint8_t ndevs;
int16_t *rawTemps;
DeviceAddress *devAddr;
unsigned long lastConvTime;

char packetBuffer[UDP_TX_PACKET_MAX_SIZE];  // buffer to hold incoming packet,

void setup() {
  int lcount;
  byte mac[] = {0xDE, 0xAD, 0xBE, 0x12, 0x3E, 0xED};
  IPAddress ip(192, 168, 11, 2); // default IP, when no DHCP, for tests
  IPAddress gateway(192, 168, 11, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  Serial.begin(115200);
  while (!Serial) { delay(1); }
  Serial.println("DS1820 to ethernet server starting");

  Wire.begin();
  lcdState = lcd.begin();
  if (lcdState) {
    Serial.println("20x4 LCD found");
    }
  else {
    Serial.println("20x4 LCD not found");    
    }

  if (lcdState) {
    lcd.set_cursor(0,0);
    lcd.print("hello! Getting IP");
    }

  if (Ethernet.begin(mac) == 0){ // try DHCP
    // use fallback ip for development
    Ethernet.setLocalIP(ip);
    Ethernet.setSubnetMask(subnet);
  }
  
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    if (lcdState) {
      lcd.set_cursor(0,0);
      lcd.print("No Ethernet hardware!");
      }
    else {
      Serial.println("No Ethernet hardware!");
      }
    delay(1000);
    //do something?
  }
  if (Ethernet.linkStatus() == LinkOFF) {
    lcd.set_cursor(0,3);
    lcd.print("No Ethernet cable");
    delay(1000);
    //do something?
  }

  if ( Udp.begin(SERVER_PORT) ) {
     lcd.set_cursor(0,3);
     lcd.print(Ethernet.localIP());
     lcd.print(":");
     lcd.print(SERVER_PORT);
     lcd.set_cursor(0,0);lcd.print("                    ");
    }

  Serial.println("Starting 1-wire devices");
  sensors.begin();
  ndevs = sensors.getDeviceCount();
  rawTemps = (int16_t*)malloc(sizeof(int16_t)*ndevs);
  devAddr = (DeviceAddress*)malloc(sizeof(DeviceAddress)*ndevs);

  lcd.set_cursor(0,2);
  lcd.print("ndevs: ");
  lcd.print(ndevs);
  Serial.print(ndevs);
  Serial.println(" devices found");
  
  for (lcount=0;lcount<ndevs;lcount++){
    sensors.getAddress(devAddr[lcount],lcount);
    sensors.setResolution(devAddr[lcount], resolution);
    }
  sensors.setWaitForConversion(false);

  lastConvTime = millis();
}

void send_packet(uint8_t packType, uint8_t *data, uint8_t data_length){
  int lc;
  uint16_t sum=0;

  sum += packType;
  sum += ndevs;
  for (lc=0; lc < data_length; lc++){
    sum += data[lc];
    }
  for (lc=0; lc < sizeof(lastConvTime); lc++){
    sum += ((uint8_t*)&lastConvTime)[lc];
    }
  uint8_t cs = lowByte(sum);

  Udp.beginPacket(clientAddr, clientPort);
  Udp.write(packType);
  Udp.write(ndevs);
  Udp.write(data, data_length);
  Udp.write((uint8_t*)&lastConvTime, sizeof(lastConvTime));
  Udp.write(cs);
  Udp.endPacket();
}

void loop() {
  int lc;
  unsigned long ntime;

  int packetSize = Udp.parsePacket();
  if (packetSize) {
    if (clientConnected && clientAddr == Udp.remoteIP() && clientPort == Udp.remotePort()) { 
      Serial.println("keepalive recieved");
      cliPackCount = 0;  
      }
    else {
      clientAddr = Udp.remoteIP();
      clientPort = Udp.remotePort();
      clientConnected = true;
      Udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
      Serial.println("new client:");
      for (lc=0; lc < 4; lc++) {
        Serial.print(clientAddr[lc], DEC);
        if (lc < 3) {
          Serial.print(".");
          }
        }
      Serial.print(":");
      Serial.println(clientPort);
      // send initial data
      send_packet(1, (uint8_t*)(&devAddr[0]), sizeof(DeviceAddress)*ndevs);  
      }
    Serial.println(packetBuffer);
  }

  ntime = millis();
  if (sensors.isConversionComplete() ) {
    for (lc=0;lc<ndevs;lc++){
      rawTemps[lc] = sensors.getTemp(devAddr[lc]);
      }

    lcd.set_cursor(0,0);
    lcd.print(sensors.rawToCelsius(rawTemps[0]));
    lcd.set_cursor(0,1);
    lcd.print(sensors.rawToCelsius(rawTemps[1]));

    sensors.requestTemperatures();
    lastConvTime = millis();  

    if (clientConnected) {
      send_packet(2, (uint8_t*)rawTemps, sizeof(int16_t)*ndevs);
      cliPackCount++;
      if (cliPackCount == 20){
        clientConnected = false;
        }
      }
    }
  
  delay(1);
}
