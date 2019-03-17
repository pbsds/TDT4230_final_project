#pragma once

#include "lodepng.h"
#include <vector>
#include <string>

typedef unsigned int uint;

struct PNGImage {
	uint width, height;
	bool repeat_mirrored = false;
	std::vector<unsigned char> pixels; // RGBA
};

PNGImage loadPNGFile(std::string fileName, bool flip_handedness=false);

PNGImage makePerlinNoisePNG(uint w, uint h, float scale=0.1);

PNGImage makePerlinNoisePNG(uint w, uint h, std::vector<float> scales);
