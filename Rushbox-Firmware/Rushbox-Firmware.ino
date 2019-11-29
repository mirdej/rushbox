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
#include <FastLED.h>
#include <ESP32Encoder.h>
#include <LXESP32DMX.h>
#include "RushWash.h"

// ..................................................................................... PIN mapping

const int PIN_STICK_X   =   39;
const int PIN_STICK_Y   =   34;
const int PIN_MASTER    =   36;
const int PIN_PIXELS    =   13;
const int PIN_ENCODERS[]    = {26,27,35,32,33,25,14,12,3,1,15,4};
const int PIN_CS            = 5;

const int NUM_PIXELS    =   26;

// ..................................................................................... Constants

const int MODE_UNKNOWN  = 0;
const int MODE_FIXTURE  = 1;
const int MODE_SCENE    = 2;
const int MODE_TEST     = 3;




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
CRGB                                    pixels[NUM_PIXELS];
ESP32Encoder                            encoder;

RushWash                fixture;

int                     buttons_raw;
signed int              test;
	char buf[] = "Hello this is an empty message with, a comma in it";

char                    mode;
int 					fixture_count;
int                     selected_fixture;
int                     selected_bank;
int                     selected_scene;

float master = 1.;

//----------------------------------------------------------------------------------------
//																		pushbuttons
void btn_press_copy(){
    Serial.println("Copy");
}

void btn_press_delete(){
    Serial.println("Delete");
}


void btn_press_start(){
    Serial.println("Start");
}


void btn_press_enter(){
    Serial.println("Enter");
}


void check_buttons(){
    static long old_buttons;
    SPI.beginTransaction(SPISettings(80000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS,LOW);
    delay(1);
    digitalWrite(PIN_CS,HIGH);
    buttons_raw = SPI.transfer(0x00) << 8;
    buttons_raw |= SPI.transfer(0x00);
    SPI.endTransaction();
    if (buttons_raw == old_buttons) return;

    if (buttons_raw >> 13 & 1) mode = MODE_FIXTURE;
    else mode = MODE_SCENE;
    
    long triggers = old_buttons & ~buttons_raw;
    old_buttons = buttons_raw;
    if (triggers == 0) return;
    
    //Serial.println(triggers,BIN);
    //Serial.print("Triggers: ");
    for (int i = 0; i < 13; i++) {
        if (triggers & (1 << i)) {
             Serial.print(i);             Serial.print(" ");
             if (i > 0 && i < 9) {
                 if (mode == MODE_FIXTURE) {
                    selected_fixture = i-1;
                    selected_fixture %= fixture_count;
                 } else if (mode == MODE_SCENE) {
                     selected_scene = i-1;
                 }
            }
            if (i == 11) btn_press_copy();
            if (i == 12) btn_press_delete();
            if (i == 10) btn_press_start();
            if (i == 9) btn_press_enter();
        }
    }
    Serial.println();
    
}
//----------------------------------------------------------------------------------------
//																		pixels
void update_pixels() { 
    if (mode == MODE_FIXTURE || mode == MODE_SCENE) {
        FastLED.clear();

        if (mode == MODE_FIXTURE) {
            pixels[7-selected_fixture] = CRGB::Gainsboro; 
            pixels[8+selected_fixture] = CRGB::Gainsboro; 
        
            for (int i = 16; i < 21; i++){
                pixels[i] = CRGB::Gold; 
            }
        
        }
    
        if (mode == MODE_SCENE) {
            pixels[7-selected_scene] = CRGB::Gainsboro; 
            pixels[8+selected_scene] = CRGB::Gainsboro; 
        
            for (int i = 21; i < NUM_PIXELS; i++){
                pixels[i] = CRGB::Gold; 
            }

        }
    
        FastLED.show();
    }
}

//----------------------------------------------------------------------------------------
//																		LEE

void lee(int i) {
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
		if (line == i) {
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
	display.display();
}
//----------------------------------------------------------------------------------------
//																		encoders
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
		lee(test);
	}
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
  if(var == "FIXT_ADDRESS"){
     	preferences.begin("changlier", false);
		String rep = preferences.getString("fixtures");
        return  rep;
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
    if (mode != MODE_TEST) return;
    
    const int TEST_AD = 1;
    const int TEST_ENC_PINS = 1;
    const int TEST_ENCOODER_0 = 0;
    const int TEST_PIXELS = 1;
    const int TEST_BUTTONS = 1;
	static long last_update;
	static long start_test = millis();
	static int pix_idx;
	
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
    	
	    if (TEST_PIXELS) {
            if ((millis() - last_update) > 100) {
                last_update = millis();
                for (int i = 0; i < NUM_PIXELS; i++){
                    pixels[i] = CRGB( 0, 0, 20);
                }
               pixels[pix_idx] = CRGB(100,0,0); 
                FastLED.show();
                pix_idx++;
                pix_idx %= NUM_PIXELS;
            }
        }

}

String string_to_addresses(String  input) {
	int addr;
	char *token;
	String  response;
	Serial.println(input);
	const char s[2] = ",";
	strcpy(buf,input.c_str());

	/* get the first token */
	token = strtok(buf, s);
	int i = 0;
	/* walk through other tokens */
	while( token != NULL ) {
		// printf( " %s\n", token );
		addr = atoi(token);
		response += addr;
		response += ",";
		
		Serial.print("Fixture ");
		Serial.print(i,DEC);
		Serial.print(" ");
		Serial.println(addr,DEC);
		token = strtok(NULL, s);
		i++;
		if (i > 7) break;
	}

	response.remove(response.length()-1,1);
	fixture_count = i;
	Serial.print("Number of fixtures:" );
	Serial.println(fixture_count);
	Serial.println(response);

    preferences.begin("changlier", false);
	preferences.putString("fixtures",response);

	return response;
}

//========================================================================================
//----------------------------------------------------------------------------------------
//																				service dmx

void service() {
    fixture.update(master);
    update_pixels();
}


//========================================================================================
//----------------------------------------------------------------------------------------
//																				SETUP

void setup(){

	display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
	display.setRotation(2); 

    FastLED.addLeds<NEOPIXEL, PIN_PIXELS>(pixels, NUM_PIXELS);
	    
    for (int i = 0; i < 12; i++) {
        pinMode(PIN_ENCODERS[i],INPUT_PULLUP);
    }
    
    encoder.attachHalfQuad(4, 15);

    pinMode(PIN_CS, OUTPUT);
    digitalWrite(PIN_CS,HIGH);
    SPI.begin();
    
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

    hostname = preferences.getString("hostname");
    if (hostname == String()) { hostname = "changlier"; }
    Serial.print("Hostname: ");
    Serial.println(hostname);

	string_to_addresses(preferences.getString("fixtures"));

 
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

        server.on("/rc/bootstrap4-toggle.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(SPIFFS, "rc/bootstrap4-toggle.min.js", "text/javascript");
        });

        server.on("/src/bootstrap4-toggle.min.css", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(SPIFFS, "/src/bootstrap4-toggle.min.css", "text/css");
        });
 
 
       server.on("/set", HTTP_GET, [] (AsyncWebServerRequest *request) {
            String inputMessage;
                        
            inputMessage = "No message sent";
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
            
        	if (request->hasParam("addresses")) {
                	inputMessage = request->getParam("addresses")->value();
                   	request->send(200, "text/text", string_to_addresses(inputMessage));
            }

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
            } else if (request->hasParam("HardwareTest")) {
                request->send(200, "text/text", "Running Hardware test...");
                mode = MODE_TEST;
            }else {
                inputMessage = "No message sent";
            }
            Serial.println(inputMessage);
            request->send(200, "text/text", inputMessage);
        });

        server.begin();
    }
    
    t.every(10, check_buttons);    
    t.every(25, service);    
//    t.every(100,check_encoder);
    t.every(100, hardware_test);
}



//========================================================================================
//----------------------------------------------------------------------------------------
//																				loop
 
void loop(){
    t.update();
}