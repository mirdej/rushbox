#include "RushWash.h"
#include <LXESP32DMX.h>
#include <Arduino.h>

RushWash::RushWash() {
	address = 0;
}

void RushWash::setAddress(int _address) {
	address = _address;
	if (address < 1) address = 1;
	if (address > (512 - 14)) address = 512 - 14;
}

int RushWash::getAddress() {
	return address;
}

void RushWash::flash (char do_flash) {
	flashing = do_flash;
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
			ESP32DMX.setSlot(address + 3,  actual_state.color.red * 255);									// R
			ESP32DMX.setSlot(address + 4,  actual_state.color.green * 255);								// G
			ESP32DMX.setSlot(address + 5,  actual_state.color.blue * 255);									// B
			ESP32DMX.setSlot(address + 6,  actual_state.color.white * 255);								// White
		}
		
		ESP32DMX.setSlot(address + 7,  0);												// “Color Wheel” Effect
		ESP32DMX.setSlot(address + 8,  actual_state.ptz.zoom * 255);										// Zoom
		ESP32DMX.setSlot(address + 9,  int(actual_state.ptz.pan * 65533) / 256);							// pan coarse
		ESP32DMX.setSlot(address + 10, int(actual_state.ptz.pan * 65533) % 256);							// pan fine
		ESP32DMX.setSlot(address + 11, int(actual_state.ptz.tilt * 65533) / 256);						// tilt coarse
		ESP32DMX.setSlot(address + 12, int(actual_state.ptz.tilt * 65533) % 256);						// tilt fine
		ESP32DMX.setSlot(address + 13, 0);												// special function
}