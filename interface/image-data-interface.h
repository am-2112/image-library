#pragma once

#include <vector>
#include <string>

namespace ImageLibrary {
	struct ImageData {
		std::vector<uint8_t> image;
		size_t height;
		size_t stride;

	};

	struct ImageFormat {
		int bitsPerPixel;

	};

	struct ImageOptions {
		/* Specifies whether to receive interlaced and/or animated images (read-only streams) */
		bool receiveInterlaced;
		bool receiveAnimation;


	};

	struct ImageReturnInfo {
		bool valid;
		bool isInterlaced;
		bool hasAnimation;
		int animationID;
	};

	struct ImageStreamState {
		bool valid;
		std::string err;
	};
}