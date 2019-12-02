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
#include "Colorspace.h"

// ..................................................................................... PIN mapping

const int PIN_STICK_X   =   39;
const int PIN_STICK_Y   =   34;
const int PIN_MASTER    =   36;
const int PIN_PIXELS    =   13;
const int PIN_ENCODERS[]    = {26,27, 35,32, 25,33, 12,14, 1,3, 4,15};
const int PIN_CS            = 5;


const int DMX_SERIAL_OUTPUT_PIN = 17;
const int NUM_PIXELS    =   26;

// ..................................................................................... Constants

#define COLOR_SELECTION CRGB::Gray

const int MODE_UNKNOWN  = 0;
const int MODE_FIXTURE  = 1;
const int MODE_SCENE    = 2;
const int MODE_TEST     = 3;

const int COPY_MODE_NONE 	= 0;
const int COPY_MODE_COLOR 	= 1;
const int COPY_MODE_PTZ 	= 2;
const int COPY_MODE_ALL 	= 3;
const int COPY_MODE_SCENE 	= 4;

const int DELETE_MODE_NONE 	= 0;
const int DELETE_MODE_ON 	= 1;


const int MENU_NONE			= 0;
const int MENU_COLORS_HSV	= 1;
const int MENU_COLORS_RGB	= 2;
const int MENU_COLORS_LEE	= 3;

const int POT_PAD			 = 50;
const float  POT_DIV		= 4000.;

const int BANK_COUNT			= 15;
int menu_items_count = 4;
// ..................................................................................... SCREEN


#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 32 
#define OLED_RESET		 -1
Adafruit_SSD1306 		display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ..................................................................................... WIFI STUFF 
#define WIFI_TIMEOUT		4000
String 	hostname;


//========================================================================================
//----------------------------------------------------------------------------------------
//																				GLOBALS
Preferences                             preferences;
Timer                                   t;
AsyncWebServer                          server(80);
CRGB                                    pixels[NUM_PIXELS];
ESP32Encoder                            encoder[6];

RushWash                fixture[8];

int                     buttons_raw;
signed int              test;
	char buf[] = "Hello this is an empty message with, a comma in it";

char                    mode;
char					copy_mode;
char					color_mode;
char					delete_mode;

rush_t					copy_state;
rush_t					clean_state;
rush_t					scene[8][8];
rush_t					copied_scene[8];

CRGB					scene_color[8][2];
int 					fixture_count;
int                     selected_fixture;
int                     selected_bank;
int                     selected_scene;
int                     selected_menu;
char					in_chase;

String					lee_string;
int						lee_number;

float master = 1.;

long		last_ui_interaction;
long 		last_btn_press_copy;
const int 	COPY_BTN_TIMEOUT = 1000;
const int 	UI_TIMEOUT = 5000;

//========================================================================================
//----------------------------------------------------------------------------------------
//																				PROTOTYPES

void update_display();
void btn_press_copy();
void check_buttons();
float center_pad(int);
void check_ad();
void update_pixels();
rgbw_t lee(int);
void set_scene_color(int);

//----------------------------------------------------------------------------------------
//																		SAVE
char f2int_highbyte(float f){ return char((f * 65533) / 256); } 
char f2int_lowbyte(float f){ return char((int)(f * 65533) % 256); } 
char f2char(float f){ return (char)(f * 255.); } 
float int2f(char highbyte, char lowbyte){ int res = (highbyte << 8) | lowbyte; return (float)res/65533.; } 
float char2f(char inbyte){ return (float)inbyte / 255.; } 



void save_scene(int scene_idx) {
	String filename;
	String filename_base = "/B";
	int num_bytes;
	filename_base += selected_bank;
	filename_base += "S";
	filename_base += scene_idx;
	filename_base += "F";
	rush_t		st;
	
	uint8_t buf[14];
	

	for (int i = 0; i < fixture_count; i++) {
		scene[scene_idx][i] = fixture[i].getState(); 
		filename = filename_base + i;
		filename += ".hex";
		
		File file = SPIFFS.open(filename, "w");
		num_bytes = 0;
		if (!file) {
  			Serial.println("Error opening file for writing");
		} else {
			st = scene[scene_idx][i];
			buf[0] 	= f2int_highbyte		(st.dim);
			buf[1] 	= f2int_lowbyte			(st.dim);
			buf[2] 	= f2char				(st.ptz.zoom);
			buf[3] 	= f2int_highbyte		(st.ptz.pan);
			buf[4]	= f2int_lowbyte			(st.ptz.pan);
			buf[5]	= f2int_highbyte		(st.ptz.tilt);
			buf[6] 	= f2int_lowbyte			(st.ptz.tilt);
			buf[7]	= f2char				(st.color.red);
			buf[8] 	= f2char				(st.color.green);
			buf[9] 	= f2char				(st.color.blue);
			buf[10] = f2char				(st.color.white);
			buf[11]	= st.lee_filter >> 8;
			buf[12]	= st.lee_filter % 256;
			buf[13] = st.color_mode;
		
			num_bytes = file.write(buf, sizeof(buf));
			if (num_bytes != sizeof(buf)) {
				Serial.println("WRITE ERROR");
			}
		}
		file.close();
		Serial.print (num_bytes);
		Serial.print(" bytes written to ");
		Serial.println(filename);
	}
	set_scene_color(scene_idx);

}

//----------------------------------------------------------------------------------------
//																		READ

int read_scene(int scene_idx) {

	int success;
	String filename;
	String filename_base = "/B";
	int num_bytes;
	filename_base += selected_bank;
	filename_base += "S";
	filename_base += scene_idx;
	filename_base += "F";
	success = 1;			// start from the principle we will succeed
	rush_t		st;
	char buf[14];

	for (int i = 0; i < fixture_count; i++) {
		filename = filename_base + i;
		filename += ".hex";
		
		if (!SPIFFS.exists(filename)) {
			Serial.print("ERROR: FILE DOES NOT EXIST: ");
			Serial.println(filename);
			success = 0;
		} else {

			File file = SPIFFS.open(filename, "rb");
			file.setTimeout(0);

			num_bytes = file.readBytes((char *)buf, sizeof(buf)); //cast changed from byte*
			if (!(num_bytes == sizeof(buf))) {
				Serial.print("ERROR: NOT ENOUGH BYTES ");
				success = 0;
			} else {
				st.dim 			= 	int2f(	buf[0],		buf[1]	);
				st.ptz.zoom 	= 	char2f(	buf[2]				);
				st.ptz.pan		= 	int2f(	buf[3], 	buf[4]	);
				st.ptz.tilt		= 	int2f(	buf[5], 	buf[6]	);
				st.color.red	=	char2f(	buf[7]				);
				st.color.green	=	char2f(	buf[8]				);
				st.color.blue	=	char2f(	buf[9]				);
				st.color.white	=	char2f(	buf[10]				);
				st.lee_filter	= 	int2f(	buf[11], 	buf[12]	);
				st.color_mode	=	char2f(	buf[13]				);
				
				scene[scene_idx][i] = st;

			}

			file.close();
			Serial.print (num_bytes);
			Serial.print(" bytes read fom ");
			Serial.println(filename);
		}
	}
	return success;
}

//----------------------------------------------------------------------------------------
//																		print color

void printColor(CRGB col) {
	Serial.print(col.r,DEC);
	Serial.print(" ");
	Serial.print(col.g,DEC);
	Serial.print(" ");
	Serial.print(col.b,DEC);
	Serial.println();
}
//----------------------------------------------------------------------------------------
//																		pushbuttons
void btn_press_copy(){
	
	if (mode == MODE_FIXTURE) {
		switch (copy_mode) {
			case COPY_MODE_NONE:
				copy_state = fixture[selected_fixture].getState();
				copy_mode = COPY_MODE_COLOR;
				break;
				
			case COPY_MODE_COLOR:
				if (millis()-last_btn_press_copy > COPY_BTN_TIMEOUT) {	copy_mode = COPY_MODE_NONE; }
				else {													copy_mode = COPY_MODE_PTZ;	}
				break;
				
			case COPY_MODE_PTZ:
				if (millis()-last_btn_press_copy > COPY_BTN_TIMEOUT) {	copy_mode = COPY_MODE_NONE; }
				else {													copy_mode = COPY_MODE_ALL;	}
				break;

			case COPY_MODE_ALL:
				copy_mode = COPY_MODE_NONE;
		}
		
		last_btn_press_copy = millis();
	}
	
	if (mode == MODE_SCENE) {
		switch (copy_mode) {
			case COPY_MODE_NONE:
				for (int fixt = 0; fixt < fixture_count; fixt++) {
					copied_scene[fixt] = scene[selected_scene][fixt];
				}
				copy_mode = COPY_MODE_SCENE;
				break;
	
			default:
				copy_mode = COPY_MODE_NONE;
		}
		
		last_btn_press_copy = millis();
	}

	update_display();
}

void delete_scene(int sc) {
	for (int i = 0; i < fixture_count; i++) {
		fixture[i].init();
		scene[sc][i] = fixture[i].getState();
		set_scene_color(sc);
		fixture[i].setState(scene[selected_scene][i]); 	// don't touch selected scene
	}
}


void btn_press_delete(){
	if (delete_mode == DELETE_MODE_NONE){
		copy_mode = COPY_MODE_NONE;
		delete_mode = DELETE_MODE_ON;
	} else {
		delete_mode = DELETE_MODE_NONE;
	}
}


void btn_press_start(){
    Serial.println("Start");
}


void btn_press_enter(){

	switch(selected_menu) {
		case MENU_NONE:
			break;
			
		case MENU_COLORS_HSV:
			color_mode = COLOR_MODE_HSV;
			fixture[selected_fixture].setColorMode(color_mode);
			break;
			
		case MENU_COLORS_RGB:
			color_mode = COLOR_MODE_RGB;
			fixture[selected_fixture].setColorMode(color_mode);
			break;
			
		case MENU_COLORS_LEE:
			color_mode = COLOR_MODE_LEE;
			fixture[selected_fixture].setColorMode(color_mode);
			break;
	}
	
	update_display();
}


void check_buttons(){
    static long old_buttons;
    static int old_mode;
    int temp;
    SPI.beginTransaction(SPISettings(80000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS,LOW);
    delay(1);
    digitalWrite(PIN_CS,HIGH);
    buttons_raw = SPI.transfer(0x00) << 8;
    buttons_raw |= SPI.transfer(0x00);
    SPI.endTransaction();
    if (buttons_raw == old_buttons) return;

	temp = (buttons_raw >> 13 & 1);				// mode switch
	if (temp != old_mode) {
		if (delete_mode) delete_mode = DELETE_MODE_NONE;
		old_mode = temp;
		if (temp){
			 mode = MODE_FIXTURE;
			 if (copy_mode == COPY_MODE_SCENE) copy_mode = COPY_MODE_NONE;
		} else mode = MODE_SCENE;
   		save_scene(selected_scene);
   	}
    
    long triggers_press = old_buttons & ~buttons_raw;
    long triggers_release = ~old_buttons & buttons_raw;
    old_buttons = buttons_raw;
    
    if (triggers_press == 0 && triggers_release == 0) return;
    

	last_ui_interaction = millis();


    for (int i = 0; i < 13; i++) {
        if (triggers_press & (1 << i)) {
     //       ----------------------------------------- FIXTURE MODE
             if (i > 0 && i < 9) {
                 if (mode == MODE_FIXTURE) {
					temp = i-1;
                    if (temp > fixture_count-1) temp = fixture_count-1;
                    if (delete_mode == DELETE_MODE_ON) {
                    	fixture[temp].init();
                    	scene[selected_scene][temp] = fixture[temp].getState();
                    } else {
						switch (copy_mode) {
							case COPY_MODE_NONE:
								selected_fixture = temp;
								fixture[selected_fixture].flash(1);
								color_mode = fixture[selected_fixture].getColorMode();
								update_display();
								break;
							
							case COPY_MODE_COLOR:
								fixture[temp].setColor(copy_state.color);
								fixture[temp].setColorMode(copy_state.color_mode);
								fixture[temp].setLEE(copy_state.lee_filter);
								break;
							
							case COPY_MODE_PTZ:
								fixture[temp].setPTZ(copy_state.ptz);
								break;

							case COPY_MODE_ALL:
								fixture[temp].setState(copy_state);
						}
					}
					
     //       ----------------------------------------- SCENE MODE
                 } else if (mode == MODE_SCENE) {
					if (delete_mode == DELETE_MODE_ON) {
                    	delete_scene(i-1);
                    } else if (copy_mode == COPY_MODE_SCENE) {
                 		for (int fixt = 0; fixt < fixture_count; fixt++) {
                 			scene[i-1][fixt] = copied_scene[fixt];
                 		}
                 		set_scene_color(i-1);
                 	} else {
						// save this scene first
						save_scene(selected_scene);
							// select new scene
						selected_scene = i-1;
						for (int i = 0; i < fixture_count; i++) {
							fixture[i].setState(scene[selected_scene][i]);
						}
					}
                    update_display();
                 }
            }
            if (i == 11) btn_press_copy();
            if (i == 12) btn_press_delete();
            if (i == 10) btn_press_start();
            if (i == 9) btn_press_enter();
        }
    }
    
    for (int i = 0; i < 13; i++) {
        if (triggers_release & (1 << i)) {
             if (i > 0 && i < 9) {
                 if (mode == MODE_FIXTURE) {
                 	int fix = i-1;
                    fixture[i-1].flash(0);
                    if (fix > fixture_count-1) fix = fixture_count-1;
                    fixture[fix].flash(0);
                 } else if (mode == MODE_SCENE) {
                 }
            }
          }
    }    
}
//----------------------------------------------------------------------------------------
//																		pots


float center_pad(int n) {
	float d = 0.;
	if (n < (2048 - POT_PAD)) { 
		d = (2048 - POT_PAD - n) /  POT_DIV; 
	}
	if (n > (2048 + POT_PAD)) { 
		d = (n - 2048 - POT_PAD) /  POT_DIV; 
	}
	d = abs(d*d*d);
	if (n < 2048) d = 0.-d;
	return d / 10.;
}


void check_ad() {
	int temp;
	static int master_avg;
	float f;

	temp = analogRead(PIN_MASTER);
	temp = (3 * master_avg + temp ) / 4;
	master_avg = temp;
	
	temp = temp - 47;
	if (temp < 0) temp = 0;
	
	f = (float)temp / 4000.;
	if (f > 1.) f = 1.;
	f = 1. - f;
	
	master = f;
	
	if (mode == MODE_FIXTURE) {
		fixture[selected_fixture].nudge(center_pad(4095-analogRead(PIN_STICK_X)),center_pad(analogRead(PIN_STICK_Y)));
	}	
}

//----------------------------------------------------------------------------------------
//																		pixels
void update_pixels() { 
	static long last_blink;
	static boolean in_blink;
	boolean do_blink;
	CRGB blink_color;
	
	do_blink = false;
	if (delete_mode != DELETE_MODE_NONE) {
		do_blink = true;
		blink_color = CRGB::Red;
	} else if (mode == MODE_FIXTURE) {
		if (copy_mode != COPY_MODE_NONE && copy_mode != COPY_MODE_SCENE) {
			do_blink = true;
			blink_color = CRGB::Green;
		}
	} else if (mode == MODE_SCENE) {
		if (copy_mode == COPY_MODE_SCENE) {
			do_blink = true;
			blink_color = CRGB::Green;
		}
	} 
	
	
	if (millis() - last_blink > 500) {
		last_blink = millis();
		if (!in_blink){ in_blink = true; } else {in_blink = false;}
    }	
    

    if (mode == MODE_FIXTURE || mode == MODE_SCENE) {
        FastLED.clear();
     //       ----------------------------------------- FIXTURE MODE

        if (mode == MODE_FIXTURE) {
        	
	 		for (int i = 0; i < fixture_count; i++) {
				if (do_blink) {
					pixels[7 - i] = blink_color;
				} else {
    	    		pixels[7 - i] = fixture[i].getPixelColor();
				}
        	}
	 
	  		if (in_blink) { pixels[7 - selected_fixture] = COLOR_SELECTION; }

	
           // pixels[7-selected_fixture] = COLOR_SELECTION; 
        
        	pixels[16] = COLOR_SELECTION;				// dim
        	switch (color_mode) {
				case COLOR_MODE_HSV:
		        	pixels[17] = COLOR_SELECTION;		//white
		        	pixels[18] = COLOR_SELECTION;		//Sat
		        	pixels[19] = COLOR_SELECTION;		//Hue
		        	pixels[20] = COLOR_SELECTION;		//Zoom
					break;
				case COLOR_MODE_RGB:
		        	pixels[17] = CRGB::Blue;
		        	pixels[18] = CRGB::Green;
		        	pixels[19] = CRGB::Red;
		        	pixels[20] = COLOR_SELECTION;
					break;
				case COLOR_MODE_LEE:
		        	pixels[17] = CRGB::Black;
		        	pixels[18] = CRGB::Black;
		        	pixels[19] = CRGB::Gold;
		        	pixels[20] = COLOR_SELECTION;
					break;
        	}
        	        
        }
         //       ----------------------------------------- SCENE MODE

        if (mode == MODE_SCENE) {
        	for (int i = 0; i < 8; i++) {
        		pixels[7-i] = scene_color[i][0];
        		pixels[8+i] = scene_color[i][1];
        	}
        	
   			if (in_blink) {
				if (do_blink) {
					for (int i = 0; i < 8; i++) {
							pixels[7-i] = blink_color;
							pixels[8+i] = blink_color;
					}
				}
				
	            pixels[7-selected_scene] = COLOR_SELECTION; 
    	        pixels[8+selected_scene] = COLOR_SELECTION; 
    	    }
        	
        	if (in_chase) {
     	       for (int i = 21; i < NUM_PIXELS; i++){
    	            pixels[i] = CRGB::Gold; 
  	          }
  	      	} else {
  	      		 pixels[21]  = CRGB::Gold; 
  	      		 pixels[NUM_PIXELS - 1]  = CRGB::Gold; 
  	      	}

        }
    
        FastLED.show();
    }
}

//----------------------------------------------------------------------------------------
//																		scene color
void set_scene_color(int idx) {
	CRGB temp;
	temp = CRGB::Black;
	for (int fixt = 0; fixt < fixture_count / 2; fixt++) {
		temp += CRGB(scene[idx][fixt].color.red * 255 ,scene[idx][fixt].color.green * 255,scene[idx][fixt].color.blue * 255).fadeLightBy(256 / fixture_count * 2);
		//printColor(temp);
	}
	scene_color[idx][0] = temp;
	
	temp = CRGB::Black;
	for (int fixt = fixture_count / 2; fixt < fixture_count; fixt++) {
		temp += CRGB(scene[idx][fixt].color.red * 255 ,scene[idx][fixt].color.green * 255,scene[idx][fixt].color.blue * 255).fadeLightBy(256 / fixture_count * 2);
	}
	scene_color[idx][1] = temp;
}

//----------------------------------------------------------------------------------------
//																		LEE

rgbw_t lee(int i) {	

	rgbw_t color;
	int r,g,b;
   // display.drawLine(0, 31, test/2, 20, WHITE);

	File file = SPIFFS.open("/lee2rgb.csv", "r");
	if(!file || file.isDirectory()){
	  Serial.println("- empty file or failed to open file");
	}
	int line = 0;
	while(file.available()){
		if (line == i) {
			lee_number = file.readStringUntil(',').toInt();
			lee_string = file.readStringUntil(',');
			r =file.readStringUntil(',').toInt();
			g = file.readStringUntil(',').toInt();
			b = file.readStringUntil('\n').toInt();
			line++;
			color.red = (float)r/255.; 
			color.green = (float)g/255.;
			color.blue=(float)b/255.;
			color.white = 0.;
	   } else {
		file.readStringUntil('\n');
		line++;
	   }
	}
	file.close();
	return color;
}
	
//----------------------------------------------------------------------------------------
//																		encoders

int encoder_accel(int i){
    float f = (float)i / 2.;
    f = abs(f);
    if (f == 0.5) f = 1;
    f = pow(f,2);
    signed int diff = round(f);
    if (i < 0) diff = -diff;
	return diff;
}

void display_white_enc() {
	if (digitalRead(12)) {
		display.drawPixel(127, 0, WHITE);
	} else {
		display.drawPixel(127, 0, BLACK);
	}
	display.display();
}


void check_encoder(){
	signed int e[6];
	signed long total;
	
	for (int i = 0; i < 6; i++) {
		e[i] = encoder[i].getCount();
		total += abs(e[i]);
	}
		
	if (total == 0) return;

	//last_ui_interaction = millis();

	if (mode == MODE_FIXTURE) {
		
		if (copy_mode == COPY_MODE_NONE) {
			fixture[selected_fixture].handleEncoder(e[0], e[1], e[2], e[3], e[4]); 
			
			if (color_mode == COLOR_MODE_LEE) {
				if (e[1] != 0) {
					selected_menu = MENU_NONE;
					signed int temp = fixture[selected_fixture].getLEE();
					temp += encoder_accel(e[1]);
					if (temp < 0) temp += 151;
					if (temp > 151) temp -= 151;
					fixture[selected_fixture].setColor(lee(temp));
					fixture[selected_fixture].setLEE(temp);
					update_display();
				}
			}
			
		}
		
		if (e[5] != 0) {
			last_ui_interaction = millis();
			copy_mode = COPY_MODE_NONE;
			if (e[5] > 0) selected_menu++;
			if (e[5] < 0) selected_menu--;
			if (selected_menu < 0 ) selected_menu = 0;
			if (selected_menu > menu_items_count -1) selected_menu =  menu_items_count - 1;
			update_display();
		}
		
	}
		
	if (mode == MODE_SCENE) {
		if (e[0] > 0) selected_bank++;
		if (e[0] < 0) selected_bank--;
		if (selected_bank < 0 ) selected_bank = 0;
		if (selected_bank > BANK_COUNT) selected_bank = BANK_COUNT;
		update_display();
	}

	for (int i = 0; i < 6; i++) {
		encoder[i].setCount(0);
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
        	char dir = encoder[0].getCount() > 0;
        	float f = (float)encoder[0].getCount() / 2.;
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
            encoder[0].setCount(0);
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
		if (addr < 0) addr = 0;
		if (addr > 496) addr = 496;
		response += ",";
		
		Serial.print("Fixture ");
		Serial.print(i,DEC);
		Serial.print(" ");
		Serial.println(addr,DEC);
		
		fixture[i].setAddress(addr);
		
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

void update_display() {
	display.clearDisplay();
	display.setTextSize(1);				
	display.setTextColor(WHITE);


	switch(selected_menu) {
		case MENU_NONE:
		
			display.setCursor(0,0);
			display.print("BNK: ");
			display.write((char)selected_bank + 65);
			display.print(" SCN: ");
			display.print(selected_scene + 1);
			display.print(" FXT: ");
			display.print(selected_fixture + 1);
	
			display.setTextSize(2);				
			display.setCursor(0,14);
			
			if (delete_mode == DELETE_MODE_ON) {display.print("DELETE"); }
			else if (copy_mode == COPY_MODE_COLOR) {display.print("COPY COLOR"); }
			else if (copy_mode == COPY_MODE_PTZ) {display.print("COPY PTZ"); }
			else if (copy_mode == COPY_MODE_ALL) {display.print("COPY ALL"); }
			else if (copy_mode == COPY_MODE_SCENE) {display.print("COPY SCENE"); }
			else if (color_mode == COLOR_MODE_LEE) {
				if (mode == MODE_FIXTURE) {
					display.setCursor(0,11);
					display.setTextWrap(false);
					display.setTextSize(1);
					display.print("LEE");
					display.setTextSize(2);
					display.setCursor(0,18);    // number
					display.print(lee_number);
					display.setTextSize(1);
					display.setCursor(40,17); // name
					display.println(lee_string.substring(0,14));
					display.setCursor(40,25); // name
					display.println(lee_string.substring(14));
				}
			}
	
			break;
			
		case MENU_COLORS_HSV:
			display.setCursor(0,0);
			display.print("Color Mode:");
	
			display.setTextSize(2);				
			display.setCursor(0,14);

			display.print("HSV");
			if (color_mode == COLOR_MODE_HSV) display.print(" (X)");
			break;
			
		case MENU_COLORS_RGB:
			display.setCursor(0,0);
			display.print("Color Mode:");
	
			display.setTextSize(2);				
			display.setCursor(0,14);
			display.print("RGB");
			if (color_mode == COLOR_MODE_RGB) display.print(" (X)");
			break;
			
		case MENU_COLORS_LEE:
			display.setCursor(0,0);
			display.print("Color Mode:");
	
			display.setTextSize(2);				
			display.setCursor(0,14);
			display.print("LEE");
			if (color_mode == COLOR_MODE_LEE) display.print(" (X)");
			break;
	}

	display.drawLine(0,8,128,8,WHITE);

	display.display();
}

//----------------------------------------------------------------------------------------
//																		UI Timeout

void check_ui_timeout() {
	if (millis() - last_ui_interaction > UI_TIMEOUT) {
		copy_mode = COPY_MODE_NONE;
		delete_mode = DELETE_MODE_NONE;
		selected_menu = 0;
		update_display();
	}
}

void intro() {
	display.clearDisplay();
	display.setTextSize(1);				
	display.setTextColor(WHITE);
	display.setCursor(26,6);		
	display.println(F("[ a n y m a ]"));
	display.setCursor(24,16);		
	display.setTextSize(2);				
	display.println(F("RUSHBOX"));
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
}

//========================================================================================
//----------------------------------------------------------------------------------------
//																				service dmx

void service() {

	check_ad();
	
	for (int i = 0; i < fixture_count; i++) {
	    fixture[i].update(master);
	}
    update_pixels();
}


//========================================================================================
//----------------------------------------------------------------------------------------
//																				SETUP

void setup(){

	display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
	display.setRotation(2); 
	intro();

    FastLED.addLeds<NEOPIXEL, PIN_PIXELS>(pixels, NUM_PIXELS);
	    
    for (int i = 0; i < 12; i++) {
        pinMode(PIN_ENCODERS[i],INPUT_PULLUP);
    }
    
    for (int i = 0; i < 6; i++) {
	    encoder[i].attachHalfQuad(PIN_ENCODERS[2*i], PIN_ENCODERS[2*i+1]);
	}

    pinMode(PIN_CS, OUTPUT);
    digitalWrite(PIN_CS,HIGH);
    SPI.begin();
    

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
    
    ESP32DMX.startOutput(DMX_SERIAL_OUTPUT_PIN);


	
	// clean, empty fixtures to start with
    for (int i = 0; i < fixture_count; i++) {
    	fixture[i].init();
    }
    
    
    // try to read files or create empty scenes
    for (int sc = 0; sc < 8; sc++) {
    	if (!read_scene(sc)) {
	    	for (int i = 0; i < fixture_count; i++) {
				scene[sc][i] = fixture[i].getState();
			}
		}
		set_scene_color(sc);
  	}
    
    selected_scene = 0;
	for (int i = 0; i < fixture_count; i++) {
		fixture[i].setState(scene[selected_scene][i]);
	}
	mode = MODE_SCENE;


    t.every(10, check_buttons);    
    t.every(25, service);    
	t.every(100,check_encoder);
    t.every(100, hardware_test);
    t.every(200, display_white_enc);
    t.every(1000, update_display);
    t.every(1000,check_ui_timeout);
}



//========================================================================================
//----------------------------------------------------------------------------------------
//																				loop
 
void loop(){
    t.update();
}