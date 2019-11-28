class RushWash {
	private:
		int 		address;
		float		pan,tilt,zoom;
		float		lookat_x, lookat_y;
		float		red, green, blue, white;
		float		hue, sat;
		float		dim;
		char 		flashing;
		
	public:
		RushWash();
		void setAddress(int);
		int getAddress();
		void flash(char);
		void update(float);
};