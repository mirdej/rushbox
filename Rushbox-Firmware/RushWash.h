#include "Colorspace.h"
#include <FastLED.h>

const int COLOR_MODE_HSV	= 0;
const int COLOR_MODE_RGB	= 1;
const int COLOR_MODE_LEE	= 2;

const int CONTROL_MODE_PT	= 0;
const int CONTROL_MODE_XY	= 1;

struct ptz_t 		{float		pan,tilt,zoom; } ;
struct point3d_t	{float 		x, y , z; };
struct rgbw_t 		{float		red, green, blue, white;};

struct rush_t	{ 
	ptz_t 	ptz;
	rgbw_t	color;
	float	dim;
	char color_mode;
	int	lee_filter;
};

class RushWash {
	private:
		int 		address;
		rush_t		actual_state;
		point3d_t	look_at;
		float		ax, ay;
		double		hue,sat;
		char 		flashing;
		int 		control_mode;
		char 		is_virgin;


		
	public:
		RushWash();
		void setAddress(int);
		int getAddress();
		
		rush_t getState();
		void setPTZ(ptz_t);
		void setColor(rgbw_t);
		void setState(rush_t);
		
		void flash(char);
		void nudge(float,float);
		void update(float);
		void init();
		void handleEncoder(int, int , int, int, int);
		
		CRGB getPixelColor();
		int	getColorMode();
		int	getLEE();
		void setColorMode(int);
		void setLEE(int);
		void unsetVirgin();

};