#pragma once

#include <vector>
#include <string>

namespace ImageLibrary {
	/* This is defining the user-side data interface for the image library */
	
	/* Mapped to values such that: 
	First 5 bits represent color channels: Grey | Red | Green | Blue | Alpha
	Next 8 bits represent number of bits per channel: 128 | 64 | 32 | 16 | 8 | 4 | 2 | 1
	Any other format will be converted to this style before returning to the user
	The order of color channels will also be converted to follow the ordering of the bits above (Grey, RGBA in that order)
	*/
	enum class FormatDetails : unsigned short {
		Any = 0,
		Gray1 = 0b1000000000001,
		Gray2 = 0b1000000000010,
		Gray4 = 0b1000000000100,
		Gray8 = 0b1000000001000,
		Gray16 = 0b1000000010000,
		GrayAlpha8 = 0b1000100001000,
		GrayAlpha16 = 0b1000100010000,
		RGB8 = 0b0111000001000,
		RGB16 = 0b0111000010000,
		RGBA8 = 0b0111100001000,
		RGBA16 = 0b0111100010000,
	};

	struct size {
		uint32_t width;
		uint32_t height;
	};

	struct ImageFormat {
		unsigned short bitsPerPixel = 0;
		FormatDetails formatting = FormatDetails::Any;
	};	

	struct ImageData {
		std::vector<uint8_t> image;
		size dimensions;
		ImageFormat format;
	};

	/* Add optional ImageFormat target later (once the functionality has been added) */
	struct ImageOptions {
		/* Specifies whether to receive interlaced and/or animated images (read-only streams) */
		bool receiveInterlaced;
		bool receiveAnimation;
		//ImageFormat target;

	};

	enum class AnimationFinish : uint8_t {
		FINISHED_CLEAR_TO_BLACK,
		REPLACED_BY_PREVIOUS,
		DRAWN_OVER_BY_NEXT
	};

	struct AnimationInfo {
		bool hasAnimation = false;
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
		bool final = false;
	};

	struct ImageStreamState {
		bool hasError;
		bool isFatalError;

		/* Should be for the most recent and most fatal error; individual streams can inherit this state for more detailed flags */
		std::string err; 
	};
}