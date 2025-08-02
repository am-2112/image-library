#pragma once

#include "image-data-interface.h"
#include "data-source.h"

namespace ImageLibrary {
	template<typename Backing, Generic::Mode mode>
	class ImageStreamInterface abstract {};

	template<typename Backing>
	class ImageStreamInterface<Backing, Generic::Mode::Read> abstract {
	public:
		/* To receive animations, will need to call ReadData until stream data is fully consumed  
		If receiving interlaced images, successive calls to ReadData will give higher quality images until fully parsed
		*/
		template<typename WriteBacking>
		virtual ImageReturnInfo ReadData(const Generic::Data<WriteBacking, uint8_t, Generic::Mode::Read>& input, ImageData& data, const ImageOptions& options) = 0;
		virtual ImageStreamState QueryState() = 0;
	};


	template<typename Backing>
	class ImageStreamInterface<Backing, Generic::Mode::Read> abstract {
	public:
		template<typename WriteBacking>
		virtual ImageStreamState EncodeData(Generic::Data<WriteBacking, uint8_t, Generic::Mode::Write>& output, const ImageOptions& options) = 0;
		virtual ImageStreamState QueryState() = 0;
	};
}