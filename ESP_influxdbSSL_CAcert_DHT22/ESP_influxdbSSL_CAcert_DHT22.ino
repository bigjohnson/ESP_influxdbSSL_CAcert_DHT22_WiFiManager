#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#Work with version 5 or 6 of ArduinoJson
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#define DEBUG
#define INTERVALLO 300

//if you wont deepsleep between reads
#define DEEPSLEEP

unsigned long int durata;

#include <time.h>
#include <WiFiClientSecure.h>

#include "DHT.h"
#define DHTPIN 4     // what digital pin the DHT22 is conected to
//#define DHTPIN 2     // what digital pin the DHT22 is conected to
#define DHTTYPE DHT22   // there are multiple kinds of DHT sensors
DHT dht(DHTPIN, DHTTYPE);

#define TRIGGER_PIN 5

// Root certificate used by api.github.com.
// Defined in "CACert" tab.
extern const unsigned char caCert[] PROGMEM;
extern const unsigned int caCertLen;
bool first = true;

time_t tempo;

WiFiClientSecure client;

//define your default values here, if there are different values in config.json, they are overwritten.
char influxdb_server[40] = "influxdb.panu.it";
char influxdb_port[6] = "443";
char influxdb_user[34] = "user";
char influxdb_pass[34] = "password";
char influxdb_db[34] = "database";
char measurement[34] = "measurement";
char location[34] = "YOUR_SENSOR_LOCATION";

#include <base64.h>
String auth;

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup() {
#ifdef DEEPSLEEP
  durata = millis();
#endif
  dht.begin();

  // put your setup code here, to run once:
  pinMode(TRIGGER_PIN, INPUT_PULLUP);

#ifdef DEBUG
  Serial.begin(115200);
  Serial.println();
#endif

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
#ifdef DEBUG
  Serial.println("mounting FS...");
#endif
  if (SPIFFS.begin()) {
#ifdef DEBUG
    Serial.println("mounted file system");
#endif
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
#ifdef DEBUG
      Serial.println("reading config file");
#endif
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
#ifdef DEBUG
        Serial.println("opened config file");
#endif
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
#if ARDUINOJSON_VERSION_MAJOR <= 5
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
#else
        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
#endif
#ifdef DEBUG
#if ARDUINOJSON_VERSION_MAJOR <= 5
        json.printTo(Serial);
#else
        serializeJson(json, Serial);
#endif
#endif
#if ARDUINOJSON_VERSION_MAJOR <= 5
        if (json.success()) {
#else
        if ( ! deserializeError )  {
#endif
#ifdef DEBUG
          Serial.println("\nparsed json");
#endif
          strcpy(influxdb_server, json["influxdb_server"]);
          strcpy(influxdb_port, json["influxdb_port"]);
          strcpy(influxdb_user, json["influxdb_user"]);
          strcpy(influxdb_pass, json["influxdb_pass"]);
          strcpy(influxdb_db, json["influxdb_db"]);
          strcpy(measurement, json["measurement"]);
          strcpy(location, json["location"]);

        } else {
#ifdef DEBUG
          Serial.println("failed to load json config");
#endif
        }
        configFile.close();
      }
    }
  } else {
#ifdef DEBUG
    Serial.println("failed to mount FS");
#endif
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_influxdb_server("server", "influxdb server", influxdb_server, 40);
  WiFiManagerParameter custom_influxdb_port("port", "influxdb port", influxdb_port, 6);
  WiFiManagerParameter custom_influxdb_user("user", "influxdb user", influxdb_user, 34);
  WiFiManagerParameter custom_influxdb_pass("password", "influxdb password", influxdb_pass, 34);
  WiFiManagerParameter custom_influxdb_db("database", "influxdb database", influxdb_db, 34);
  WiFiManagerParameter custom_measurement("measurement", "measurement", measurement, 34);
  WiFiManagerParameter custom_location("location", "sensor location", location, 34);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

#ifndef DEBUG
  wifiManager.setDebugOutput(false);
#endif

  //wifiManager.resetSettings();

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&custom_influxdb_server);
  wifiManager.addParameter(&custom_influxdb_port);
  wifiManager.addParameter(&custom_influxdb_user);
  wifiManager.addParameter(&custom_influxdb_pass);
  wifiManager.addParameter(&custom_influxdb_db);
  wifiManager.addParameter(&custom_measurement);
  wifiManager.addParameter(&custom_location);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration

  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    if (!wifiManager.startConfigPortal("OnDemandAP", "password")) {
#ifdef DEBUG
      Serial.println("failed to connect and hit timeout");
#endif
      //delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(5000);
    }
  } else {

    if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
#ifdef DEBUG
      Serial.println("failed to connect and hit timeout");
#endif
      //delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(5000);
    }
  }
  //if you get here you have connected to the WiFi
#ifdef DEBUG
  Serial.println("connected...yeey :)");
#endif
  //read updated parameters
  strcpy(influxdb_server, custom_influxdb_server.getValue());
  strcpy(influxdb_port, custom_influxdb_port.getValue());
  strcpy(influxdb_user, custom_influxdb_user.getValue());
  strcpy(influxdb_pass, custom_influxdb_pass.getValue());
  strcpy(influxdb_db, custom_influxdb_db.getValue());
  strcpy(measurement, custom_measurement.getValue());
  strcpy(location, custom_location.getValue());

  //force save config
  //shouldSaveConfig = true;

  //save the custom parameters to FS
  if (shouldSaveConfig) {
#ifdef DEBUG
    Serial.println("saving config");
#endif
    
#if ARDUINOJSON_VERSION_MAJOR <= 5
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
#else    
    DynamicJsonDocument json(1024);
#endif    
    json["influxdb_server"] = influxdb_server;
    json["influxdb_port"] = influxdb_port;
    json["influxdb_user"] = influxdb_user;
    json["influxdb_pass"] = influxdb_pass;
    json["influxdb_db"] = influxdb_db;
    json["measurement"] = measurement;
    json["location"] = location;

    File configFile = SPIFFS.open("/config.json", "w");
#ifdef DEBUG
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }
#if ARDUINOJSON_VERSION_MAJOR <= 5
    json.printTo(Serial);
#else
    serializeJson(json, Serial);
#endif
    Serial.println("");
#endif
#if ARDUINOJSON_VERSION_MAJOR <= 5
    json.printTo(configFile);
#else
    serializeJson(json, configFile);
#endif
    configFile.close();
    //end save
  }
#ifdef DEBUG
  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  Serial.print("Setting time using SNTP");
#endif

  configTime(0, 0, "it.pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);

#ifdef DEBUG
    Serial.print(".");
#endif

    now = time(nullptr);
  }
#ifdef DEBUG
  Serial.println("");
#endif
  // struct tm timeinfo;
  tempo = now;
  //gmtime_r(&now, &timeinfo);

  // Load root certificate in DER format into WiFiClientSecure object
  bool res = client.setCACert_P(caCert, caCertLen);
  if (!res) {
#ifdef DEBUG
    Serial.println("Failed to load root CA certificate!");
#endif
    while (true) {
      yield();
    }
    return;
  }
  auth = base64::encode(String(influxdb_user) + ":" + String(influxdb_pass));
}

void loop() {
  // Connect to remote server
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  //Serial.println(now);

  if ( now < tempo ) {
    tempo = now;
  } 

  if ( ( now - tempo >= INTERVALLO ) || first ) {
    tempo = now;
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if ( isnan(h) || isnan(t) || h > 100) {
      Serial.println(F("Failed to read from DHT sensor!"));
      delay(1000);
      return;
    }

#ifdef DEBUG
    Serial.print("Current time: ");
    Serial.print(asctime(&timeinfo));;
    Serial.print("Humidity: ");
    Serial.print(h);
    Serial.println("%");
    Serial.print("Temperature: ");
    Serial.print(t);
    Serial.println("C");

    Serial.print("connecting to ");
    Serial.print(influxdb_server);
    Serial.print(":");
    //    Serial.println(httpsPort);
    Serial.println(influxdb_port);

#endif
    String data = (String)measurement + (String)",location=" + (String)location + (String)" temperatura=" + String(t) + (String)",umidita=" + String(h);

    if (!client.connect(influxdb_server,  atoi(influxdb_port))) {

#ifdef DEBUG
      Serial.println("connection failed");
#endif
      first = true;
      return;
    }

    String url = "/write?db=" + (String)influxdb_db;

#ifdef DEBUG
    Serial.print("requesting URL: ");
    Serial.println(url);
    Serial.print("auth: ");
    Serial.println(auth);
    Serial.print("post: ");
    Serial.println(data);
#endif
    client.println("POST " + url + " HTTP/1.1");
    client.print( "Authorization: Basic ");
    client.println(auth);
    client.println("User-Agent: ESP8266/1.0");
    client.println("Host: " + (String)influxdb_server);
    client.println("Connection: close");
    client.println("Content-Type: application/x-www-form-urlencoded;");
    client.print("Content-Length: ");
    client.println(data.length());
    client.println();
    client.println(data);
#ifdef DEBUG
    Serial.println("request sent");
#endif
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") {
#ifdef DEBUG
        Serial.println("headers received");
#endif
        break;
      }
    }
    String line = client.readStringUntil('\n');
    while (client.available()) {
      char c = client.read();
#ifdef DEBUG
      Serial.write(c);
#endif
    }
#ifdef DEBUG
    Serial.println("reply was:");
    Serial.println("==========");
    Serial.println(line);
    Serial.println("==========");
    Serial.println();
#endif
    if (first) {
      first = false;
    }
//    tempo = now;

    unsigned long int adesso = millis();
    #ifdef DEEPSLEEP
    if (durata > adesso) {
      durata = adesso;
    }
    if ( (adesso - durata) < (INTERVALLO * 1000)) {
      unsigned long int attesa = ((INTERVALLO * 1000000)-(adesso - durata) * 1000);
#ifdef DEBUG
      Serial.print("Going into deep sleep for ");
      Serial.print(attesa / 1000000);
      Serial.println(" seconds");
#endif
      ESP.deepSleep(attesa);
    } else {
      durata = millis();
    }
#endif
#ifdef DEBUG
#ifndef DEEPSLEEP
  Serial.print("Waiting ");
  Serial.print(INTERVALLO - ((adesso - durata)/1000));
  Serial.println(" seconds");
  #endif
#endif
  }
}
