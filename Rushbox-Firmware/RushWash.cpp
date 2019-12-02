#include "Colorspace.h"
#include <math.h>
#include "RushWash.h"
#include <LXESP32DMX.h>
#include <Arduino.h>

//========================================================================================
//----------------------------------------------------------------------------------------
//																				UTILS
float clip (float in, float min, float max) {
	if (in < min) return min;
	if (in > max) return max;
	return in;
}

float mapf(float value, float istart, float istop, float ostart, float ostop) {
	return ostart + (ostop - ostart) * ((value - istart) / (istop - istart));
}

float enc_map(int in) {
    float f = (float)in / 30.;
    f = abs(f);
    f = pow(f,2);
    
    if (in < 0) f = -f;
	return f;
} 


//========================================================================================
//----------------------------------------------------------------------------------------
//																			IMPLEMENTATION

RushWash::RushWash() {
	address = 0;
}
//----------------------------------------------------------------------------------------
CRGB RushWash::getPixelColor(){
	return CRGB( round(actual_state.color.red * 255.), round(actual_state.color.green * 255.) , round(actual_state.color.blue * 255.));
}
//----------------------------------------------------------------------------------------

void RushWash::setAddress(int _address) {
	address = _address;
	if (address < 1) address = 1;
	if (address > (512 - 14)) address = 512 - 14;
}

//----------------------------------------------------------------------------------------

int RushWash::getAddress() {
	return address;
}

//----------------------------------------------------------------------------------------

int RushWash::getColorMode() {
	return actual_state.color_mode;
}

//----------------------------------------------------------------------------------------

int RushWash::getLEE() {
	return actual_state.lee_filter;
}
//----------------------------------------------------------------------------------------

void RushWash::setColorMode(int color_mode) {
    HsvColor* hsvf = NULL;

	 actual_state.color_mode = color_mode;
	 if (color_mode == COLOR_MODE_HSV) {
	 	hsvf = Hsv_CreateFromRgbF(actual_state.color.red, actual_state.color.green, actual_state.color.blue);
		hue = hsvf->H;
		sat = hsvf->S;
	 }
	 free(hsvf);
}
	
//----------------------------------------------------------------------------------------

void RushWash::setLEE(int lee) {
	actual_state.lee_filter = lee;
}

//----------------------------------------------------------------------------------------

rush_t RushWash::getState() {
	return actual_state;
}
//----------------------------------------------------------------------------------------

void RushWash::setPTZ(ptz_t ptz) {
	actual_state.ptz = ptz;
}

//----------------------------------------------------------------------------------------

void RushWash::setColor(rgbw_t color) {
    HsvColor* hsvf = NULL;
	hsvf = Hsv_CreateFromRgbF(color.red, color.green, color.blue);
	
	hue = hsvf->H;
	sat = hsvf->S;
	
	actual_state.color = color;
	free(hsvf);
}

//----------------------------------------------------------------------------------------

void RushWash::setState(rush_t state) {
	actual_state = state;
	// set color again to update hue/sat
	setColor(actual_state.color);
}



//----------------------------------------------------------------------------------------

void RushWash::flash (char do_flash) {
	flashing = do_flash;
}

//----------------------------------------------------------------------------------------

void RushWash::init() {
	actual_state.ptz.zoom = 1.;
	actual_state.ptz.tilt = 0.5;
	actual_state.ptz.pan = 0.5;
	actual_state.color.white = 0.0;
	actual_state.color.red = .5;
	actual_state.color.green = .5;
	actual_state.color.blue = .5;
	actual_state.dim = 1;
	actual_state.lee_filter = 0;
	setColor(actual_state.color);
}

//----------------------------------------------------------------------------------------

void RushWash::handleEncoder(int enc_zoom, int enc_hue, int enc_sat, int enc_white, int enc_dim) {
	if (enc_zoom != 0 ) {
		actual_state.ptz.zoom -= enc_map(enc_zoom);
		actual_state.ptz.zoom = clip(actual_state.ptz.zoom,0.,1.);
	}
	
	
	if (enc_dim != 0 ) {
		actual_state.dim += enc_map(enc_dim);
		actual_state.dim = clip(actual_state.dim,0.,1.);
	}

	
	// RGB COLOR MODE
	if (actual_state.color_mode == COLOR_MODE_RGB) {
		if (enc_hue != 0 ) {
			actual_state.color.red += enc_map(enc_hue);
			actual_state.color.red = clip(actual_state.color.red,0.,1.);
		}
	
		if (enc_sat != 0 ) {
			actual_state.color.green += enc_map(enc_sat);
			actual_state.color.green = clip(actual_state.color.green,0.,1.);
		}

		if (enc_white != 0 ) {
			actual_state.color.blue += enc_map(enc_white);
			actual_state.color.blue = clip(actual_state.color.blue,0.,1.);
		}
	}
	
	
	if (actual_state.color_mode == COLOR_MODE_HSV) {
		
		if (enc_hue != 0 ) {
			hue += enc_map(enc_hue)*180.;
			if (hue > 360.) hue -= 360.;
			if (hue < 0.) hue += 360.;
		}
	
		if (enc_sat != 0 ) {
			sat += enc_map(enc_sat);
			sat = clip(sat,0.,1.);
		}
		
		RgbFColor * temp_color;
		temp_color = RgbF_CreateFromHsv(hue,sat,1.);
		actual_state.color.red = temp_color->R;
		actual_state.color.green = temp_color->G;
		actual_state.color.blue = temp_color->B;
		free(temp_color);

		if (enc_white != 0 ) {
			actual_state.color.white += enc_map(enc_white);
			actual_state.color.white = clip(actual_state.color.white,0.,1.);
		}

	}


	// lee state handled by Rushbox.ino
	// lee looks up rgb values and sets color directly, filter number gets stored for reference only
	
}

//----------------------------------------------------------------------------------------

void RushWash::nudge(float dx, float dy) {

	if (control_mode == CONTROL_MODE_XY) {
		ax += dx;
		ax = clip (ax, 0 , M_PI);
		ay += dy;
		ay = clip (ay, 0, M_PI);
	
		look_at.x = cos(ay);
		look_at.y =	cos(ax) * sin (ay);
		look_at.z = sin(ax) * sin (ay);

		float pan = atan2(look_at.y, look_at.x);
		// gives us -PI to +PI	
		// pan range 0. - 1. is 540 degrees in total. 
		// 360 degrees are 0.6666666, so we want -PI = 0.16666 and PI = 0.8333333
		pan = mapf (pan,-M_PI, M_PI, 0.1666, 0.83333);
	
		float tilt =  atan2((look_at.z - 0.364), sqrt( pow(look_at.x,2) + pow(look_at.y,2)) );
		// tilt range 0. - 1. is 220 degrees 
		tilt = mapf (tilt, -0.61111 * M_PI, 0.61111 * PI, 0., 1.);
		clip (tilt, 0., 1.);
	
		actual_state.ptz.pan = pan;
		actual_state.ptz.tilt = tilt;
	} else {	
		actual_state.ptz.pan 	+= dx;
		actual_state.ptz.tilt 	+= dy;
		actual_state.ptz.pan = clip(actual_state.ptz.pan, 0.,1.);
		actual_state.ptz.tilt = clip(actual_state.ptz.tilt, 0.,1.);
	}
}



void RushWash::update(float master) {


// DMX	
		float level;
		if (flashing) 	{ level = 1.; }
		else 			{ level = actual_state.dim * master; }
		
		ESP32DMX.setSlot(address, 255);
		ESP32DMX.setSlot(address + 1,  int(level * 65533) / 256);						// dim coarse
		ESP32DMX.setSlot(address + 2,  int(level * 65533) % 256);						// dim fine
		
		if (flashing) {
			ESP32DMX.setSlot(address + 3,  0);											// R
			ESP32DMX.setSlot(address + 4,  0);											// G
			ESP32DMX.setSlot(address + 5,  0);											// B
			ESP32DMX.setSlot(address + 6,  255);										// White
		} else {
			ESP32DMX.setSlot(address + 3,  actual_state.color.red * 255);				// R
			ESP32DMX.setSlot(address + 4,  actual_state.color.green * 255);				// G
			ESP32DMX.setSlot(address + 5,  actual_state.color.blue * 255);				// B
			ESP32DMX.setSlot(address + 6,  actual_state.color.white * 255);				// White
		}
		
		ESP32DMX.setSlot(address + 7,  0);												// “Color Wheel” Effect
		ESP32DMX.setSlot(address + 8,  actual_state.ptz.zoom * 255);					// Zoom
		ESP32DMX.setSlot(address + 9,  int(actual_state.ptz.pan * 65533) / 256);		// pan coarse
		ESP32DMX.setSlot(address + 10, int(actual_state.ptz.pan * 65533) % 256);		// pan fine
		ESP32DMX.setSlot(address + 11, int(actual_state.ptz.tilt * 65533) / 256);		// tilt coarse
		ESP32DMX.setSlot(address + 12, int(actual_state.ptz.tilt * 65533) % 256);		// tilt fine
		ESP32DMX.setSlot(address + 13, 0);												// special function
}