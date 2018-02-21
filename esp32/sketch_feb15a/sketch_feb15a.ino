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
  bool retained;
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
  newMessage.retained = mqttMessage.retained;
  return messageQueue.push(&newMessage);
}

void print(MqttMessage &mqttMessage) {
  Serial.print("Topic: ");
  Serial.print(mqttMessage.topic);
  Serial.print(" Body: ");
  Serial.println(mqttMessage.body);
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
  mqttMessage.retained = true;
  ++message_number;
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& messageBody = jsonBuffer.createObject();
  messageBody["open"] = doorOpen;
  messageBody["messageNumber"] = message_number;   
  messageBody.printTo(mqttMessage.body, 128);
  return publishMessage(mqttMessage);
}

void set_up_wifi() {
  Serial.print(publishWifiNotConnectedMessage(mqttMessage));
  print(mqttMessage);

  Serial.print(publishWifiConnectingMessage(mqttMessage));
  print(mqttMessage);
  
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(publishWifiConnectingMessage(mqttMessage));
    print(mqttMessage);
  }

  Serial.print(publishWifiConnectedMessage(mqttMessage));
  print(mqttMessage);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print(publishMqttServerConnectingMessage(mqttMessage));
    print(mqttMessage);
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
    print(mqttMessage);;
  }
}

void doorStatusChanged() {
  doorOpen = digitalRead(SWITCH_GPIO) == 0;
}

void publishQueuedMessage() {
    MqttMessage oldestMessage;
    messageQueue.pop(&oldestMessage);
    Serial.print("  Queued Message: ");
    Serial.print(client.publish(oldestMessage.topic, oldestMessage.body));
    print(oldestMessage);
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
  print(mqttMessage);
}

void loop() {
  if (!client.connected()) {
    Serial.print(publishMqttServerConnectionStateMessage(mqttMessage));
    print(mqttMessage);
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
      print(mqttMessage);

      oldDoorIsOpen = doorOpen;
    }
  }
}

