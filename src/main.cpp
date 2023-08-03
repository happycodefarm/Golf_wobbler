#include <Arduino.h>
#include <climits>
#include <EEPROM.h>
#include <FastLED.h>
#include "AiEsp32RotaryEncoder.h"
#include <SimpleKalmanFilter.h>

// OTA
#include "OverTheAir.h"
#include <esp_wifi.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

// server
#include "SPIFFS.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
AsyncWebServer server(80);

#include <AsyncJson.h>
#include <ArduinoJson.h>
StaticJsonDocument<20000> json;  

//#define SETUP // uncomment to reset setup default

// hall sensors
#define ENC_A 25
#define ENC_B 26

float speed = 0;
long position = 0;
long lastPostion = 0;

AiEsp32RotaryEncoder ballEncoder = AiEsp32RotaryEncoder(ENC_A, ENC_B);
SimpleKalmanFilter simpleKalmanFilter(2, 2, 0.01);


#define RETRY_BUTTON 14
#define RETRY_GND 27

bool debug = false;

#define MAX_LEDS 1500
int pointCount = 10;

float terrainSlopeAngles[MAX_LEDS/10] = {0.0};
float terrainLevels[MAX_LEDS/10] = {0.0};

// leds
#define LEDSTRIP_DATA     13
//#define LEDSTRIP_GND     26
#define COLOR_ORDER GRB
#define CHIPSET     WS2811
#define BUTTON  0
#define BRIGHTNESS  255
#define PIEZO 4
CRGB leds[MAX_LEDS];

// game
float terrainScale = 200.0f;
float sensorScale = 0.6f;
float ballMass = 1.0f; // Kg // not used
float terrainFriction = 0.1f; // Coefficient of Friction (mu)
float worldGravity = 9.98f;
float ballInHoleVelocity = 25.0f;
float ballVelocity = 0.0f; // m/s
float ballAcceleration = 0.0; // m/s^2
float maxBallVelocity = 800.0;
unsigned int hyterisisLimit = 50;
unsigned long hysterisisCounter = 0;
bool lastBallVelocityDirection = false;
int lastPositionIndex = 0;
volatile unsigned long lastPlay = 0;

unsigned int numberOfTries = 7;

volatile bool retry = false;

float ballPosition = 0.0f;
int ballPositionIndex = 0;
int holeLedIndex = 0;

unsigned int holes[MAX_LEDS/10] = {0};
int holeCounter = 0;
int holesCount = 3;

unsigned int waters[MAX_LEDS/10] = {0};
int watersCount = 3;


volatile bool playing = false; // playing state

unsigned int hitCounter = 0; // hits counter
unsigned int missCounter = 0; // miss counter

volatile bool needReadData = false;


void IRAM_ATTR readEncoderISR(){
	ballEncoder.readEncoder_ISR();
 
}



float getAngle(float x1, float y1, float x2, float y2) {

  // returns the angle between edge and a vector
  //        90°   
  // 135°    |   45°
  //      \  |  /
  //       \ | /
  //        \|/
  // 180°--x1-y1--0°
  //        /|\
  //       / | \
  //      /  |  \
  // 225°    |   315°
  //        270°

  float dx, dy, angle;
  dx = x2 - x1;
  dy = y2 - y1;
  
  angle = atan2(dy, dx) * 180.0 / PI;
  while(angle >= 360.0) angle -= 360.0;
  while(angle < 0.0) angle += 360.0;

  return angle;
}

// fade all leds to black
void fadeall() { for(int i = 0; i < pointCount*10; i++) { leds[i].nscale8(250); } }

void showWinAnimation() {
  Serial.println("win");
  for (int x = 0; x < 100; x++) {
    for(int i = 0; i < pointCount*10; i++) {
        ((i+x)%6>3) ?  leds[(pointCount*10)-i] = CRGB::Yellow : leds[(pointCount*10)-i] = CRGB::Black;
    }  
    FastLED.show();
  }
}


void showStartAnimation() {
  unsigned char hue = 0;

  leds[holeLedIndex-1] = CRGB(255,20,0);
  leds[holeLedIndex] = CRGB::Green;
  leds[holeLedIndex+1] = CRGB(255,20,0);

  for(int i = 0; i<pointCount ; i+=3) {
    leds[i] = CHSV(hue++, 255, 255);
    leds[i-1] = CHSV(hue++, 255, 255);
    leds[i-2] = CHSV(hue++, 255, 255);
    fadeall();
    FastLED.show(); 
  }
}

void showNextHoleAnimation() {
  unsigned char hue = 0;

  leds[holeLedIndex-1] = CRGB(255,20,0);
  leds[holeLedIndex] = CRGB::Red;
  leds[holeLedIndex+1] = CRGB(255,20,0);

  // for (int point = 0; point < pointCount; point++) {
  //   leds[point*10] = CHSV(terrainLevels[point]*2.55, 255, 255); // editor terrain

  //   for (int i = 0; i< 10; i++) {
  //     float steps = (terrainLevels[point+1] - terrainLevels[point]) / 10.0;
  //     float interpolated = terrainLevels[point] + (steps*i);
  //     leds[(point*10)+i] = CHSV(interpolated*2.55, 255, 255); // editor terrain
  //   }
  // }

  for(int i = holes[holeCounter-1]; i < holes[holeCounter]; i+=3) {
    leds[i] = CHSV(hue++, 255, 255);
    leds[i-1] = CHSV(hue++, 255, 255);
    leds[i-2] = CHSV(hue++, 255, 255);
    fadeall();
    FastLED.show(); 
  }
}

void showLooseAnimation() {

  for (int x = 0; x < 50; x++) {
    for(int i = 0; i < (pointCount*10); i++) {
        ((i+x)%6>3) ?  leds[i] = CRGB::Red : leds[i] = CRGB::Black;
    }
    FastLED.show();
  }
}

void showMissedAnimation() {

  if (missCounter >= numberOfTries) return;

  unsigned char hue = 0;

  for(int i = 4; i < holes[holeCounter]; i+=5) {
    leds[i] = CHSV(hue++, 255, 255);
    leds[i-1] = CHSV(hue++, 255, 255);
    leds[i-2] = CHSV(hue++, 255, 255);
    leds[i-3] = CHSV(hue++, 255, 255);
    leds[i-4] = CHSV(hue++, 255, 255);
    fadeall();
    FastLED.show(); 
  }
}

void resetGame() {
  // ESP.restart();
  playing = false;
  ballPosition = 0;
  ballPositionIndex = 0;
  hysterisisCounter = 0;
  holeCounter = 0;
  holeLedIndex = holes[holeCounter];
  hitCounter = 0;
  missCounter = 0;
  ballVelocity = 0; 

  // sampling = false;
  lastPlay = millis();
  retry = false;
  Serial.println("game reset");
}

void replayHole() {
  playing = false;
  ballPosition = 0;
  ballPositionIndex = 0;
  hysterisisCounter = 0;
  ballVelocity = 0; 

  missCounter ++;

  Serial.println("missed !");
}

void nextHole() {
  holeCounter++;
  playing = false;
  ballVelocity = 0; 
  // ballPosition = 0;
  // ballPositionIndex = 0;
  hysterisisCounter = 0;

  Serial.println("hole !");

  if (holeCounter >= holesCount) { // last hole == win !!
    showWinAnimation();
    ESP.restart();
  } else {
    holeLedIndex = holes[holeCounter];// + random(-20,20);
    showNextHoleAnimation();
  }
  // sampling = false;
}

void draw() {
   digitalWrite(LED_BUILTIN, debug);
   bool blink = (millis() % 1000) < 500;

    if (debug) {
      int led = 0;
      for (int point = 0; point < pointCount; point++) {
        leds[point*10] = CHSV(terrainLevels[point]*2.55, 255, 255); // editor terrain

        for (int i = 0; i< 10; i++) {
          float steps = (terrainLevels[point+1] - terrainLevels[point]) / 10.0;
          float interpolated = terrainLevels[point] + (steps*i);
          leds[(point*10)+i] = CHSV(interpolated*2.55, 255, 255); // editor terrain
        }
      }

      for (int i = 0; i<holesCount; i++) {
        leds[holes[i]-1] = blink ? CRGB(255,20,0) : CRGB::Black;
        leds[holes[i]] = blink ? CRGB::Red : CRGB::Black;
        leds[holes[i]+1] = blink ? CRGB(255,20,0) : CRGB::Black;
      }
  
      //leds[led] = CHSV((100.0 - terrainLevels[led])*2.55, 255, 255); // editor terrain
    } else {

       for (int led = 0; led < pointCount * 10; led++) {
          leds[led] = CRGB(0,5,0); // green terrain
       }

      // water
      // for (int i = 0; i<watersCount; i++) {
      //   for (uint8_t w = 0; w<10; w++) {
      //     leds[waters[i]] = CRGB(0,0,255);
      //   }
      //   // leds[waters[i]-1] = CRGB(0,0,255);
      //   // leds[waters[i]] = CRGB(0,0,255);
      //   // leds[waters[i]+1] = CRGB(0,0,255);
      // }
    }

  // hole
  if (playing) {
    leds[holeLedIndex-1] = CRGB(255,20,0);
    leds[holeLedIndex] = CRGB::Red;
    leds[holeLedIndex+1] = CRGB(255,20,0);
  } else if (blink) {
    leds[holeLedIndex-1] = CRGB(255,20,0);
    leds[holeLedIndex] = CRGB::Red;
    leds[holeLedIndex+1] = CRGB(255,20,0);
  }
 

  // ball
  leds[ballPositionIndex] =  playing ? CRGB::White : (blink ? CRGB::White : CRGB(0,5,0));
  FastLED.show();
}


void play() {
  if (ballEncoder.encoderChanged()) {
     lastPlay = millis();
     playing = true;
  }
  position = ballEncoder.readEncoder();
  speed += (position - lastPostion);
  lastPostion = position;

  if (speed > 0.1 || speed < -0.1 ) speed = speed - (speed*0.5);

  //float smoothedSpeed = simpleKalmanFilter.updateEstimate(speed);
  //float realBallVelocity = speed * sensorScale;

  ballVelocity += speed * sensorScale ;
 

  //Serial.print(">sensor:");
  // Serial.println(speed * sensorScale);
  // Serial.print(">velocity:");
  // Serial.println(ballVelocity);
  // Serial.print(">position:");
  // Serial.println(ballPositionIndex);

  // Newton's 2nd law of motion
  float nReaction = (ballMass*worldGravity*sin(terrainSlopeAngles[ballPositionIndex/10]*PI/180.0));
  float nFriction = (ballMass*terrainFriction*worldGravity*cos(terrainSlopeAngles[ballPositionIndex/10]*PI/180.0));

  // acceleration
  lastBallVelocityDirection = ballVelocity >=0;
  ballVelocity -= nReaction;

  // friction 
  if (ballVelocity > 0.0f) ballVelocity -= nFriction;
  else if (ballVelocity < 0.0f ) ballVelocity += nFriction;

  // hysterisis check
  if (playing && lastBallVelocityDirection != (ballVelocity >= 0)){ 
    hysterisisCounter ++;
    ballVelocity /=10.0;
  }
 
  // if (hysterisisCounter > hyterisisLimit) {
  //   //Serial.println("hysterisis");
  //   ballVelocity = 0.0;
  //   // speed = 0.0;
  // }

  ballPosition += ballVelocity; // update ball position

  if (ballPosition < 0 && ballVelocity < 0.0f) { // bounce
    ballVelocity = -ballVelocity * 0.02;
    ballPosition = 0;
    ballPositionIndex = 0;
  }
  // position to led index with congruance
  ballPositionIndex = int(floor(ballPosition / terrainScale)) % (pointCount*10);

  // ball over hole velocity reduction
  if (abs(ballPositionIndex - holeLedIndex) < 2) {
    ballVelocity *= 0.5;
  }

  // playing = (abs(ballVelocity > 0.1));

  // ball in hole check
  if (abs(ballPositionIndex - holeLedIndex) < 5 && abs(ballVelocity) < ballInHoleVelocity) {
    ballPositionIndex = holeLedIndex;
    draw();
    nextHole();
    return;
  }
}


bool readData() {
  File dataFile = SPIFFS.open("/data.json", "r");
  if (!dataFile) {
    Serial.println("Failed to open config file");
    return false;
  }

  Serial.print("deserializeJson... ");
  DeserializationError error = deserializeJson(json, dataFile);
  Serial.println("done");

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return false;
  }

  JsonArray levelsArray = json["levels"];
  for (int i = 0; i < levelsArray.size(); i++) {
    float f = levelsArray[i];
    terrainLevels[i] = levelsArray[i];
  }

  for (int i = 0; i < levelsArray.size(); i++) {
    float angle = getAngle(i*5.0, terrainLevels[i], (i+1)*5.0, terrainLevels[i+1]);
    terrainSlopeAngles[i] = angle;
    Serial.print(angle);
    Serial.print(", ");
  }     
  
  JsonArray holesArray = json["holes"];
  for (int i = 0; i < holesArray.size(); i++) {
    int index = holesArray[i];
    holes[i] = index*10;
  }
  holesCount =  holesArray.size();


  // JsonArray watersArray = json["waters"];
  // for (int i = 0; i < watersArray.size(); i++) {
  //   int index = watersArray[i];
  //   waters[i] = index*10;
  // }
  // watersCount =  watersArray.size();

  terrainScale = json["terrain_scale"];
  sensorScale = json["sensor_scale"];
  ballMass = json["ball_mass"];
  terrainFriction  = json["terrain_friction"];
  worldGravity = json["world_gravity"];
  ballInHoleVelocity = json["ball_in_hole_velocity"];
  hyterisisLimit  = json["hysterisis_limit"] | 200;
  numberOfTries = json["number_of_tries"] | 7;

  pointCount = levelsArray.size();

  Serial.printf("point count is = %i\n", pointCount);
  dataFile.close();
  resetGame();
  return true;
}

// handles uploads
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
  Serial.println(logmessage);

  if (!index) {
    logmessage = "Upload Start: " + String(filename);
    // open the file on first call and store the file handle in the request object
    request->_tempFile = SPIFFS.open("/" + filename, "w");
    Serial.println(logmessage);
  }

  if (len) {
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);
    logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
    Serial.println(logmessage);
  }

  if (final) {
    logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
    // close the file handle as the upload is now done
    request->_tempFile.close();
    Serial.println(logmessage);
    request->redirect("/");
  }
}

// handle json post
void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
  if(!index){
    Serial.printf("BodyStart: %u B\n", total);
     request->_tempFile = SPIFFS.open("/data.json", "w");
  }

  request->_tempFile.write(data, len);

  if(index + len == total){
    Serial.printf("BodyEnd: %u B\n", total);
    request->_tempFile.close();
    needReadData = true;
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  readData();
  
  initOTA(OTA_SSID, OTA_PASSWORD);

  pinMode(LED_BUILTIN, OUTPUT);
  
  pinMode(RETRY_GND, LOW);
  pinMode(RETRY_BUTTON, INPUT_PULLUP);

  // for (int i = 0; i<10; i++)   {
  //   digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  //   delay(100);
  // }

  // delay(1000);
  
  holeLedIndex = holes[holeCounter];//+ random(-20,20);

  FastLED.addLeds<CHIPSET, LEDSTRIP_DATA, COLOR_ORDER>(leds, MAX_LEDS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness( BRIGHTNESS );

  // encoder

  ballEncoder.areEncoderPinsPulldownforEsp32 = false;
  ballEncoder.begin();
  ballEncoder.setup(readEncoderISR);
  // ballEncoder.disableAcceleration();
  ballEncoder.setAcceleration(200);

  /// server
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", String(), false);
  });

 server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/data.json", String(), false );
 });

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request){
      Serial.print("sending levels...   ");
      request->send(SPIFFS, "/data.json", String(), true );
      Serial.println("done.");
 });

 // run handleUpload function when any file is uploaded
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200);
      }, handleUpload);
  
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200);
      },NULL, handleBody);


  server.on("/debug", HTTP_GET, [] (AsyncWebServerRequest *request) {
    debug = !debug;
    request->send(200, "text/plain", "OK");
  });
  
  server.begin();
  Serial.println("HTTP server started");

  // showStartAnimation();
  draw();

}

void loop() {
  if (needReadData) {
    readData();
    needReadData = false;
  }
  ArduinoOTA.handle();
  
  if(!digitalRead(RETRY_BUTTON)){ // debug button
    while(!digitalRead(RETRY_BUTTON)) {
      delay(1);
    };

    showLooseAnimation();
    ESP.restart();

    // resetGame();
    // showLooseAnimation();

    // showMissedAnimation();
    // replayHole(); 
   
  }

  // if (millis() - lastPlay > 120000 ) { // 2 min reset.
  //   // resetGame();
  //   //showLooseAnimation();
  // }

  play();
  draw();
}
