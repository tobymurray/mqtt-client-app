#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <cppQueue.h>
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

typedef struct MqttMessage {
  char topic[128];
  char body[128];
};
MqttMessage mqttMessage;


WiFiClient espClient;
PubSubClient client(espClient);
// A place to hold messages when MQTT server connection drops
Queue messageQueue(sizeof(MqttMessage), 10, QueueType::FIFO); 
long lastMsg = 0;
int message_number = 0;
volatile bool oldDoorIsOpen = true;
volatile bool doorOpen = true;

bool publishMessage(MqttMessage &mqttMessage) {
  MqttMessage newMessage;
  strcpy(newMessage.topic, mqttMessage.topic);
  strcpy(newMessage.body, mqttMessage.body);
  return messageQueue.push(&newMessage);
}

bool publishWifiNotConnectedMessage(MqttMessage &mqttMessage) {
  strcpy(mqttMessage.topic, STATE_OF_HEALTH);
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["wifiConnectionStatus"] = "notConnected";
  messageBody.printTo(mqttMessage.body, 128);
  return publishMessage(mqttMessage);
}

bool publishWifiConnectingMessage(MqttMessage &mqttMessage) {
  strcpy(mqttMessage.topic, STATE_OF_HEALTH);
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["wifiConnectionStatus"] = "connecting";
  messageBody["ssid"] = ssid;
  messageBody.printTo(mqttMessage.body, 128);
  return publishMessage(mqttMessage);
}

bool publishWifiConnectedMessage(MqttMessage &mqttMessage) {
  strcpy(mqttMessage.topic, STATE_OF_HEALTH);
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["wifiConnectionStatus"] = "connected";
  messageBody["ipAddress"] = WiFi.localIP().toString();
  messageBody.printTo(mqttMessage.body, 128);
  return publishMessage(mqttMessage);
}

bool publishMqttServerConnectingMessage(MqttMessage &mqttMessage) {
  strcpy(mqttMessage.topic, STATE_OF_HEALTH);
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["mqttConnectionState"] = "connecting";
  messageBody.printTo(mqttMessage.body, 128);
  return publishMessage(mqttMessage);
}

bool publishMqttServerConnectionStateMessage(MqttMessage &mqttMessage) {
  strcpy(mqttMessage.topic, STATE_OF_HEALTH);
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["mqttConnectionState"] = client.state();
  messageBody.printTo(mqttMessage.body, 128);
  return publishMessage(mqttMessage);
}

bool publishDoorStatusChangedMessage(MqttMessage &mqttMessage, bool doorOpen) {
  strcpy(mqttMessage.topic, DOOR_STATUS);
  ++message_number;
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["open"] = doorOpen;
  messageBody["messageNumber"] = message_number;   
  messageBody.printTo(mqttMessage.body, 128);
  return client.publish(mqttMessage.topic, mqttMessage.body, true);
}

void set_up_wifi() {
  Serial.print(publishWifiNotConnectedMessage(mqttMessage));
  Serial.print(mqttMessage.topic);
  Serial.println(mqttMessage.body);

  Serial.print(publishWifiConnectingMessage(mqttMessage));
  Serial.print(mqttMessage.topic);
  Serial.println(mqttMessage.body);
  
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(publishWifiConnectingMessage(mqttMessage));
    Serial.print(mqttMessage.topic);
    Serial.println(mqttMessage.body);
  }

  Serial.print(publishWifiConnectedMessage(mqttMessage));
  Serial.print(mqttMessage.topic);
  Serial.println(mqttMessage.body);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print(publishMqttServerConnectingMessage(mqttMessage));
    Serial.print(mqttMessage.topic);
    Serial.println(mqttMessage.body);
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    // Attempt to connect
    if (!client.connect(clientId.c_str())) {
      // Wait 5 seconds before retrying
      delay(5000);
    }

    // Publish the connection result
    Serial.print(publishMqttServerConnectionStateMessage(mqttMessage));
    Serial.print(mqttMessage.topic);
    Serial.println(mqttMessage.body);
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
  Serial.print(publishDoorStatusChangedMessage(mqttMessage, doorOpen));
  Serial.print(mqttMessage.topic);
  Serial.println(mqttMessage.body);
}

void publishQueuedMessage() {
    MqttMessage oldestMessage;
    messageQueue.pop(&oldestMessage);
    Serial.print("  Queued Message: ");
    Serial.print(client.publish(oldestMessage.topic, oldestMessage.body));
    Serial.print(oldestMessage.topic);
    Serial.println(oldestMessage.body);
}

void loop() {
  if (!client.connected()) {
    Serial.print(publishMqttServerConnectionStateMessage(mqttMessage));
    Serial.print(mqttMessage.topic);
    Serial.println(mqttMessage.body);
    reconnect();
  }
  // Allow the client to process incoming messages and maintain its connection to the server
  client.loop();

  while (!messageQueue.isEmpty()) {
    Serial.print("There are ");
    Serial.print(messageQueue.nbRecs());
    Serial.println(" messages queued up.");
    publishQueuedMessage();
  }


  long now = millis();
  if (now - lastMsg > 1000) {
    lastMsg = now;
    
    if (doorOpen != oldDoorIsOpen) {
      Serial.print(publishDoorStatusChangedMessage(mqttMessage, doorOpen));
      Serial.print(mqttMessage.topic);
      Serial.println(mqttMessage.body);

      oldDoorIsOpen = doorOpen;
    }
  }
}

