#include <FS.h>

#include <ESP8266WiFi.h>

#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

#include <PubSubClient.h>

#include <ArduinoJson.h>

#define MQTT_VERSION MQTT_VERSION_3_1_1

// MQTT: ID, server IP, port, username and password
#define MQTT_CLIENT_ID "fan_switch1"
char mqttServer[40] = "192.168.0.2";
char mqttPort[6] = "1883";
#define MQTT_USER ""
#define MQTT_PASSWORD ""

// MQTT: topics
#define MQTT_RELAY_STATE_TOPIC "living/fan1/status"
#define MQTT_RELAY_COMMAND_TOPIC "living/fan1/switch"

// WIFI: WifiManager and custom parameters
WiFiManager wifiManager;
WiFiManagerParameter customMqttServer("server", "mqtt server", mqttServer, 40);
WiFiManagerParameter customMqttPort("port", "mqtt port", mqttPort, 5);
boolean shouldSaveConfig = false;

// payloads by default (on/off)
#define RELAY_ON "ON"
#define RELAY_OFF "OFF"

#define RELAY_PIN D3

// relay is turned off by default
boolean mRelayState = false; 

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// function called to publish the state of the relay (on/off)
void publishRelayState() {
  if (mRelayState) {
    client.publish(MQTT_RELAY_STATE_TOPIC, RELAY_ON, true);
  } else {
    client.publish(MQTT_RELAY_STATE_TOPIC, RELAY_OFF, true);
  }
}

// function called to turn on/off the relay
void setRelayState() {
  if (mRelayState) {
    digitalWrite(RELAY_PIN, HIGH);
    Serial.println("INFO: Turn relay on...");
  } else {
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("INFO: Turn relay off...");
  }
}

// function called when a MQTT message arrived
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {
  // concat the payload into a string
  String payload;
  for (uint8_t i = 0; i < p_length; i++) {
    payload.concat((char)p_payload[i]);
  }
  
  // handle message topic
  if (String(MQTT_RELAY_COMMAND_TOPIC).equals(p_topic)) {
    // test if the payload is equal to "ON" or "OFF"
    if (payload.equals(String(RELAY_ON))) {
      if (mRelayState != true) {
        mRelayState = true;
        setRelayState();
        publishRelayState();
      }
    } else if (payload.equals(String(RELAY_OFF))) {
      if (mRelayState != false) {
        mRelayState = false;
        setRelayState();
        publishRelayState();
      }
    }
  }
}

void loadConfig(){
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqttServer, json["mqtt_server"]);
          strcpy(mqttPort, json["mqtt_port"]);          

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
}

void saveConfig(){
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqttServer;
    json["mqtt_port"] = mqttPort;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
}

void setupMQTT(){  
  client.setServer(mqttServer, atoi(mqttPort));
  client.setCallback(callback);
}

void reconnectMQTT() {
  
  if(WiFi.status() != WL_CONNECTED){
    return;
  }
  
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("INFO: Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("INFO: connected");
      // Once connected, publish an announcement...
      publishRelayState();
      // ... and resubscribe
      client.subscribe(MQTT_RELAY_COMMAND_TOPIC);
    } else {
      Serial.print("ERROR: failed, rc=");
      Serial.print(client.state());
      Serial.println("DEBUG: try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void saveConfigCallback(){
  shouldSaveConfig = true;
}

void setupWifiManager(){  
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&customMqttServer);
  wifiManager.addParameter(&customMqttPort);
}

void connectWifi(){
  
  if(WiFi.status() == WL_CONNECTED){
    return;
  }

  shouldSaveConfig = false;

  if(!wifiManager.autoConnect(MQTT_CLIENT_ID,"123456")) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  } 

  strcpy(mqttServer, customMqttServer.getValue());
  strcpy(mqttPort, customMqttPort.getValue());


  saveConfig();
  
  setupMQTT();
  
  Serial.println("");
  Serial.println("INFO: WiFi connected");
  Serial.print("INFO: IP address: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  // init the serial
  Serial.begin(115200);

  // init the led
  pinMode(RELAY_PIN, OUTPUT);
  analogWriteRange(255);
  setRelayState();

  loadConfig();

  // init the WiFi connection     
  setupWifiManager();
  connectWifi();

  // setup the MQTT connection 
  setupMQTT();
}

void loop() {
  
  connectWifi();
  
  if (!client.connected()) {
    reconnectMQTT();
  }
  
  client.loop();
}
