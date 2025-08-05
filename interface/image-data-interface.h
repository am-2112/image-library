#pragma once

#include <vector>
#include <string>

namespace ImageLibrary {
	/* This is defining the user-side data interface for the image library */
	
	/* Mapped to values such that: 
	First 5 bits represent color channels: Grey | Red | Green | Blue | Alpha
	Next bits represent number of bits per channel: 1 | 2 | 4 | 8 | 16 | 32 | 64 | 128
	Any other format will be converted to this style before returning to the user
	The order of color channels will also be converted to follow the ordering of the bits above (Grey, RGBA in that order)
	*/
	enum class FormatDetails : unsigned short {
		Gray1 = 0b100001,
		Gray2 = 0b1000001,
		Gray4 = 0b10000001,
		Gray8 = 0b100000001,
		Gray16 = 0b1000000001,
		GrayAlpha8 = 0b100010001,
		GrayAlpha16 = 0b1000100001,
		RGB8 = 0b011100001,
		RGB16 = 0b0111000001,
		RGBA8 = 0b011110001,
		RGBA16 = 0b0111100011,
	};

	struct ImageFormat {
		unsigned short bitsPerPixel;
		FormatDetails formatting;
	};	

	struct ImageData {
		std::vector<uint8_t> image;
		size_t height;
		size_t width;
		ImageFormat format;
	};

	struct ImageOptions {
		/* Specifies whether to receive interlaced and/or animated images (read-only streams) */
		bool receiveInterlaced;
		bool receiveAnimation;
		ImageFormat target;

	};

	enum class AnimationFinish : uint8_t {
		FINISHED_CLEAR_TO_BLACK,
		REPLACED_BY_PREVIOUS,
		DRAWN_OVER_BY_NEXT
	};

	struct AnimationInfo {
		bool hasAnimation;
		unsigned int animationID;
		int relativeX;
		int relativeY;
		bool applyTransparency;
		AnimationFinish overAction;
	};

	struct ImageReturnInfo {
		bool valid;
		bool isInterlaced;
		AnimationInfo animInfo;
		bool final;
	};

	struct ImageStreamState {
		bool hasError;
		bool isFatalError;

		/* Should be for the most recent and most fatal error; individual streams can inherit this state for more detailed flags */
		std::string err; 
	};
}