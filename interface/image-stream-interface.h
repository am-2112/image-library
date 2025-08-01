#pragma once

#include "image-data-interface.h"

namespace ImageLibrary {
	class ImageStreamInterface abstract {

		/* To receive animations, will need to call ReadData until stream data is fully consumed  
		If receiving interlaced images, successive calls to ReadData will give higher quality images until fully parsed
		*/
		virtual ImageReturnInfo ReadData(std::string filePath, ImageData& data, bool receiveInterlaced = false, bool receiveAnimation) = 0;
		virtual ImageStreamState QueryState() = 0;
	};
}