#define DEBUG           1

#define DEVICE_NAME     "BCG_MASTER"

#define SWITCH_PIN      4 // GPIO pin for the switch
#define DEBOUNCE_DELAY  500 // Debounce delay in milliseconds

#include <Arduino.h>
#include "eth_properties.h"
#include <ETH.h>
#include <WiFiUdp.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include <OSCMessage.h>


BluetoothSerial SerialBT; // Bluetooth Serial
Preferences preferences;  // Preferences for storing data
WiFiUDP Udp;

IPAddress ip, subnet, gateway;
IPAddress PC_IP(192, 168, 1, 200); // Default PC IP


uint16_t inPort = 7001;
uint16_t outPort = 7000;

IPAddress outIPs[] = {
  IPAddress(192, 168, 1, 101), // Default outgoing IP
  IPAddress(192, 168, 1, 102), // Default outgoing IP
  IPAddress(192, 168, 1, 103), // Default outgoing IP
  IPAddress(192, 168, 1, 104), // Default outgoing IP
  IPAddress(192, 168, 1, 105), // Default outgoing IP
  IPAddress(192, 168, 1, 106), // Default outgoing IP
  IPAddress(192, 168, 1, 107), // Default outgoing IP
  IPAddress(192, 168, 1, 108)  // Default outgoing IP
};

uint32_t lastMillis = 0;

bool status = 0;

const String HELP = "Available commands:\n"
                    "SET_IP <ip_address> - Set the device IP address\n"
                    "SET_SUBNET <subnet_mask> - Set the subnet mask\n"
                    "SET_GATEWAY <gateway_ip> - Set the gateway IP address\n"
                    "SET_OUTIP <outgoing_ip> - Set the outgoing IP address\n"
                    "SET_INPORT <port_number> - Set the input port (default 7001)\n"
                    "SET_OUTPORT <port_number> - Set the output port (default 7000)\n"
                    "GET - Get current configuration\n"
                    "IP - Show current IP address\n"
                    "MAC - Show current MAC address\n"
                    "HELP - Show this help message\n";

void saveIPAddress(const char* keyPrefix, IPAddress address) {
  for (int i = 0; i < 4; i++) {
    String key = String(keyPrefix) + i;
    preferences.putUInt(key.c_str(), address[i]);
  }
}

IPAddress loadIPAddress(const char* keyPrefix, IPAddress defaultIP) {
  IPAddress result;
  for (int i = 0; i < 4; i++) {
    String key = String(keyPrefix) + i;
    result[i] = preferences.getUInt(key.c_str(), defaultIP[i]);
  }
  return result;
}

void getConfig() {
  SerialBT.printf("IP: %s\n",         ip.toString().c_str());
  SerialBT.printf("Subnet: %s\n",     subnet.toString().c_str());
  SerialBT.printf("Gateway: %s\n",    gateway.toString().c_str());
  SerialBT.printf("In Port: %d\n",    inPort);
  SerialBT.printf("Out Port: %d\n",   outPort);
}

void saveNetworkConfig() {
  preferences.begin("CONFIG", false);
  saveIPAddress("ip", ip);
  saveIPAddress("sub", subnet);
  saveIPAddress("gw", gateway);
  preferences.putUInt("inPort", inPort); // Save input port
  preferences.putUInt("outPort", outPort); // Save output port
  preferences.end();
}

void loadNetworkConfig() {
  preferences.begin("CONFIG", true);
  ip      = loadIPAddress("ip",  IPAddress(192, 168, 1,  99));
  subnet  = loadIPAddress("sub", IPAddress(255, 255, 255, 0));
  gateway = loadIPAddress("gw",  IPAddress(192, 168, 1, 1  ));
  inPort  = preferences.getUInt("inPort", 7001); // Load input port
  outPort = preferences.getUInt("outPort", 7000); // Load output port
  preferences.end();
}

void resolumeOSCSend(){
  char address[40];  // increase buffer size
  snprintf(address, sizeof(address), "/column/1/connect");
  OSCMessage msg(address);
  msg.add(1);
  Udp.beginPacket(PC_IP, outPort);
  msg.send(Udp);
  Udp.endPacket();
  msg.empty();
  if (DEBUG){ Serial.println("Resolume OSC message sent."); }
}

void oscSend(int value) {
  char address[40];  // increase buffer size
  snprintf(address, sizeof(address), "/device/");
  if (DEBUG){ Serial.println(address); } // Debug: print the address to Serial
  OSCMessage msg(address);
  msg.add(value);
  Udp.beginPacket(outIPs[value-1], outPort);
  msg.send(Udp);
  Udp.endPacket();
  msg.empty();
}

void clearOSCSend(int value) {
  char address[40];  // increase buffer size
  snprintf(address, sizeof(address), "/clear/");
  if (DEBUG){ Serial.println(address); } // Debug: print the address to Serial
  OSCMessage msg(address);
  msg.add(value);
  Udp.beginPacket(outIPs[value], outPort);
  msg.send(Udp);
  Udp.endPacket();
  msg.empty();
}

void processOSCData(uint8_t data_In){
  if (DEBUG) { Serial.printf("Processing OSC data: %d\n", data_In); }
  if (!status){ oscSend(data_In); resolumeOSCSend(); status = 1; if (DEBUG){ Serial.println("OSC message sent."); }} // Send OSC message if status is true

}

void oscReceive() {
  int packetSize = Udp.parsePacket(); // Check if a packet is available
  if (packetSize > 0) {
    OSCMessage msgIn;
    if (DEBUG) { Serial.printf("Received OSC packet of size: %d\n", packetSize); } // Debug: print packet size
    while (packetSize--) { msgIn.fill(Udp.read()); }  // Fill the OSCMessage with incoming data 
    if (msgIn.fullMatch("/device/")) {                // Check if the address matches "/device/"
      int data = msgIn.getInt(0);                    // Get the integer value from the first argument
      processOSCData(data);
      if (DEBUG) {Serial.printf("Received OSC message: Address = /device/, Value = %d\n", data);}
    } else { Serial.println("Received OSC message with unmatched address."); }
    msgIn.empty(); // Clear the message after processing
  }
}



void processData(String data) {
  data.trim(); // Remove leading and trailing whitespace
  auto updateIP = [&](const String& prefix, IPAddress& target, int offset) {
    String value = data.substring(offset);
    if (target.fromString(value)) {
      saveNetworkConfig();
      SerialBT.printf("✅ %s updated and saved.\n", prefix.c_str());
    } else {
      SerialBT.printf("❌ Invalid %s format.\n", prefix.c_str());
    }
  };
  if (data.startsWith("SET_IP ")) { updateIP("IP", ip, 7); } 
  else if (data.startsWith("SET_SUBNET ")) { updateIP("Subnet", subnet, 11); } 
  else if (data.startsWith("SET_GATEWAY ")) { updateIP("Gateway", gateway, 12);  } 
  else if (data.startsWith ("SET_INPORT ")) {
    int port = data.substring(10).toInt();
    if (port > 0 && port < 65536) { inPort = static_cast<uint16_t>(port); saveNetworkConfig(); SerialBT.printf("✅ Input port set to %d and saved.\n", inPort); } 
    else { SerialBT.println("❌ Invalid port. Must be between 1 and 65535."); }
  }
  else if (data.startsWith("SET_OUTPORT ")) {
    int port = data.substring(12).toInt();
    if (port > 0 && port < 65536) { outPort = static_cast<uint16_t>(port); saveNetworkConfig(); SerialBT.printf("✅ Output port set to %d and saved.\n", outPort); } 
    else { SerialBT.println("❌ Invalid port. Must be between 1 and 65535."); }
  }
  else if (data == "GET") { getConfig(); }
  else if (data == "IP") { SerialBT.printf("ETH IP: %s\n", ETH.localIP().toString().c_str());}
  else if (data == "MAC") { SerialBT.printf("ETH MAC: %s\n", ETH.macAddress().c_str());}
  else if (data.indexOf("HELP")>=0){
    SerialBT.println(HELP);
    Serial.println(HELP);
    return;
  } else if (data.indexOf("GET")>=0){
    getConfig();
    return;
  }
}

void readBTSerial(){
  if (SerialBT.available()) {
    String incoming = SerialBT.readStringUntil('\n');
    processData(incoming);
    if (DEBUG) {SerialBT.println(incoming);}
  }
}

void readSwitch(){
  if (millis() - lastMillis < DEBOUNCE_DELAY){ return; } // Debounce delay
  if (digitalRead(SWITCH_PIN) == HIGH) { // Check if switch is pressed
    if (DEBUG) { Serial.println("Switch pressed"); }
    status = 0; // Toggle status
    for (int i = 0; i < 8; i++) { // Send OSC messages for all outputs
      clearOSCSend(i); // Clear OSC message with value i+1
    }
    
    clearOSCSend(1);
    lastMillis = millis();
  }
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("ETH Started");
      ETH.setHostname("esp32-ethernet");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      Serial.print("ETH IP: ");
      Serial.println(ETH.localIP());
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      Serial.println("ERROR: No Ethernet connection - Restarting ESP32... ");
      ESP.restart(); // Restart ESP32 if disconnected
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      break;
    default:
      break;
  }
}

void ethInit() {
  ETH.begin( ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE_0);
  ETH.config(ip, gateway, subnet);
  WiFi.onEvent(WiFiEvent);
  Udp.begin(inPort);
  delay(10); // Wait for the Ethernet to initialize
  Serial.println("ETH Initialized");
  Serial.printf("ETH IP: %s\n", ETH.localIP().toString().c_str());
  Serial.printf("ETH MAC: %s\n", ETH.macAddress().c_str());
}

void setup() {
  Serial.begin(115200);
  pinMode(SWITCH_PIN, INPUT_PULLUP); // Set switch pin as input with pull-up resistor
  SerialBT.begin(DEVICE_NAME); // Initialize Bluetooth Serial
  loadNetworkConfig(); // Load network configuration from Preferences
  ethInit(); // Initialize Ethernet
}

void loop() {
  readSwitch();   // Read switch state and send OSC message if pressed
  readBTSerial(); // Read data from Bluetooth Serial
  oscReceive();   // Check for incoming OSC messages
}