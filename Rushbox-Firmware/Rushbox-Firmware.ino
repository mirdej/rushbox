#define VERSION "2019-11-26"

//----------------------------------------------------------------------------------------
//
//  RUSHBOX - Martin Rush Controller by [ a n y m a ]
//                                          
//          Target MCU: DOIT ESP32 DEVKIT V1
//          Copyright:  2019 Michael Egger, me@anyma.ch
//          License:        This is FREE software (as in free speech, not necessarily free beer)
//                                  published under gnu GPL v.3
//
// !!!!!!!!!!!!!!!! 
// !!!!      DMX Input functionality requires replacing the esp32-hal-uart.c
// !!!!      files in Arduino/hardware/espressif/esp32/cores/esp32/
// !!!!!!!!!!!!!!!! 
//
//----------------------------------------------------------------------------------------
#include <Preferences.h>
#include "Timer.h"
#include <SPI.h>
#include "Password.h"
#include "WiFi.h"
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include "ESPAsyncWebServer.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <ESP32Encoder.h>

// ..................................................................................... PIN mapping

const int PIN_STICK_X   =   39;
const int PIN_STICK_Y   =   34;
const int PIN_MASTER    =   36;
const int PIN_PIXELS    =   13;
const int PIN_ENCODERS[]    = {26,27,35,32,33,25,14,12,3,1,15,4};
const int PIN_CS            = 5;

const int NUM_PIXELS    =   26;
// ..................................................................................... SCREEN


#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 32 
#define OLED_RESET		 -1
Adafruit_SSD1306 		display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ..................................................................................... WIFI STUFF 
#define WIFI_TIMEOUT		4000
String hostname;

//========================================================================================
//----------------------------------------------------------------------------------------
//																				GLOBALS
Preferences                             preferences;
Timer                                   t;
AsyncWebServer                          server(80);
Adafruit_NeoPixel 						pixels = Adafruit_NeoPixel(NUM_PIXELS, PIN_PIXELS, NEO_GRB + NEO_KHZ800);
ESP32Encoder                            encoder;
int                     buttons_raw;
signed int              test;

//----------------------------------------------------------------------------------------
//																		pushbuttons
void check_buttons(){
    SPI.beginTransaction(SPISettings(80000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS,LOW);
    delay(1);
    digitalWrite(PIN_CS,HIGH);
    buttons_raw = SPI.transfer(0x00) << 8;
    buttons_raw |= SPI.transfer(0x00);
    SPI.endTransaction();
}


void check_encoder(){
    static int last_line = 5000;
    char dir = encoder.getCount() > 0;
    float f = (float)encoder.getCount() / 2.;
    f = abs(f);
    if (f == 0.5) f = 1;
    f = pow(f,2);
    signed int diff = round(f);
    if (!dir) diff = -diff;
    test = test + diff;
    if (test > 255) test = 255;
    if (test < 0) test = 0;
    encoder.setCount(0);
	if (test != last_line) {
        last_line = test;
        display.clearDisplay();
	display.setCursor(0,0);
	display.setTextWrap(false);
    display.setTextSize(1);
    display.print("LEE");
   // display.drawLine(0, 31, test/2, 20, WHITE);

        File file = SPIFFS.open("/lee2rgb.csv", "r");
        if(!file || file.isDirectory()){
          Serial.println("- empty file or failed to open file");
        }
        int line = 0;
        while(file.available()){
            if (line == test) {
                display.setTextSize(2);
    	    	display.setCursor(0,14);    // number
                display.print(file.readStringUntil(','));
                display.setTextSize(1);
    	    	display.setCursor(40,12); // name
                display.println(file.readStringUntil(','));
    	    	display.setCursor(40,22); // r
                display.print(file.readStringUntil(','));
                 display.print( " ");   	    	// g
                display.print(file.readStringUntil(','));
    	    	 display.print( " "); // b
                display.print(file.readStringUntil('\n'));
                line++;
           } else {
            file.readStringUntil('\n');
            line++;
           }
        }
        Serial.println();
        file.close();
    }
    display.display();

}
 
//----------------------------------------------------------------------------------------
//																file functions

 String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path, "r");
  if(!file || file.isDirectory()){
    Serial.println("- empty file or failed to open file");
    return String();
  }
  Serial.println("- read from file:");
  String fileContent;
  while(file.available()){
    fileContent+=String((char)file.read());
  }
  Serial.println(fileContent);
  return fileContent;
}

void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, "w");
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}


//----------------------------------------------------------------------------------------
//																process webpage template

// Replaces placeholders
String processor(const String& var){
  if(var == "HOSTNAME"){
        return hostname;
    }
  return String();
}

//----------------------------------------------------------------------------------------
//																process webpage template
void restart() {
	display.clearDisplay();
	display.display();
	display.setCursor(0,0);
	display.setTextSize(2);	
	display.println(F("Restart...")); 
	display.display();
	delay(1000);
	ESP.restart();
}

//----------------------------------------------------------------------------------------
//																		harware test
void hardware_test() {

    const int TEST_AD = 1;
    const int TEST_ENC_PINS = 0;
    const int TEST_ENCOODER_0 = 0;
    const int TEST_PIXELS = 1;
    const int TEST_BUTTONS = 1;
	long last_update = millis();
	long start_test = millis();
	int pix_idx;

	while(1) {
	
	 	display.clearDisplay();
	    display.setTextSize(1);				
	    display.setTextColor(WHITE);
	    display.setCursor(0,0);	
	    if (TEST_AD){
        	display.print(analogRead(PIN_STICK_X),DEC);
            display.print(" ");
    	    display.print(analogRead(PIN_STICK_Y),DEC);
            display.print(" ");
        	display.print(analogRead(PIN_MASTER),DEC);
        	display.println();
    	}
        if (TEST_ENC_PINS) {
            for (int i = 0; i < 12; i = i + 2) {
                display.print(digitalRead(PIN_ENCODERS[i]),DEC);
                display.print(digitalRead(PIN_ENCODERS[i+1]),DEC);
                display.print(" ");
            }
            display.println();
        }
        if (TEST_ENCOODER_0) {
        	display.setTextSize(2);
        	char dir = encoder.getCount() > 0;
        	float f = (float)encoder.getCount() / 2.;
        	f = abs(f);
        	if (f == 0.5) f = 1;
        	f = pow(f,2);
        	signed int diff = round(f);
        	if (!dir) diff = -diff;
        	test = test + diff;
        	if (test > 255) test = 255;
        	if (test < 0) test = 0;
            display.print(test,DEC);
            display.drawLine(0, 20, test/2, 20, WHITE);
            encoder.setCount(0);
            display.println();
        }
        
        if (TEST_BUTTONS) {
            check_buttons();
            display.print(buttons_raw,BIN);
        }
                
    	display.display();
	    delay(100);
	    if (TEST_PIXELS) {
            if ((millis() - last_update) > 100) {
                last_update = millis();
                for (int i = 0; i < NUM_PIXELS; i++){
                    pixels.setPixelColor(i, pixels.Color(0,0,10)); 
                }
                pixels.setPixelColor(pix_idx, pixels.Color(100,0,0)); 
                pixels.show();
                pix_idx++;
                pix_idx %= NUM_PIXELS;
            }
        }
	}
}

//========================================================================================
//----------------------------------------------------------------------------------------
//																				SETUP

void setup(){
	display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
	display.setRotation(2); 

	pixels.begin(); 
	pixels.setBrightness(60);
	
    for (int i = 0; i < NUM_PIXELS; i++){
        pixels.setPixelColor(i, pixels.Color(0,0,0)); 
    }
	pixels.show();
	delay(100);
	pixels.show();
	    
    for (int i = 0; i < 12; i++) {
        pinMode(PIN_ENCODERS[i],INPUT_PULLUP);
    }
    
    encoder.attachHalfQuad(4, 15);

    pinMode(PIN_CS, OUTPUT);
    digitalWrite(PIN_CS,HIGH);
    SPI.begin();

    hardware_test();
    
 	display.clearDisplay();
	display.setTextSize(1);				
	display.setTextColor(WHITE);
	display.setCursor(14,0);		
	display.println(F("[ a n y m a ]"));
	display.setCursor(0,16);		
	display.setTextSize(2);				
	display.println(F(" RUSHBOX"));
	display.display();
	delay(1000);
/*
	display.clearDisplay();
	display.setTextSize(1);		
	display.setCursor(40,0);		
	display.println(F("version"));
	display.setCursor(34,12);		
	display.println(F(VERSION));
	display.display();
	delay(1000);
*/

    Serial.begin(115200);
 
    if(!SPIFFS.begin()){
         Serial.println("An Error has occurred while mounting SPIFFS");
         return;
    }
 
    preferences.begin("changlier", false);
    //hostname = readFile(SPIFFS, "/hostname.txt");
    hostname = preferences.getString("hostname");
    if (hostname == String()) { hostname = "changlier"; }
    Serial.print("Hostname: ");
    Serial.println(hostname);
	preferences.end();

 
    WiFi.begin(ssid, pwd);
    long start_time = millis();
    while (WiFi.status() != WL_CONNECTED) { 
        display.print("."); 
        display.display();
        delay(500); 
        if ((millis()-start_time) > WIFI_TIMEOUT) break;
	}

 
  	if (WiFi.status() == WL_CONNECTED) {
  	    Serial.print("Wifi connected. IP: ");
        Serial.println(WiFi.localIP());

        if (!MDNS.begin(hostname.c_str())) {
             Serial.println("Error setting up MDNS responder!");
        }
        Serial.println("mDNS responder started");

        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(SPIFFS, "/index.html",  String(), false, processor);
        });
 
        server.on("/src/bootstrap.bundle.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(SPIFFS, "/src/bootstrap.bundle.min.js", "text/javascript");
        });
 
        server.on("/src/jquery-3.4.1.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(SPIFFS, "/src/jquery-3.4.1.min.js", "text/javascript");
        });
 
        server.on("/src/bootstrap.min.css", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(SPIFFS, "/src/bootstrap.min.css", "text/css");
        });
 
         server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
            String inputMessage;
                        
                        
            //List all parameters
            int params = request->params();
            for(int i=0;i<params;i++){
              AsyncWebParameter* p = request->getParam(i);
              if(p->isFile()){ //p->isPost() is also true
                Serial.printf("FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
              } else if(p->isPost()){
                Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
              } else {
                Serial.printf("GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
              }
            }
            if (request->hasParam("HostName")) {
                inputMessage = request->getParam("HostName")->value();
                hostname = inputMessage;
//                writeFile(SPIFFS, "/hostname.txt", inputMessage.c_str());
                preferences.begin("changlier", false);
            	preferences.putString("hostname", hostname);
                preferences.end();

            } else if (request->hasParam("ReStart")) {
                request->send(200, "text/text", "Restarting...");
                restart();
            } else {
                inputMessage = "No message sent";
            }
            Serial.println(inputMessage);
            request->send(200, "text/text", inputMessage);
        });

        server.begin();
    }
    
    
    t.every(100,check_encoder);
}

//========================================================================================
//----------------------------------------------------------------------------------------
//																				loop
 
void loop(){
    t.update();
}