#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <LanConfiguration.h>

#ifndef SWITCH_GPIO
#define SWITCH_GPIO  4
#endif

// Values defined in LanConfiguration.h
const char ssid[] = SSID;
const char password[] = PASSWORD;
const byte mqtt_server_ip[] = MQTT_SERVER_IP;
const int mqtt_server_port = MQTT_SERVER_PORT;
const char STATE_OF_HEALTH[] = "stateOfHealth";
const char DOOR_STATUS[] = "doorStatus";

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
char json[128];
int message_number = 0;
volatile bool oldDoorIsOpen = true;
volatile bool doorOpen = true;

bool publishWifiNotConnectedMessage(char json[]) {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["wifiConnectionStatus"] = "notConnected";
  messageBody.printTo(json, 128);
  return client.publish(STATE_OF_HEALTH, json);
}

bool publishWifiConnectingMessage(char json[]) {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["wifiConnectionStatus"] = "connecting";
  messageBody["ssid"] = ssid;
  messageBody.printTo(json, 128);
  return client.publish(STATE_OF_HEALTH, json);
}

bool publishWifiConnectedMessage(char json[]) {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["wifiConnectionStatus"] = "connected";
  messageBody["ipAddress"] = WiFi.localIP().toString();
  messageBody.printTo(json, 128);
  return client.publish(STATE_OF_HEALTH, json);
}

bool publishMqttServerConnectingMessage(char json[]) {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["mqttConnectionState"] = "connecting";
  messageBody.printTo(json, 128);
  return client.publish(STATE_OF_HEALTH, json);
}

bool publishMqttServerConnectionStateMessage(char json[]) {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["mqttConnectionState"] = client.state();
  messageBody.printTo(json, 128);
  return client.publish(STATE_OF_HEALTH, json);
}

bool publishDoorStatusChangedMessage(char json[], bool doorOpen) {
  ++message_number;
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["open"] = doorOpen;
  messageBody["messageNumber"] = message_number;   
  messageBody.printTo(json, 128);
  return client.publish(DOOR_STATUS, json, true);
}

void set_up_wifi() {
  Serial.print(publishWifiNotConnectedMessage(json));
  Serial.println(json);

  Serial.print(publishWifiConnectingMessage(json));
  Serial.println(json);
  
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(publishWifiConnectingMessage(json));
    Serial.println(json);
  }

  Serial.print(publishWifiConnectedMessage(json));
  Serial.println(json);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print(publishMqttServerConnectingMessage(json));
    Serial.println(json);
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    // Attempt to connect
    if (!client.connect(clientId.c_str())) {
      // Wait 5 seconds before retrying
      delay(5000);
    }

    // Publish the connection result
    Serial.print(publishMqttServerConnectionStateMessage(json));
    Serial.println(json);
  }
}

void doorStatusChanged() {
  doorOpen = digitalRead(SWITCH_GPIO) == 0;
}

void setup() {
  // Initializes the pseudo-random number generator
  randomSeed(micros());

  // Set GPIO to input with pulldown to avoid floating pin when circuit is broken
  pinMode(SWITCH_GPIO, INPUT_PULLDOWN);

  // To avoid polling, use interrupts whenever the pin's value changes
  attachInterrupt(digitalPinToInterrupt(SWITCH_GPIO), doorStatusChanged, CHANGE);
  
  Serial.begin(115200);
  // Add a newline to separate this code from boot up code
  Serial.println();
  
  set_up_wifi();
  client.setServer(mqtt_server_ip, mqtt_server_port);

  // Set the initial state of the door
  doorStatusChanged();
  Serial.print(publishDoorStatusChangedMessage(json, doorOpen));
  Serial.println(json);
}

void loop() {
  if (!client.connected()) {
    Serial.print(publishMqttServerConnectionStateMessage(json));
    Serial.println(json);
    reconnect();
  }
  // Allow the client to process incoming messages and maintain its connection to the server
  client.loop();


  long now = millis();
  if (now - lastMsg > 1000) {
    lastMsg = now;
    
    if (doorOpen != oldDoorIsOpen) {
      Serial.print(publishDoorStatusChangedMessage(json, doorOpen));
      Serial.println(json);

      oldDoorIsOpen = doorOpen;
    }
  }
}

