//this file will be used to test all of the parsers and helpers associated with it
//depending on input into the console, all the tests can be run at once, or individual tests can be selected

#include <iostream>
#include "png/png.h"

using namespace ImageLibrary;

int main() {
	std::cout << "Hello World";

	PNG::PNGStream<std::basic_ifstream<uint8_t, std::char_traits<uint8_t>>, Generic::Mode::Read> png("C:\\Users\\madma\\Desktop\\github\\image-library\\png\\test\\test-suite\\basi0g01.png");
	ImageOptions opt = { .receiveInterlaced = true, .receiveAnimation = true };
	ImageData data;

	bool final = false;
	while (!final) {
		ImageReturnInfo info = png.ReadData(&data, &opt);
		final = info.final;
		if (info.valid) {
			ImageStreamState state = png.QueryState();
			int breakpoint = 0;
		}
		else {
			PNG::PNGStreamState state = png.ExtQueryState();
			int breakpoint = 0;
		}
	}
}