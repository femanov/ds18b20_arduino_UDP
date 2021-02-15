#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Wire.h>
#include <jm_LCM2004A_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define N 5
#define SERVER_PORT 12333

jm_LCM2004A_I2C lcd;
bool lcdState;
unsigned long dhcp_time;

EthernetUDP Udp;
IPAddress clientAddr;
uint16_t clientPort;
bool clientConnected = false;
unsigned char cliPackCount;

OneWire firstMod(2);
OneWire secondMod(3);
OneWire thirdMod(5);
OneWire forthMod(6);
OneWire room(9);
DallasTemperature lines[N] = {DallasTemperature(&firstMod), DallasTemperature(&secondMod), DallasTemperature(&thirdMod), DallasTemperature(&forthMod), DallasTemperature(&room)};

int resolution = 12;
uint8_t ndevs[N];
int16_t **rawTemps;
DeviceAddress **devAddr;
unsigned long lastConvTime;

char packetBuffer[UDP_TX_PACKET_MAX_SIZE];  // buffer to hold incoming packet,

void setup() {
  // DallasTemperature lines start
  for (int i = 0; i < N; i++) {
    lines[i].begin();
  }

  int lcount;
  byte mac[] = {0xDE, 0xAD, 0xBE, 0x12, 0x3E, 0xED};
  IPAddress ip(192, 168, 11, 2); // default IP, when no DHCP, for tests
  IPAddress gateway(192, 168, 11, 1);
  IPAddress subnet(255, 255, 255, 0);

  Serial.begin(115200);
  for (int i = 0; i < N; i++) {
    Serial.println(lines[i].getDeviceCount());
  }

  while (!Serial) {
    delay(1);
  }
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
    lcd.set_cursor(0, 0);
    lcd.print("Hello! Getting IP");
  }

  if (Ethernet.begin(mac) == 0) { // try DHCP
    // use fallback ip for development
    Ethernet.setLocalIP(ip);
    Ethernet.setSubnetMask(subnet);
  }

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    if (lcdState) {
      lcd.set_cursor(0, 0);
      lcd.print("No Ethernet hardware!");
    }
    else {
      Serial.println("No Ethernet hardware!");
    }
    delay(1000);
    //do something?
  }
  if (Ethernet.linkStatus() == LinkOFF) {
    lcd.set_cursor(0, 3);
    lcd.print("No Ethernet cable");
    delay(1000);
    //do something?
  }

  if ( Udp.begin(SERVER_PORT) ) {
    lcd.set_cursor(0, 3);
    lcd.print(Ethernet.localIP());
    lcd.print(":");
    lcd.print(SERVER_PORT);
    lcd.set_cursor(0, 0); lcd.print("                    ");
  }

  Serial.println("Starting 1-wire devices");
  lcd.set_cursor(0, 2);
  lcd.print("ndevs: ");

  rawTemps = (int16_t**)malloc(sizeof(int16_t*) * N);
  devAddr = (DeviceAddress**)malloc(sizeof(DeviceAddress*) * N);
  for (int i = 0; i < N; i++) {
    ndevs[i] = lines[i].getDeviceCount();
    rawTemps[i] = (int16_t*)malloc(sizeof(int16_t) * ndevs[i]);
    devAddr[i] = (DeviceAddress*)malloc(sizeof(DeviceAddress) * ndevs[i]);

    for (lcount = 0; lcount < ndevs[i]; lcount++) {
      lines[i].getAddress(devAddr[i][lcount], lcount);
      lines[i].setResolution(devAddr[i][lcount], resolution);
    }
    lines[i].setWaitForConversion(false);
    lcd.print(ndevs[i]);
    lcd.print(";");
  }
  lastConvTime = millis();

  Serial.print(ndevs[1]);
  Serial.println(" devices found");

  dhcp_time = millis();
}

void send_packet(uint8_t packType, uint8_t line_num, uint8_t *data, uint8_t data_length, uint8_t numdevs) {
  int lc;
  uint16_t sum = 0;

  sum += packType;
  sum += numdevs;
  for (lc = 0; lc < data_length; lc++) {
    sum += data[lc];
  }
  for (lc = 0; lc < sizeof(lastConvTime); lc++) {
    sum += ((uint8_t*)&lastConvTime)[lc];
  }
  uint8_t cs = lowByte(sum);

  Udp.beginPacket(clientAddr, clientPort);
  Udp.write(packType);
  Udp.write(numdevs);
  Udp.write(line_num);
  Udp.write(data, data_length);
  Udp.write((uint8_t*)&lastConvTime, sizeof(lastConvTime));
  Udp.write(cs);
  Udp.endPacket();
}

void loop() {
  int lc;
  unsigned long ntime;
  byte res;
  int dhcp_request_counter = 0;

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
      for (lc = 0; lc < 4; lc++) {
        Serial.print(clientAddr[lc], DEC);
        if (lc < 3) {
          Serial.print(".");
        }
      }
      Serial.print(":");
      Serial.println(clientPort);
      // send initial data
      send_packet(1, (uint8_t)0, (uint8_t*)(&devAddr[1][0]), sizeof(DeviceAddress)*ndevs[1], ndevs[1]);
    }
    Serial.println(packetBuffer);
  }

  ntime = millis();

  // does IP from DHCP should be updated. 600000 ms == 10 min
  if (ntime - dhcp_time > 600000 ) {
    dhcp_request_counter += 1;
    res = Ethernet.maintain();
    if (res == 0) {
      Serial.println("DHCP request is None");
    }
    if (res == 1) {
      Serial.println("Lease time update was unsuccessful");
    }
    if (res == 2) {
      Serial.println("Lease time update was successful");
      dhcp_time = ntime;
    }
    if (res == 3) {
      Serial.println("IP adress update was unsuccessful");
    }
    if (res == 4) {
      Serial.println("IP adress update was successful");
      dhcp_time = ntime;
    }
  }
  else
  {
    if (dhcp_request_counter > 10) {
      dhcp_request_counter = 0;
      dhcp_time = ntime;
    }
  }

  for (int i = 0; i < N; i++) {
    if (lines[i].isConversionComplete()) {
      for (lc = 0; lc < ndevs[i]; lc++) {
        rawTemps[i][lc] = lines[i].getTemp(devAddr[i][lc]);
      }

      lcd.set_cursor(6 * i, 0);
      lcd.print(lines[i].rawToCelsius(rawTemps[i][0]));
      lcd.set_cursor(6 * i, 1);
      lcd.print(lines[i].rawToCelsius(rawTemps[i][1]));

      lines[i].requestTemperatures();
      lastConvTime = millis();

      if (clientConnected) {
        send_packet(2, (uint8_t)(i + 1), (uint8_t*)rawTemps[i], sizeof(int16_t)*ndevs[i], ndevs[i]);
        cliPackCount++;
        if (cliPackCount == 20) {
          clientConnected = false;
        }
      }
    }
  }
  delay(1);
}
