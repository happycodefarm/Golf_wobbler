#include <Arduino.h>
#include "TM1637Display.h"
#include <climits>
#include <EEPROM.h>
#include "elapsedMillis/elapsedMillis.h"
#include <FastLED.h>
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
#define hall_A 5
#define hall_B 4
#define max_samples 10

#define RETRY_BUTTON 14
#define RETRY_GND 27

volatile unsigned long ts_A = 0;
volatile unsigned long ts_B = 0;
volatile  long long diff = LLONG_MAX;
volatile bool done = false;
volatile bool sampling = false;
volatile  bool canSample = true;
float speedPeak;
bool debug = false;

#define MAX_LEDS 1000
int pointCount = 10;

float terrainSlopeAngles[MAX_LEDS/10] = {0.0};
float terrainLevels[MAX_LEDS/10] = {0.0};

#define SAMPLING_TIME 4000000 // 4 seconds

// 7 segment display
#define CLK 26
#define DIO 25

// Animation Data - HGFEDCBA Map
const uint8_t ANIMATION[5][4] = {
  { 0x08, 0x08, 0x08, 0x5c },  // Frame 0
  { 0x08, 0x08, 0x5c, 0x08 },  // Frame 1
  { 0x08, 0x5c, 0x08, 0x08 },  // Frame 2
  { 0x5c, 0x08, 0x08, 0x08 },  // Frame 3
  { 0x08, 0x08, 0x08, 0x08 }   // Frame 4
};
const uint8_t NONE[] = {0x0, 0x0, 0x0, 0x0};
const uint8_t PLAY[] = {B01110011, B00111000, B01011111, B01101110};
const uint8_t HIT[] = {0x76, 0x30, 0x78, 0x00};
const uint8_t GOOD[] = {0x3d, 0x3f, 0x3f, 0x5e};
const uint8_t BAD[] =  {0x00, 0x7c, 0x5f, 0x5e};
const uint8_t LOST[] = {0x38, 0x3f, 0x6d, 0x78};
const uint8_t HOLE[] = {0x76, 0x3f, 0x38, 0x79};
const uint8_t GOLF[] = {0x3d, 0x3f, 0x38, 0x71};
TM1637Display display(CLK, DIO);


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
float peak = 0.0;
unsigned long lastPlay = 0;

volatile bool retry = false;

float ballPosition = 0.0f;
int ballPositionIndex = 0;
int holeLedIndex = 0;

int holes[] = {100, 380,550};
int holeCounter = 0;
int maxHoles = 3;

volatile bool playing = false; // playing state
int hits = 0; // hits counter

elapsedMillis immobilityTimer;
int timeInterval = 2; // game timer
volatile bool needReadData = false;

// utils
//  void interpolateLevels(float x1, float y1, float x2, float y2, int nPoints) {
//   // get equation y = ax + b
//   // let  points=[]:
  
//   float a = (y2 - y1) / (x2 - x1);
//   let float = y2 - a * x2;
//   float step = abs(x1 - x2) / nPoints;
//   for (int x = min(x1, x2); x < max(x1,x2); x += step) {
//       float y = a*x+b;
//       points.push(y)
//   }
//   points.push(y2)
//   return points
// }


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
  // angle = angle.0;//%360;
  while(angle >= 360.0) angle -= 360.0;
  while(angle < 0.0) angle += 360.0;

  return angle;
}

// fade all leds to black
void fadeall() { for(int i = 0; i < pointCount*10; i++) { leds[i].nscale8(250); } }

void showWin() {
  Serial.println("win");
  display.setSegments(GOOD);
  for (int x = 0; x < 250; x++) {
    for(int i = 0; i < pointCount*10; i++) {
        ((i+x)%6>3) ?  leds[(pointCount*10)-i] = CRGB::Yellow : leds[(pointCount*10)-i] = CRGB::Black;
    }
    unsigned char state = (x/10)%3;
    if (state==0) display.setSegments(HIT);
    else if (state==1) display.showNumberDec(hits,false);
    else display.setSegments(GOOD); 
    FastLED.show();
  }
}

void showSucess() {
  unsigned char hue = 0;

  leds[holeLedIndex-1] = CRGB(255,20,0);
  leds[holeLedIndex] = CRGB::Red;
  leds[holeLedIndex+1] = CRGB(255,20,0);

  display.setSegments(GOOD);
  for(int i = holes[holeCounter-1]; i < holes[holeCounter]; i+=3) {
    leds[i] = CHSV(hue++, 255, 255);
    leds[i-1] = CHSV(hue++, 255, 255);
    leds[i-2] = CHSV(hue++, 255, 255);
    fadeall();
    FastLED.show(); 
  }
}

void showLoose() {
  display.setSegments(LOST);

  for (int x = 0; x < 250; x++) {
    for(int i = 0; i < (pointCount*10); i++) {
        ((i+x)%6>3) ?  leds[i] = CRGB::Red : leds[i] = CRGB::Black;
    }
    FastLED.show();
  }
}

void showMissed() {
  display.setSegments(BAD);

  if (hits >= 7) return;

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
  playing = false;
  ballPosition = 0;
  ballPositionIndex = 0;
  hysterisisCounter = 0;
  holeCounter = 0;
  holeLedIndex = holes[holeCounter];
  hits = 0;
  //diff = ULLONG_MAX;
  speedPeak = 0;
  ballVelocity = 0; 

  sampling = false;
  canSample = true;
  lastPlay = millis();
  retry = false;
  Serial.println("game reset");
}

void replayHole() {
  immobilityTimer = 0;
  playing = false;
  ballPosition = 0;
  ballPositionIndex = 0;
  hysterisisCounter = 0;
  //diff = ULLONG_MAX;
  speedPeak = 0;
  ballVelocity = 0; 

  Serial.println("missed !");

  if (hits == 7) { // restart game
      showLoose();
      resetGame();
  }
  sampling = false;
  canSample = true;
}

void nextHole() {
  holeCounter++;
  playing = false;
  //diff = ULLONG_MAX;
  speedPeak = 0;
  ballVelocity = 0; 
  ballPosition = 0;
  ballPositionIndex = 0;
  hysterisisCounter = 0;

  Serial.println("hole !");

  if (holeCounter >= maxHoles) { // win !!
    showWin();
    resetGame();
  } else { // 
    if (hits == 7) { // restart game
      showLoose();
      resetGame();
    } else {
      holeLedIndex = holes[holeCounter];// + random(-20,20);
      showSucess();
    }
  }
  sampling = false;
  canSample = true;
}

void draw() {
  
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
      //leds[led] = CHSV((100.0 - terrainLevels[led])*2.55, 255, 255); // editor terrain
    } else {
       for (int led = 0; led < pointCount * 10; led++) {
          leds[led] = CRGB(0,5,0); // green terrain
       }
    }

  // hole
  if (!playing) {
    if (millis()%500 < 250) {
      leds[holeLedIndex-1] = CRGB(255,20,0);
      leds[holeLedIndex] = CRGB::Red;
      leds[holeLedIndex+1] = CRGB(255,20,0);
    }
  } else {
    leds[holeLedIndex-1] = CRGB(255,20,0);
    leds[holeLedIndex] = CRGB::Red;
    leds[holeLedIndex+1] = CRGB(255,20,0);
  }

  // ball
  leds[ballPositionIndex] = CRGB::White;

  if (playing) { // show ball animation
    display.setSegments(ANIMATION[(ballPositionIndex) % 5]);
  } else { // print game info
      if (hits == 0) { // print "play golf"
        if (millis()%2000<1000) display.setSegments(PLAY);
        else display.setSegments(GOLF);
      } else { // print hits count
      int ticks = millis()%3000;
      if (ticks<1000) display.setSegments(PLAY);
      else if (ticks<1500) display.setSegments(NONE);
      else if (ticks<2000) display.setSegments(HIT);
      else display.showNumberDec(hits,false);
    }
  }
  FastLED.show();
}

void IRAM_ATTR ISR1() {
  digitalWrite(LED_BUILTIN, HIGH);
  ts_A = micros();
  done = true;  
}

void IRAM_ATTR ISR2() {
  if (done) {
    ts_B = micros();
    diff = ts_B - ts_A;
    if (diff < 0) diff = -diff;
    done = false;
  }
  if (canSample) sampling = true;
  digitalWrite(LED_BUILTIN, LOW);
}



void play() {

  if (sampling && canSample) {
    float speed =  10000.0f / float(diff) * 1000.0f;
    playing = true;
    immobilityTimer = 0;

    if  (speed < speedPeak || micros() - ts_B >  SAMPLING_TIME) { // slowing down or timeout : stop sampling
      ballVelocity = speedPeak * sensorScale;
      Serial.printf("Sampling Done\nvelocity = %f, speed = %f, peak = %f\n",ballVelocity,  speed, speedPeak);

      speedPeak = 0;
      lastPlay = millis();

      sampling = false;
      canSample = false;
      
      hits ++;
    } else {
      speedPeak = speed;
      ballVelocity = speedPeak * sensorScale;
    }
  }
  
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
 
  if (hysterisisCounter > hyterisisLimit) {
    //Serial.println("hysterisis");
    ballVelocity = 0;
  }

  ballPosition += ballVelocity; // update ball position

  // position to led index with congruance
  ballPositionIndex = int(floor(ballPosition / terrainScale)) % (pointCount*10);

  // immobility check
  if (lastPositionIndex != ballPositionIndex) immobilityTimer = 0; 
  lastPositionIndex = ballPositionIndex;

  // ball over hole velocity reduction
  if (abs(ballPositionIndex - holeLedIndex) < 2) {
    ballVelocity *= 0.5;
    //Serial.println("slowing down");
  }
  // ball in hole chek
  if (abs(ballPositionIndex - holeLedIndex) < 5 && abs(ballVelocity) < ballInHoleVelocity) {
    ballPositionIndex = holeLedIndex;
    draw();
    nextHole();
    return;
  
  } else if (ballPositionIndex <= 0 && ballVelocity < 0.0f) { // bounce
    ballVelocity = -ballVelocity * 0.1;
    ballPosition = 0;
    ballPositionIndex = 0;
  }

  if (playing && immobilityTimer > 2000) {
    if (abs(ballPositionIndex - holeLedIndex) < 5 && abs(ballVelocity) < ballInHoleVelocity) {
      ballPositionIndex = holeLedIndex;
      draw();
      nextHole();
      return;
    } else {
      showMissed();
      replayHole();
      return;
    }
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
  

  terrainScale = json["terrain_scale"];
  sensorScale = json["sensor_scale"];
  ballMass = json["ball_mass"];
  terrainFriction  = json["terrain_friction"];
  worldGravity = json["world_gravity"];
  ballInHoleVelocity = json["ball_in_hole_velocity"];
  hyterisisLimit  = json["hysterisis_limit"];

  pointCount = levelsArray.size();

  Serial.printf("point count is = %i\n", pointCount);
  dataFile.close();
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
  // Initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  readData();
  
  initOTA(OTA_SSID, OTA_PASSWORD);

  pinMode(hall_A, INPUT);
  pinMode(hall_B, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  
  pinMode(RETRY_GND, LOW);
  pinMode(RETRY_BUTTON, INPUT_PULLUP);

  for (int i = 0; i<10; i++)   {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(100);
  }

  attachInterrupt(hall_A, ISR1, FALLING);
  attachInterrupt(hall_B, ISR2, FALLING);
  
  Serial.begin(115200);
  display.setBrightness(0x0a);
  delay(1000);
  speedPeak = 0;
  
  holeLedIndex = holes[holeCounter];//+ random(-20,20);

  FastLED.addLeds<CHIPSET, LEDSTRIP_DATA, COLOR_ORDER>(leds, MAX_LEDS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness( BRIGHTNESS );
  display.setSegments(GOLF);

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

  draw();
  delay(2000);
}

void loop() {
  if (needReadData) {
    readData();
    needReadData = false;
  }
  ArduinoOTA.handle();
  
  digitalWrite(LED_BUILTIN,canSample);

  if(!digitalRead(RETRY_BUTTON)){ // debug button
    // playing = false;
    // ballPosition = 0;
    // ballPositionIndex = 0;
    while(!digitalRead(RETRY_BUTTON)) {
      delay(1);
    };
    
    Serial.println("retry");
     if (hits>=7) {
      showLoose();
      resetGame();
    } else {
      showMissed();
      replayHole(); 
    }
  }

  if (millis() - lastPlay > 120000 ) { // 2 min reset.
    resetGame();
  }

  play();
  draw();
}
