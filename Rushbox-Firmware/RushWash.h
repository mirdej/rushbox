struct ptz_t 	{float		pan,tilt,zoom; } ;
struct rgbw_t 	{float		red, green, blue, white;};

struct rush_t	{ 
	ptz_t 	ptz;
	rgbw_t	color;
	float	dim;
};

class RushWash {
	private:
		int 		address;
		rush_t		actual_state;
		float		lookat_x, lookat_y;
		float		hue, sat;
		char 		flashing;
		
	public:
		RushWash();
		void setAddress(int);
		int getAddress();
		void flash(char);
		void update(float);
};