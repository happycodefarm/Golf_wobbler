#include <Arduino.h>
#include "TM1637Display.h"
#include <climits>
#include <EEPROM.h>
#include "elapsedMillis/elapsedMillis.h"
#include <FastLED.h>
#include "slopes.h"
#include <Preferences.h>


// OTA
#include "OverTheAir.h"
#include <esp_wifi.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

// server
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "index.h"
const char* PARAM_INPUT = "value";

AsyncWebServer server(80);


//#define SETUP // uncomment to reset setup default

// #define DEBUG

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
CRGB leds[NUM_LEDS];


// game
Preferences prefs;

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

// fade all leds to black
void fadeall() { for(int i = 0; i < NUM_LEDS; i++) { leds[i].nscale8(250); } }

void showWin() {
  Serial.println("win");
  display.setSegments(GOOD);
  for (int x = 0; x < 250; x++) {
    for(int i = 0; i < NUM_LEDS; i++) {
        ((i+x)%6>3) ?  leds[NUM_LEDS-i] = CRGB::Yellow : leds[NUM_LEDS-i] = CRGB::Black;
        //(millis()/20 + i) % 20 < 10 ?  leds[i] = CRGB::Yellow : leds[i] = CRGB::Black;
        // unsigned char state = (millis()/1000)%3;
        // if (state==0) display.setSegments(HIT);
        // else if (state==1) display.showNumberDec(hits,false);
        // else display.setSegments(GOOD); 
    }
    //delay(1000);
    unsigned char state = (x/10)%3;
    if (state==0) display.setSegments(HIT);
    else if (state==1) display.showNumberDec(hits,false);
    else display.setSegments(GOOD); 
    FastLED.show();
  }
  // for(int i = 0; i < NUM_LEDS; i++) {
  //   (millis()/20 + i) % 20 < 10 ?  leds[i] = CRGB::Yellow : leds[i] = CRGB::Black;
  //   if (i%3==0) FastLED.show();
  //   unsigned char state = (millis()/1000)%3;

  //   if (state==0) display.setSegments(HIT);
  //   else if (state==1) display.showNumberDec(hits,false);
  //   else display.setSegments(GOOD);
  // }
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
  // for(int i = 0; i < NUM_LEDS; i++) {
  //     (millis()/20 + i) % 20 < 10 ?  leds[i] = CRGB::Red : leds[i] = CRGB::Black;
      
  //     if (millis()%2000<1000) display.setSegments(NONE);
  //     else display.showNumberDec(hits,false);
  // }
  // FastLED.show();

  for (int x = 0; x < 250; x++) {
    for(int i = 0; i < NUM_LEDS; i++) {
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
  for (int led = 0; led < NUM_LEDS; led++) { // green terrain
    if (debug)
        leds[led] =  led % 10 == 0 ? (led % 100 == 0 ? CRGB(5,0,0) : CRGB(0,0,5)) : CRGB(0,5,0);
    else
        leds[led] = CRGB(0,5,0);
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

// void IRAM_ATTR ISR3() {
//    Serial.println("retry");
//   retry = true;  
// }



void printPrefs() {
  Serial.printf("Setting\n\n----------\n(t)errain scale : %f \n", prefs.getFloat("t_scale"));
  Serial.printf("sensor (s)cale : %f \n", prefs.getFloat("g_scale"));
  Serial.printf("ball (m)ass : %f \n", prefs.getFloat("b_mass"));
  Serial.printf("ball in hole (v)elocity : %f \n", prefs.getFloat("b_hole"));
  Serial.printf("terrain (f)riction : %f \n", prefs.getFloat("t_friction"));
  Serial.printf("world (g)ravity : %f \n", prefs.getFloat("w_gravity"));
  Serial.printf("(h)Hysterisis limit : %i \n----------\n", prefs.getUInt("g_hysterisis"));
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
  
  //Serial.println(ballVelocity);

  // Newton's 2nd law of motion
  float nReaction = (ballMass*worldGravity*sin(terrainSlopeAngles[ballPositionIndex]*PI/180));
  float nFriction = (ballMass*terrainFriction*worldGravity*cos(terrainSlopeAngles[ballPositionIndex]*PI/180));

  // acceleration
  lastBallVelocityDirection = ballVelocity >=0;
  ballVelocity -= nReaction;
  //Serial.println(ballVelocity);
  // friction 
  if (ballVelocity > 0.0f) ballVelocity -= nFriction;
  else if (ballVelocity < 0.0f ) ballVelocity += nFriction;

  // hysterisis check
  if (playing && lastBallVelocityDirection != (ballVelocity >= 0)){ 
    hysterisisCounter ++;
    ballVelocity /=10.0;
  }
 
  if (hysterisisCounter > hyterisisLimit) {
    //Serial.println("hysterisi");
    ballVelocity = 0;
  }

  ballPosition += ballVelocity; // update ball position

  // position to led index with congruance
  ballPositionIndex = int(floor(ballPosition / terrainScale)) % NUM_LEDS;

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

void checkSerial() {
  if (Serial.available()) { // Serial command parsing
    uint8_t cmd = Serial.read();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    if (cmd == 't') {
      terrainScale =  Serial.parseFloat();
      prefs.putFloat("t_scale", terrainScale);
      printPrefs();
    } else if (cmd == 's') {
      sensorScale =  Serial.parseFloat();
      prefs.putFloat("g_scale", sensorScale);
      printPrefs();
    } else if (cmd == 'm') {
      ballMass =  Serial.parseFloat();
      prefs.putFloat("b_mass", ballMass);
      printPrefs();
    }  else if (cmd == 'f') {
      terrainFriction =  Serial.parseFloat();
      prefs.putFloat("t_friction", terrainFriction);
      printPrefs();
    }  else if (cmd == 'g') {
      worldGravity =  Serial.parseFloat();
      prefs.putFloat("w_gravity", worldGravity);
      printPrefs();
    } else if (cmd == 'v') {
      ballInHoleVelocity =  Serial.parseFloat();
      prefs.putFloat("b_hole", ballInHoleVelocity);
      printPrefs();
    } else if (cmd == 'p') {
      float initialVelocity = Serial.parseFloat();
      ballVelocity = initialVelocity;
      playing = true;
    } else if (cmd == 'h') {
      hyterisisLimit = Serial.parseInt();
      prefs.putUInt("g_hysterisis", hyterisisLimit);
      printPrefs();
    } else if (cmd == 'w') {
      showWin();
    } else {
      printPrefs();
    }
  }
}


//   //  prefs.putFloat("t_scale", terrainScale);
//   // prefs.putFloat("g_scale", sensorScale);
//   // prefs.putFloat("b_mass", ballMass);
//   // prefs.putFloat("t_friction", terrainFriction);
//   // prefs.putFloat("w_gravity", worldGravity);
//   // prefs.putFloat("b_hole", ballInHoleVelocity);
//   // prefs.putUInt("g_hysterisis", hyterisisLimit);


// Replaces placeholder with button section in your web page
String processor(const String& var){
  
  //  terrainScale = prefs.getFloat("t_scale", 200.0);
  // sensorScale = prefs.getFloat("g_scale", 2.0);
  // ballMass = prefs.getFloat("b_mass", 1.0);
  // terrainFriction = prefs.getFloat("t_friction", 0.1);
  // worldGravity = prefs.getFloat("w_gravity", 9.98);
  // ballInHoleVelocity = prefs.getFloat("b_hole", 25.0);
  // hyterisisLimit = prefs.getUInt("g_hysterisis", 50);


  if (var == "T_SCALE"){
    return String(terrainScale);
  } else  if (var == "G_SCALE") {
    return String(sensorScale);
  } else  if (var == "B_MASS") {
    return String(ballMass);
  } else  if (var == "T_FRICTION") {
    return String(terrainFriction);
  } else  if (var == "W_GRAVITY") {
    return String(worldGravity);
  } else  if (var == "B_HOLE") {
    return String(ballInHoleVelocity);
  } else  if (var == "G_HYSTERISIS") {
    return String(hyterisisLimit);
  }
  else 
  return String();
}

void setup() {

  initOTA(OTA_SSID, OTA_PASSWORD);


  pinMode(hall_A, INPUT);
  pinMode(hall_B, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  
  pinMode(RETRY_GND, LOW);
  pinMode(RETRY_BUTTON, INPUT_PULLUP);
  // attachInterrupt(RETRY_BUTTON, ISR3, FALLING);

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
  

  prefs.begin("vGolf", false); 
  #ifdef SETUP
  prefs.putFloat("t_scale", terrainScale);
  prefs.putFloat("g_scale", sensorScale);
  prefs.putFloat("b_mass", ballMass);
  prefs.putFloat("t_friction", terrainFriction);
  prefs.putFloat("w_gravity", worldGravity);
  prefs.putFloat("b_hole", ballInHoleVelocity);
  prefs.putUInt("g_hysterisis", hyterisisLimit);
  #endif

  terrainScale = prefs.getFloat("t_scale", 200.0);
  sensorScale = prefs.getFloat("g_scale", 2.0);
  ballMass = prefs.getFloat("b_mass", 1.0);
  terrainFriction = prefs.getFloat("t_friction", 0.1);
  worldGravity = prefs.getFloat("w_gravity", 9.98);
  ballInHoleVelocity = prefs.getFloat("b_hole", 25.0);
  hyterisisLimit = prefs.getUInt("g_hysterisis", 50);

  holeLedIndex = holes[holeCounter];//+ random(-20,20);

  FastLED.addLeds<CHIPSET, LEDSTRIP_DATA, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness( BRIGHTNESS );

  display.setSegments(GOLF);


    // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  // Send a GET request to <ESP_IP>/slider?value=<inputMessage>
  server.on("/pref", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    // GET input1 value on <ESP_IP>/slider?value=<inputMessage>

    if (request->hasParam("t_scale")) {
      inputMessage = request->getParam("t_scale")->value();
      prefs.putFloat("t_scale", inputMessage.toFloat());
    } else if (request->hasParam("g_scale")) {
      inputMessage = request->getParam("g_scale")->value();
      prefs.putFloat("g_scale", inputMessage.toFloat());
    } else if (request->hasParam("b_mass")) {
      inputMessage = request->getParam("b_mass")->value();
      prefs.putFloat("b_mass", inputMessage.toFloat());
    } else if (request->hasParam("t_friction")) {
      inputMessage = request->getParam("t_friction")->value();
      prefs.putFloat("t_friction", inputMessage.toFloat());
    } else if (request->hasParam("w_gravity")) {
      inputMessage = request->getParam("w_gravity")->value();
      prefs.putFloat("w_gravity", inputMessage.toFloat());
    } else if (request->hasParam("b_hole")) {
      inputMessage = request->getParam("b_hole")->value();
      prefs.putFloat("b_hole", inputMessage.toFloat());
    } else if (request->hasParam("g_hysterisis")) {
      inputMessage = request->getParam("g_hysterisis")->value();
      prefs.putUInt("g_hysterisis", inputMessage.toInt());
    }
    else {
      inputMessage = "No message sent";
    }

    terrainScale = prefs.getFloat("t_scale", 200.0);
    sensorScale = prefs.getFloat("g_scale", 2.0);
    ballMass = prefs.getFloat("b_mass", 1.0);
    terrainFriction = prefs.getFloat("t_friction", 0.1);
    worldGravity = prefs.getFloat("w_gravity", 9.98);
    ballInHoleVelocity = prefs.getFloat("b_hole", 25.0);
    hyterisisLimit = prefs.getUInt("g_hysterisis", 50);

    
    Serial.println(inputMessage);
    request->send(200, "text/plain", "OK");
  });

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
  checkSerial();
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

  // if (retry){
  //   if (hits>=7) {
  //     showLoose();
  //     resetGame();
  //   } else {
  //     showMissed();
  //     replayHole(); 
  //   }
  // }

  if (millis() - lastPlay > 120000 ) { // 2 min reset.
    resetGame();
  }

  play();
  draw();
}
