#pragma once

#include "image-data-interface.h"
#include "data-source.h"

namespace ImageLibrary {
	/* This is defining the user-side interface for the image library */
	/* The implementation streams will inherit from Generic::Data, which will be used to provide the data needed (or write access needed) */

	template<typename Backing, Generic::Mode mode>
	class ImageStreamInterface abstract {};

	template<typename Backing>
	class ImageStreamInterface<Backing, Generic::Mode::Read> abstract {
	public:
		/* To receive animations, will need to call ReadData until stream data is fully consumed  
		If receiving interlaced images, successive calls to ReadData will give higher quality images until fully parsed
		*/
		virtual ImageReturnInfo ReadData(ImageData* out, const ImageOptions* const options) = 0;
		virtual ImageStreamState QueryState() = 0;
	};


	template<typename Backing>
	class ImageStreamInterface<Backing, Generic::Mode::Write> abstract {
	public:
		virtual ImageStreamState EncodeData(const ImageData& in, const ImageOptions& options) = 0;
		virtual ImageStreamState QueryState() = 0;
	};
}