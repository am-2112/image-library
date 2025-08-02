//this file will be used to test all of the parsers and helpers associated with it
//depending on input into the console, all the tests can be run at once, or individual tests can be selected

#include <iostream>
#include "interface/image-stream-interface.h"

int main() {
	std::cout << "Hello World";

	Generic::Data < std::basic_ifstream<uint8_t, std::char_traits<uint8_t>>, uint8_t, Generic::Mode::Read> test("C:\\Users\\madma\\Desktop\\github\\recovery.txt");
	uint8_t buffer[20] = {};
	test.Read(buffer, 20);
}