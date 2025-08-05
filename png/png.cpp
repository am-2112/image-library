#include "png.h"
#include <bit>
#include <xmmintrin.h>
#include <immintrin.h>

using namespace Generic;
using namespace std;

namespace ImageLibrary {
	/* For fatal errors, may want to use jmp to get out of the zlib code and back to the png stream that owns it (since if fatal, cannot continue processing) */
	namespace PNG {

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::BaseRead(uint8_t* out, const int length, const bool updateCRC) {
			Data<Backing, uint8_t, Mode::Read>::Read(out, length);

			if (updateCRC) {
				crc = currentChunk.CRC; //temp (until crc is actually calculated)
			}
		}
		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::CheckCRC() {
			if (currentChunk.CRC != crc) {  //if CRCs do not match, file may be corrupted
				throw std::exception("CRC Mismatch");
			}
		}

		template<typename Backing>
		ImageReturnInfo PNGStream<Backing, Mode::Read>::ReadData(ImageData* out, const ImageOptions* const options) {
			this->out = out;
			opt = options;

			try {
				try {
					while (state.next != NextAction::Finished) {
						Loop();
					}
				}
				catch (ReturnInterlacedPass i) { //return interlaced pass if this exception is passed
					state.next = NextAction::Return_To_Zlib;
					return currentImageInfo;
				}
			}
			catch (std::exception e) {
				state.next = NextAction::Fatal_Error;
				FlagCurrentChunk(state.chunkErrors);
				state.hasError = true;
				state.isFatalError = true;
				state.err = string("[PNG] Location: ") + to_string((unsigned int)currentChunk.type) + string(" ") + e.what();
				return currentImageInfo;
			}


			/* Managing state & error reporting before returning */
			if (state.chunkErrors != ChunkFlag::NONE) {
				state.hasError = true;
				if (state.next != NextAction::Fatal_Error) {
					state.isFatalError = false;
					state.err = string("[PNG] Recoverable Error in chunk: ") + to_string((unsigned int)currentChunk.type);
				}
			}
			if (state.next == NextAction::Finished) {
				currentImageInfo.final = true;
			}

			return currentImageInfo;
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::Loop() {
			if (currentChunk.type != ChunkType::NONE) {
				chunkHistory[currentChunk.type] = currentChunk;
				prevChunk = currentChunk;
			}

			uint64_t sig = 0;
			switch (state.next) {
			case NextAction::Read_Signature:
				Data<Backing, uint8_t, Mode::Read>::Read((uint8_t*)sig, 8);
				if (sig != signature) {
					throw std::exception("Invalid Signature");
				}
				state.next = NextAction::Read_Chunks;
				break;
			case NextAction::Read_Chunks:
				ReadChunkHeaders();
				ProcessChunk();
				CheckCRC();
				break;
			case NextAction::Read_From_Zlib:
				GetUnfilteredData();
				break;
			case NextAction::Return_To_Zlib:
				state.next = NextAction::Read_Chunks;
				
				break;
			default:
				break;
			}
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::ReadChunkHeaders() {
			BaseRead((uint8_t*)currentChunk.length, 4, false);
			BaseRead((uint8_t*)currentChunk.type, 4, true); //crc computed over chunk type and data (not length)

			Data<Backing, uint8_t, Mode::Read>::Seek(currentChunk.length);
			BaseRead((uint8_t*)currentChunk.CRC, 4, false); //should put this into CheckCRC once crc calculations are implemented (to avoid seeking back & forth)
			Data<Backing, uint8_t, Mode::Read>::SeekBack(currentChunk.length);
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::ProcessChunk() {
			if (currentChunk.type != ChunkType::IHDR && !chunkHistory.contains(ChunkType::IHDR)) { throw std::exception("IHDR not first chunk!"); } //IHDR always first
			if (chunkHistory.contains(ChunkType::IEND)) { throw std::exception("IEND should be last!"); } //IEND should always be last
			if (!firstIDAT && currentChunk.type == ChunkType::IDAT && prevChunk.type != ChunkType::IDAT) { throw exception("IDAT chunks should be next to each other"); }

			bool isCritical = ((unsigned int)currentChunk.type << 5) & 0x1; //if first letter uppercase, chunk is critical (5th bit)
			if (isCritical) {
				ProcessCriticalChunk();
			}
			else {
				ProcessAncillaryChunk();
			}
			FlagCurrentChunk(state.processedChunks);
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::ProcessCriticalChunk() {
			if (chunkHistory.contains(currentChunk.type)) { throw std::exception("Duplicate chunk type"); }
			if (currentChunk.length != 0) { throw std::exception("unexpected field"); } //according to spec, any unexpected fields in critical chunks should be treated as fatal errors
			
			switch (currentChunk.type) {
			case ChunkType::IHDR:
				IHDRFillMetadata();
				break;
			case ChunkType::PLTE:
				PLTEGetPalette();
				break;
			case ChunkType::IDAT:
				if (firstIDAT) {
					BeginReadIDAT();
				}
				else {
					GetNextIDAT(); /* Shouldn't be called again through this, just a failsafe */
				}
				break;
			case ChunkType::IEND:
				state.next = NextAction::Finished;
				break;
			default:
				throw std::exception("Found unknown critical chunk!");
			}
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::IHDRFillMetadata() {	
			BaseRead((uint8_t*)out->width, 4, true);
			BaseRead((uint8_t*)out->height, 4, true);

			uint8_t bpc = 0;
			Color_Type color_t = (Color_Type)0;
			BaseRead(&bpc, 1, true);
			BaseRead((uint8_t*)color_t, 1, true);
			color_type = color_t;

			switch (color_t) {
			case Color_Type::Greyscale:
				if (!(bpc == 1 || bpc == 2 || bpc == 4 || bpc == 8 || bpc == 16)) {
					throw std::exception("Invalid Format Settings");
				}
				else {
					if (bpc < 8) { bpc = 8; } //even if bpc 1, will widen to 8 (ie. 1 byte) in the stream
					out->format = {
						.bitsPerPixel = bpc,
						.formatting = (FormatDetails)(0b1 << 12 | (unsigned short)bpc)
					};
				}
				break;
			case Color_Type::Truecolor:
				if (!(bpc == 8 || bpc == 16)) {
					throw std::exception("Invalid Format Settings");
				}
				else {
					out->format = {
						.bitsPerPixel = bpc * 3u,
						.formatting = (FormatDetails)(0b0111 << 9 | (unsigned short)bpc)
					};
				}
				break;
			case Color_Type::IndexedColor:
				if (!(bpc == 1 || bpc == 2 || bpc == 4 || bpc == 8)) {
					throw std::exception("Invalid Format Settings");
				}
				else {
					out->format = {
						.bitsPerPixel = 24, //always 8bpc for indexed
						.formatting = FormatDetails::RGB8
					};
				}
				break;
			case Color_Type::GreyscaleAlpha:
				if (!(bpc == 8 || bpc == 16)) {
					throw std::exception("Invalid Format Settings");
				}
				else {
					out->format = {
						.bitsPerPixel = bpc * 2u,
						.formatting = (FormatDetails)(0b10001 << 8 | (unsigned short)bpc)
					};
				}
				break;
			case Color_Type::TruecolorAlpha:
				if (!(bpc == 8 || bpc == 16)) {
					throw std::exception("Invalid Format Settings");
				}
				else {
					out->format = {
						.bitsPerPixel = bpc * 4u,
						.formatting = (FormatDetails)(0b01111 << 8 | (unsigned short)bpc)
					};
				}
				break;
			}

			uint8_t filter = 0;
			BaseRead(&filter, 1, true);
			if (filter != 0) { throw std::exception("Unsupported filter found"); } //only filter 0 is defined by spec and supported by this decoder

			uint8_t interlacing = 0;
			BaseRead(&interlacing, 1, true);
			if (interlacing > 1) { throw std::exception("Unsupported interlacing method found"); }
			interlaced = interlacing;
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::PLTEGetPalette() {
			if (currentChunk.length % 3 != 0) { //length should be divisible by 3
				if (color_type == Color_Type::IndexedColor) {
					throw std::exception("Invalid palette for indexed format"); //only fatal for indexed (since valid chunk is required)
				}
				else {
					FlagCurrentChunk(state.chunkErrors);
					return;
				}
			}
			if (color_type == Color_Type::Greyscale || color_type == Color_Type::GreyscaleAlpha) { FlagCurrentChunk(state.chunkErrors); return; } //flag non-fatal error and skip

			unsigned int pSize = currentChunk.length / 3;
			palette = vector<PaletteEntry>(pSize);

			for (int i = 0; i < pSize; i++) {
				BaseRead(palette[i].color, 3, true); //read the 3 bytes of data into palette (corresponding to rgb)
			}
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::BeginReadIDAT() {
			_remaining_length = currentChunk.length;
			UpdateCurrentBuffer();
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::GetNextIDAT() {
			ReadChunkHeaders();
			if (currentChunk.type == ChunkType::IDAT) {
				_remaining_length = currentChunk.length;
			}
			else {
				ProcessChunk();
			}
		}

		//as soon as _remaining_length == 0, can CheckCRC early to avoid further processing if invalid crc computed
		/* This will loop through as many chunks as needed to fill _current, or run out of chunks (ie. _remaining_length stays at 0) */
		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::UpdateCurrentBuffer() {
			_pointer = 0;
			_max = 0;
			do {
				if (_remaining_length == 0) {
					GetNextIDAT();
				}

				unsigned int amount = _remaining_length;
				if (_remaining_length > buffer_size)
					amount = buffer_size;

				if (_remaining_length != 0) {
					BaseRead(_current + _max, amount, true);
					_max += amount;
					_remaining_length -= amount;
				}
				else {
					break; /* No IDAT chunks left (remaining length wasn't updated after running loop) */
				}
			} while (_max != buffer_size);
		}

		/* Will also set _last_read_count for how much data it was able to read from _current (including after buffer updates) */
		//also, change length to an unsigned int
		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::Read(uint8_t* out, const unsigned int length) {
			_last_read_count = 0;
			unsigned int currentLength = length;
			while (currentLength > 0) {
				unsigned int amountWritten = _max - _pointer;
				for (; _pointer < _max; _pointer++) {
					*out = _current[_pointer];
					out++;
				}

				currentLength -= amountWritten;
				_last_read_count += amountWritten;
				if (currentLength != 0) {
					if (_max == buffer_size) {
						UpdateCurrentBuffer();
					}
					else {
						throw std::exception("Reached end of stream!");
					}
				}
			}
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::Seek(const unsigned int amount) {
			unsigned int remaining = amount;
			while (_pointer + remaining > _max) { /* While loop in case of large seeks requiring multiple buffer updates (shouldn't ever happen though) */
				if (_max == buffer_size) {
					remaining -= (_max - _pointer);
					_pointer = 0;
					UpdateCurrentBuffer();
				}
				else {
					throw std::exception("Reached end of stream!");
				}
			}
			_pointer = (_pointer + remaining) % buffer_size;
		}

		template<typename Backing>
		int PNGStream<Backing, Mode::Read>::GetReadCount() {
			return _last_read_count;
		}

		/* Largest format is 128bpp so accounting for overflow, need to use mm256i vector operations and then shorten back to mm128i 
		If options set to receive interlaced images, will break early by throwing exception to return pass
		If not interlaced, ignores the pass given in and will just loop for width & height given in out
		*/
		template<typename Backing>
		template<typename Pixel> void PNGStream<Backing, Generic::Read>::FilterPass() {
			unsigned int width;
			unsigned int height;
			Pixel* target = (Pixel*)out->image.data();

			/* Do loop runs once per interlace pass (or only once for non-interlaced) */
			do {

				width = out->width;
				height = out->height;

				if (width == 0) { /* Empty interlace pass */
					passes[interlacePass] = passes[interlacePass - 1];

					/* Set output to this if receiving interlaced stuff, but don't explicitly return (only return right at end, or when new non-empty pass) */
					if ((interlaced && opt->receiveInterlaced) || (interlaced && interlacePass == 6)) {
						out->width = passes[interlacePass].width;
						out->height = passes[interlacePass].height;
						out->image = passes[interlacePass].image;
					}

					continue;
				}

				unsigned int rowIncrement = 1;
				unsigned int colIncrement = 1;

				unsigned int totalPixels = width * height;
				unsigned int currentPixelI = 1; /* 1-indexed here */
				unsigned int currentRow = 0;
				if (interlaced) {
					width = passes[interlacePass].width;
					height = passes[interlacePass].height;
					target = (Pixel*)passes[interlacePass].image.data();

					/* Put pixels from previous pass into correct place in this image's pass & prepare new pass indexing */
					if (interlacePass > 0) {
						Pixel* prevPass = (Pixel*)passes[interlacePass - 1].image.data();

						if (interlacePass == 1 || interlacePass == 3 || interlacePass == 5) {
							/* These passes insert columns */
							colIncrement = 2;
							currentPixelI = 2; //new data on odd col

							unsigned int prevIndex = 0;
							for (unsigned int cRow = 0; cRow < height; cRow++) {
								for (unsigned int cCol = 0; cCol < width; cCol+= 2) { /* original data on even col */		
									target[(cRow * width) + cCol] = prevPass[prevIndex++];
								}
							}

						}
						else {
							/* These passes insert rows */
							rowIncrement = 2;
							currentRow = 1; //new data on odd rows

							unsigned int prevIndex = 0;
							for (unsigned int cRow = 0; cRow < height; cRow+= 2) { /* original data on even row */
								for (unsigned int cCol = 0; cCol < width; cCol ++) {
									target[(cRow * width) + cCol] = prevPass[prevIndex++];
								}
							}
						}
					}
				}

				std::vector<uint8_t> prevRow(sizeof(__m128i) * (width + 1)); /* +1, since will index (currentPixelI - 1) for upper left */
				Pixel* prevRowPixels = (Pixel*)prevRow.data();
				__m128i* prevRowView = (__m128i*)prevRow.data();

				std::vector<uint8_t> prev(sizeof(__m128i));
				Pixel* prevPixel = (Pixel*)prev.data();
				__m128i* prevPixelView = (__m128i*)prev.data();

				std::vector<uint8_t> current(sizeof(__m128i));
				Pixel* currentPixel = (Pixel*)current.data();
				__m128i* currentPixelView = (__m128i*)current.data();

				unsigned int readAmount = sizeof(uint8_t); /* Set to read filter byte */
				bool newColumn = true;
				while (deflate.TryRead(current.data(), readAmount)) {
					totalPixels--;
					readAmount = sizeof(Pixel);

					if (newColumn) {
						currentFilter = (PNG_Filter)current[0];
						if (!deflate.TryRead(current.data(), readAmount)) {
							throw exception("No data for new scanline");
						}
					}

					switch (currentFilter) {
					case PNG_Filter::Filter_None:
						break;
					case PNG_Filter::Filter_Sub:

						break;
					case PNG_Filter::Filter_Up:
						/* remember upper left using -rowIncrement */

						break;
					case PNG_Filter::Filter_Paeth:
						/* remember upper left using -rowIncrement */

						break;
					}

					/* Write pixel to output */
					target[(currentRow * width) + currentPixelI] = *currentPixel;

					/* Set prevRow (upper left), prevPixel */
					*prevRowView = *prevPixelView;
					prevRowView += rowIncrement;

					*prevPixelView = *currentPixelView;

					currentPixelI += colIncrement;

					if (currentPixelI > width) {
						currentPixelI = 1;
						currentRow += rowIncrement;
						newColumn = true;
						readAmount = sizeof(uint8_t); /* Set to read filter byte next iteration */
						prevRowView = (__m128i*)prevRow.data();

						memset(&prev, 0, sizeof(__m128i));
					}
				}

				if (totalPixels != 0) {
					throw exception("Not enough image data!");
				}

				/* Then, set the ImageData image to the current pass (if receiveInterlaced; otherwise, only do this for the last pass (pass 7)) */
				if ((interlaced && opt->receiveInterlaced) || (interlaced && interlacePass == 6)) {
					out->width = passes[interlacePass].width;
					out->height = passes[interlacePass].height;
					out->image = passes[interlacePass].image;
					throw ReturnInterlacedPass();
				}

				interlacePass++;
			} while (interlacePass < 7);
		}

		/* This should only be called once, after the first IDAT chunk header has been parsed */
		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::GetUnfilteredData() {
			state.next = NextAction::Read_Chunks;

			unsigned int width = out->width;
			unsigned int height = out->height;
			if (interlaced && iPreProcessed) {
				/* Give correct width & height and resize image vector, for each interlace pass (including setting = 0 for empty passes)
				*/
				iPreProcessed = false;

				if (width < 5) {
					passes[1].width = 0;
					if (width < 3) {
						passes[3].width = 0;
						if (width < 2) {
							passes[5].width = 0;
						}
					}
				}
				if (height < 5) {
					passes[2].width = 0;
					if (height < 3) {
						passes[4].width = 0;
						if (height < 2) {
							passes[6].width = 0;
						}
					}
				}

				/* Calculate width & height for non-empty passes */
				unsigned int runningWidth = 0;
				unsigned int runningHeight = 0;
				for (int pass = 0; pass < 7; pass++) {
					/* Get reduced count */
					unsigned int storeWidth = width;
					unsigned int storeHeight = height;

					if (passes[pass].width == 0) {
						continue;
					}

					switch (pass) {
					case 0:
						passes[pass].width = (storeWidth + 7) / 8; /* ceil */
						passes[pass].height = (storeHeight + 7) / 8;
						break;
					case 1:
						storeWidth -= 4; //offset
						passes[pass].width = (storeWidth + 7) / 8;
						passes[pass].height = (storeHeight + 7) / 8;
						break;
					case 2:
						storeHeight -= 4;
						passes[pass].width = (storeWidth + 1) / 2;
						passes[pass].height = (storeHeight + 7) / 8;
						break;
					case 3:
						storeWidth -= 2;
						passes[pass].width = (storeWidth +3) / 4;
						passes[pass].height = (storeHeight +3) / 4;
						break;
					case 4:
						storeHeight -= 2;
						passes[pass].width = (storeWidth + 1) / 2;
						passes[pass].height = (storeHeight + 3) / 4;
						break;
					case 5:
						storeWidth -= 1;
						passes[pass].width = (storeWidth + 1) / 2;
						passes[pass].height = (storeHeight + 1) / 2;
						break;
					case 6:
						storeHeight -= 1;
						passes[pass].width = storeWidth;
						passes[pass].height = (storeHeight + 1) / 2;
						break;
					}

					/* Add to Running total */
					if (pass > 0) {
						passes[pass].width += runningWidth;
						passes[pass].height += runningHeight;

						runningWidth = passes[pass].width;
						runningHeight = passes[pass].height;
					} 
				}
			}

			unsigned short bytesPerPixel = out->format.bitsPerPixel / 8;
			switch (bytesPerPixel) {
			case 1:
				FilterPass<uint8_t>();
				break;
			case 2:
				FilterPass<uint16_t>();
				break;
			case 4:
				FilterPass<uint32_t>();
				break;
			case 8:
				FilterPass<uint64_t>();
				break;
			case 16:
				FilterPass<__m128i>();
				break;
			}


			/* while (deflate.TryRead(nullptr, 0)) {
				//defilter data and put it into return image buffer
				Filter();
				ConvertFormat();
			}*/
			state.next = NextAction::Finished;
		}



		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::ProcessAncillaryChunk() {
			switch (currentChunk.type) {
			default:
				Data<Backing, uint8_t, Mode::Read>::Seek(currentChunk.length + 4); //to make processing chunks faster, skip unknown ones and don't check the CRC either
			}
		}



		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::FlagCurrentChunk(ChunkFlag& toChange) {
			switch (currentChunk.type) {
			case ChunkType::IHDR:
				toChange |= ChunkFlag::IHDR;
				break;
			case ChunkType::PLTE:
				toChange |= ChunkFlag::PLTE;
				break;
			case ChunkType::IDAT:
				toChange |= ChunkFlag::IDAT;
				break;
			case ChunkType::IEND:
				toChange |= ChunkFlag::IEND;
				break;
			case ChunkType::tRNS:
				toChange |= ChunkFlag::tRNS;
				break;
			case ChunkType::cHRM:
				toChange |= ChunkFlag::cHRM;
				break;
			case ChunkType::gAMA:
				toChange |= ChunkFlag::gAMA;
				break;
			case ChunkType::iCCP:
				toChange |= ChunkFlag::iCCP;
				break;
			case ChunkType::sBIT:
				toChange |= ChunkFlag::sBIT;
				break;
			case ChunkType::sRGB:
				toChange |= ChunkFlag::sRGB;
				break;
			case ChunkType::cICP:
				toChange |= ChunkFlag::cICP;
				break;
			case ChunkType::mDCV:
				toChange |= ChunkFlag::mDCV;
				break;
			case ChunkType::iTXt:
				toChange |= ChunkFlag::iTXt;
				break;
			case ChunkType::tEXt:
				toChange |= ChunkFlag::tEXt;
				break;
			case ChunkType::zTXt:
				toChange |= ChunkFlag::zTXt;
				break;
			case ChunkType::bKGD:
				toChange |= ChunkFlag::bKGD;
				break;
			case ChunkType::hlST:
				toChange |= ChunkFlag::hlST;
				break;
			case ChunkType::pHYs:
				toChange |= ChunkFlag::pHYs;
				break;
			case ChunkType::sPLT:
				toChange |= ChunkFlag::sPLT;
				break;
			case ChunkType::eXlf:
				toChange |= ChunkFlag::eXlf;
				break;
			case ChunkType::tIME:
				toChange |= ChunkFlag::tIME;
				break;
			case ChunkType::acTL:
				toChange |= ChunkFlag::acTL;
				break;
			case ChunkType::fcTL:
				toChange |= ChunkFlag::fcTL;
				break;
			case ChunkType::fdAT:
				toChange |= ChunkFlag::fdAT;
				break;
			case ChunkType::NONE:
				break;
			default:
				bool isCritical = ((unsigned int)currentChunk.type << 5) & 0x1; //if first letter uppercase, chunk is critical (5th bit)
				if (isCritical) {
					toChange |= ChunkFlag::UNKNOWN_CRITICAL;
				}
				else {
					toChange |= ChunkFlag::UNKNOWN_ANCILLARY;
				}
			}
		}

		template<typename Backing>
		ImageStreamState PNGStream<Backing, Mode::Read>::QueryState() {
			return state;
		}
		template<typename Backing>
		PNGStreamState PNGStream<Backing, Mode::Read>::ExtQueryState() {
			return state;
		}



		/* Explicit template instantiations */
		template class PNGStream<vector<uint8_t>, Mode::Read>;
		template class PNGStream<basic_ifstream<uint8_t, std::char_traits<uint8_t>>, Mode::Read>;
	}
}