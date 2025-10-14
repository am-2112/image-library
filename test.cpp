//this file will be used to test all of the parsers and helpers associated with it
//depending on input into the console, all the tests can be run at once, or individual tests can be selected
//need to find a simple gfx api (eg. opengl?) that is quick to set up and display texture on screen
//will not include the api in the github but instead inform the user that there are dependencies (can this be set up in premake? as well as compiler config such as simd stuff?)

#include <iostream>
#include "png/png.h"
#include <filesystem>

#include <thread>
#include <atomic>

#include <glfw3.h>
#include <glfw3native.h>

using namespace ImageLibrary;

void Test(std::string fPath, std::shared_ptr<GLFWwindow> window);
void GetInput(std::shared_ptr<GLFWwindow> window);

std::atomic<bool> exitLoop = false;
std::atomic<bool> safeExit = false;
std::atomic<bool> runTest = false;
std::string fPath;

void Poll(std::shared_ptr<GLFWwindow> window) {
	while (!exitLoop && !glfwWindowShouldClose(window.get())) {
		glfwPollEvents();

		if (runTest) {
			Test(fPath, window);
			runTest = false;
		}
	}
	exitLoop = true;
}

int main() {
	bool run = false;

	/* Open glfw window for displaying decoded images */
	glfwInit();
	std::shared_ptr<GLFWmonitor> monitor(glfwGetPrimaryMonitor());
	std::shared_ptr<const GLFWvidmode> mode(glfwGetVideoMode(monitor.get()));

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE); /* Compatibility to use glDrawPixels */
	std::shared_ptr<GLFWwindow> window(glfwCreateWindow(640, 480, "view", NULL, NULL));
	if (window.get() == nullptr) {
		std::cout << "Unable to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window.get());
	glViewport(0, 0, 640, 480);

	glClearColor(1.0, 0.0, 1.0, 0.5);
	glClear(GL_COLOR_BUFFER_BIT);
	glFlush();

	glfwSwapBuffers(window.get());
	std::thread th(GetInput, window);
	Poll(window);
	while (!safeExit) {}

	glfwDestroyWindow(window.get());
	glfwTerminate();
	exit(0);
}

void GetInput(std::shared_ptr<GLFWwindow> window) {
	bool run = false;

	std::string cwd = std::filesystem::current_path().generic_string();
	std::filesystem::path abs;
	while (!exitLoop) {

		std::string input;
		if (!run) {
			std::cout << "Beginning Test\n";
			std::cout << "    Change current directory (cd [fully qualified filepath | relative filepath])\n";
			std::cout << "    Run test on all files from current directory (press Enter)\n";
			std::cout << "    Run test on specific file from directory (cd [filename.ext])\n";
			std::cout << "    Run test on specific file from any directory (cd [filepath/filename.ext])\n";
			std::cout << "    Press ctrl+x and enter to exit\n" << std::endl;
			run = true;
		}
		std::cout << cwd << ">";
		safeExit = true;
		std::getline(std::cin, input);

		if (input.length() > 0 && input[0] == '\x18') {
			break;
		}

		/* parse command */
		safeExit = false;
		int index = 0;
		while (*(input.data() + index) == ' ' && index < input.length()) { index++; } /* Ignore white spaces at the start */
		input = input.substr(index, input.length() - index);


		if (input.length() == 0) {
			/* Run all tests from current directory */

			for (const auto& entry : std::filesystem::directory_iterator(cwd)) {
				if (strlen(entry.path().filename().extension().generic_string().c_str()) > 0) {
					fPath = (cwd / entry.path()).generic_string();
					runTest = true;
					//Test((cwd / entry.path()).generic_string(), window);
				}
				while (runTest) {}
			}
		}
		else {

			std::string command = input.substr(0, 3);
			std::string rest = input.substr(3, input.length() - 3);
			if (command == "cd ") {
				abs = cwd / std::filesystem::path(rest);
				abs = std::filesystem::absolute(abs);
				if (!std::filesystem::exists(abs)) {
					std::cout << "Invalid file path\n";
					continue;
				}

				/* If not to a specific file, then change cwd */
				if (strlen(abs.filename().extension().generic_string().c_str()) > 0) {
					/* Run test on file */

					fPath = abs.generic_string();
					runTest = true;
					//Test(abs.generic_string(), window);
					while (runTest) {}
				}
				else {
					/* change cwd */
					cwd = abs.generic_string();
				}

			}
			else {
				std::cout << "Invalid command\n";
			}
		}
	}

	exitLoop = true;
}

GLenum GetGLFormat(ImageLibrary::ImageData& data) {
	if ((data.format.formatting & ImageLibrary::FormatDetails::hasGray) != ImageLibrary::FormatDetails::Any) {
		if ((data.format.formatting & ImageLibrary::FormatDetails::hasAlpha) != ImageLibrary::FormatDetails::Any) {
			return GL_LUMINANCE_ALPHA;
		}
		else {
			return GL_LUMINANCE;
		}
	}
	else if ((data.format.formatting & ImageLibrary::FormatDetails::hasRGB) != ImageLibrary::FormatDetails::Any) {
		if ((data.format.formatting & ImageLibrary::FormatDetails::hasAlpha) != ImageLibrary::FormatDetails::Any) {
			return GL_RGBA;
		}
		else {
			return GL_RGB;
		}
	}
}
/* No support for sizes above 32-bit per channel */
GLenum GetGLType(ImageLibrary::ImageData& data) {
	if ((data.format.formatting & ImageLibrary::FormatDetails::has32) != ImageLibrary::FormatDetails::Any) {
		return GL_UNSIGNED_INT;
	}
	else if ((data.format.formatting & ImageLibrary::FormatDetails::has16) != ImageLibrary::FormatDetails::Any) {
		return GL_UNSIGNED_SHORT;
	}
	else if (((data.format.formatting & ImageLibrary::FormatDetails::has8) | 
		(data.format.formatting & ImageLibrary::FormatDetails::has4) | 
		(data.format.formatting & ImageLibrary::FormatDetails::has2) | 
		(data.format.formatting & ImageLibrary::FormatDetails::has1))
		!= ImageLibrary::FormatDetails::Any) {
		return GL_UNSIGNED_BYTE;
	}
}

void Test(std::string fPath, std::shared_ptr<GLFWwindow> window) {
	PNG::PNGStream<std::basic_ifstream<uint8_t, std::char_traits<uint8_t>>, Generic::Mode::Read> png(fPath);
	ImageOptions opt = { .receiveInterlaced = true, .receiveAnimation = true };
	ImageData data;
	
	bool final = false;
	while (!final) {
		ImageReturnInfo info = png.ReadData(&data, &opt);
		final = info.final;
		if (info.valid) {
			ImageStreamState state = png.QueryState();
			int breakpoint = 0;

			/* Display image and wait to get next */
			GLfloat zoomx = (float)640 / (float)data.dimensions.width;
			GLfloat zoomy = (float)480 / (float)data.dimensions.height;
			
			glRasterPos2f(-1, 1);
			glPixelZoom(zoomx, -zoomy);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glDrawPixels(data.dimensions.width, data.dimensions.height, GetGLFormat(data), GetGLType(data), data.image.data()); //only draws if on main thread
			GLenum err = glGetError();
			if (err == GL_INVALID_OPERATION) {
				glClearColor(1.0, 0.0, 1.0, 1.0);
				glClear(GL_COLOR_BUFFER_BIT);
			}
			glFlush();
			glfwSwapBuffers(window.get());

			if (!final) {
				std::cout << "enter for next image>" << std::endl;
				std::string next;
				std::getline(std::cin, next);
			}
		}
		else {
			PNG::PNGStreamState state = png.ExtQueryState();
			int breakpoint = 0;

			std::cout << "error decoding image: " << state.err << std::endl;
			break;
		}		
	}
}