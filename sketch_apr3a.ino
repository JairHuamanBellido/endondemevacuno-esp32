#include "secrets.h"
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"
#include "time.h"

#define AWS_IOT_PUBLISH_TOPIC "esp32/lambda"
#define AWS_IOT_SUBSCRIBE_TOPIC "esp32/sub"
//define sound speed in cm/uS
#define SOUND_SPEED 0.034
#define CM_TO_INCH 0.393701
#define VACCINE_CENTER_ID "834d8d53-45cf-419f-aa32-a66286eb2ef4"
#define IS_OUTPUT true
// 5 minuts
#define SENDING_INTERVAL_M 1
#define iterations 5 //Number of readings in the calibration stage

const int trigPin = 23;
const int echoPin = 22;
const int ledPin = 25;

int lastShipping = -1;
int people = 0;
bool prev_inblocked = false;

const char *NTPServer = "pool.ntp.org";
const long GMTOffset_sec = -18000;   //Replace with your GMT offset (seconds)
const int DayLightOffset_sec = 0;  //Replace with your daylight offset (seconds)

float calibrate_out;
float max_distance;

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(256);

void connectAWS() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  // Configure WiFiClientSecure to use the AWS IoT device credentials
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  // Connect to the MQTT broker on the AWS endpoint we defined earlier
  client.begin(AWS_IOT_ENDPOINT, 8883, net);

  // Create a message handler
  client.onMessage(messageHandler);

  Serial.print("Connecting to AWS IOT");

  while (!client.connect(THINGNAME)) {
    Serial.print(".");
    delay(100);
  }

  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    return;
  }

  // Subscribe to a topic
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);

  Serial.println("AWS IoT Connected!");
}

void publishMessage() {
  Serial.println(lastShipping);
  StaticJsonDocument<200> doc;
  doc["vaccine_center_id"] = VACCINE_CENTER_ID;
  const int toCalibrate = (IS_OUTPUT)? -1 : 1;
  doc["data"] = people * toCalibrate;

  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);  // print to client

  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
  Serial.println("Mensaje enviado!");
  people=0;
}

void messageHandler(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);

  //  StaticJsonDocument<200> doc;
  //  deserializeJson(doc, payload);
  //  const char* message = doc["message"];
}



long duration;
float distanceCm;
float distanceInch;

bool isTimeToSend() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return false;
  }
  if((timeinfo.tm_min)%SENDING_INTERVAL_M==0 && lastShipping!=timeinfo.tm_min) {
    lastShipping=timeinfo.tm_min;
    return true;
  }
  return false;
}

void caliper(){
  Serial.println("Calibrating...");
  delay(1500);
  for (int a = 0; a < iterations; a++) {
    getObjectDistance();
    calibrate_out += distanceCm;
    delay(200);
  }
  max_distance = 1.25 * calibrate_out / iterations;
  calibrate_out = 0.75 * calibrate_out / iterations;
  Serial.print("max_distance:");
  Serial.println(max_distance);
  Serial.print("calibrate_out:");
  Serial.println(calibrate_out);
}

void setup() {
  Serial.begin(115200);      // Starts the serial communication
  pinMode(trigPin, OUTPUT);  // Sets the trigPin as an Output
  pinMode(echoPin, INPUT);   // Sets the echoPin as an Input
  pinMode(ledPin, OUTPUT);
  connectAWS();
  configTime(GMTOffset_sec, DayLightOffset_sec, NTPServer);
  isTimeToSend();
  caliper();
}

void getObjectDistance(){
   // Clears the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(echoPin, HIGH);
  // Calculate the distance
  distanceCm = duration * SOUND_SPEED / 2;
  // Convert to inches
  distanceInch = distanceCm * CM_TO_INCH;
}

void showDistanceValues(){
  // Prints the distance in the Serial Monitor
  Serial.print("Distance (cm): ");
  Serial.println(distanceCm);
  Serial.print("Distance (inch): ");
  Serial.println(distanceInch);
}

void loop() {
  if (isTimeToSend())
    publishMessage();

  getObjectDistance();
  //showDistanceValues();

  if (distanceCm < calibrate_out ) {
    digitalWrite(ledPin, HIGH);
    showDistanceValues();
    if(!prev_inblocked){
      prev_inblocked = true;
      people++;
      Serial.print("People: ");
      Serial.println(people);
    }
  } else {
    if(max_distance < distanceCm){
      showDistanceValues();
    }
    prev_inblocked = false;
    digitalWrite(ledPin, LOW);
  }
  client.loop();
  delay(200);
}
