//this file will be used to test all of the parsers and helpers associated with it
//depending on input into the console, all the tests can be run at once, or individual tests can be selected

#include <iostream>
#include "png/png.h"

int main() {
	std::cout << "Hello World";
	ImageLibrary::PNG::PNGStream<std::basic_ifstream<uint8_t, std::char_traits<uint8_t>>, Generic::Mode::Read> png("C:\\Users\\madma\\Desktop\\github\\image-library\\png\\test\\test-suite\\basi0g01.png");
}