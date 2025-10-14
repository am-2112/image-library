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
			if (Data<Backing, uint8_t, Mode::Read>::GetReadCount() != length) {
				throw exception("Unable to read from stream");
			}

			if (updateCRC) {
				//crc = currentChunk.CRC; //temp (until crc is actually calculated)
			}
		}
		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::CheckCRC() {
			BaseRead((uint8_t*)&currentChunk.CRC, 4, false);
			crc = currentChunk.CRC; //temp
			if (currentChunk.CRC != crc) {  //if CRCs do not match, file may be corrupted
				throw std::exception("CRC Mismatch");
			}
		}

		template<typename Backing>
		ImageReturnInfo PNGStream<Backing, Mode::Read>::ReadData(ImageData* out, const ImageOptions* const options) {
			this->out = out;
			opt = options;

			currentImageInfo.valid = true;
			currentImageInfo.final = false;

			try {
				try {
					while (state.next != NextAction::Finished) {
						Loop();
					}
				}
				catch (ReturnInterlacedPass i) { //return interlaced pass if this exception is passed
					if (state.next != NextAction::Finished) {
						state.next = NextAction::Return_To_Zlib;
						currentImageInfo.valid = true;
						return currentImageInfo;
					}
				}
			}
			catch (std::exception e) {
				state.next = NextAction::Fatal_Error;
				FlagCurrentChunk(state.chunkErrors);
				state.hasError = true;
				state.isFatalError = true;
				state.err = string("[PNG] Location: ") + to_string((unsigned int)currentChunk.type) + string(" ") + e.what();
				currentImageInfo.valid = false;
				return currentImageInfo;
			}


			/* Managing state & error reporting before returning */
			if (state.chunkErrors != ChunkFlag::NONE) {
				state.hasError = true;
				if (state.next != NextAction::Fatal_Error) {
					state.isFatalError = false;
					state.err = string("[PNG] Recoverable Error in chunk: ") + to_string((unsigned int)currentChunk.type);
					currentImageInfo.valid = false;
				}
			}
			if (state.next == NextAction::Finished) {
				currentImageInfo.final = true;
				currentImageInfo.valid = true;
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
				Data<Backing, uint8_t, Mode::Read>::Read((uint8_t*)&sig, 8);
				if (sig != signature) {
					throw std::exception("Invalid Signature");
				}
				state.next = NextAction::Read_Chunks;
				break;
			case NextAction::Read_Chunks:
				ReadChunkHeaders();
				ProcessChunk();
				break;
			case NextAction::Read_From_Zlib:
				GetUnfilteredData();
				break;
			case NextAction::Return_To_Zlib:
				state.next = NextAction::Read_Chunks;
				GetUnfilteredData();
				break;
			default:
				break;
			}
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::ReadChunkHeaders() {
			BaseRead((uint8_t*)&currentChunk.length, 4, false);
			BaseRead((uint8_t*)&currentChunk.type, 4, true); //crc computed over chunk type and data (not length)
			currentChunk.length = Generic::ConvertEndian((uint8_t*)&currentChunk.length);
			int breakpoint = 0;

			//if length is fixed (ie. currentChunk.length shouldn't be used), then this should not happen
			//as a result, this should probably only be called in chunks that rely on the length data
			//Data<Backing, uint8_t, Mode::Read>::Seek(currentChunk.length);
			//BaseRead((uint8_t*)&currentChunk.CRC, 4, false); //should put this into CheckCRC once crc calculations are implemented (to avoid seeking back & forth)
			//Data<Backing, uint8_t, Mode::Read>::SeekBack(currentChunk.length); //is the crc skipped at end of chunk? (need to check all chunk implementations)
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::ProcessChunk() {
			if (currentChunk.type != ChunkType::IHDR && !chunkHistory.contains(ChunkType::IHDR)) { throw std::exception("IHDR not first chunk!"); } //IHDR always first
			if (chunkHistory.contains(ChunkType::IEND)) { throw std::exception("IEND should be last!"); } //IEND should always be last
			if (!firstIDAT && currentChunk.type == ChunkType::IDAT && prevChunk.type != ChunkType::IDAT) { throw exception("IDAT chunks should be next to each other"); }

			bool isCritical = !((unsigned int)currentChunk.type & 0x00000020); //if first letter uppercase, chunk is critical (5th bit)
			if (isCritical) {
				ProcessCriticalChunk();
			}
			else {
				ProcessAncillaryChunk();
			}
			FlagCurrentChunk(state.processedChunks); //not called for IDAT or unknown ancillary (gAMA)
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::ProcessCriticalChunk() {
			if (chunkHistory.contains(currentChunk.type)) { throw std::exception("Duplicate chunk type"); }
			
			switch (currentChunk.type) {
			case ChunkType::IHDR:
				//if (currentChunk.length != 0) { throw std::exception("unexpected field"); }
				IHDRFillMetadata();
				CheckCRC();
				break;
			case ChunkType::PLTE:
				PLTEGetPalette();
				CheckCRC();
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
			BaseRead((uint8_t*)&current.dimensions.width, 4, true);
			BaseRead((uint8_t*)&current.dimensions.height, 4, true);
			current.dimensions.width = Generic::ConvertEndian((uint8_t*)&current.dimensions.width);
			current.dimensions.height = Generic::ConvertEndian((uint8_t*)&current.dimensions.height);

			uint8_t bpc = 0;
			Color_Type color_t = (Color_Type)0;
			BaseRead(&bpc, 1, true);
			BaseRead((uint8_t*)&color_t, 1, true);
			color_type = color_t;

			if (bpc == 16) {
				c16 = true;
			}

			switch (color_t) {
			case Color_Type::Greyscale:
				if (!(bpc == 1 || bpc == 2 || bpc == 4 || bpc == 8 || bpc == 16)) {
					throw std::exception("Invalid Format Settings");
				}
				else {
					actualbpp = bpc;
					if (bpc < 8) { bpc = 8; } //even if bpc 1, will widen to 8 (ie. 1 byte) in the stream
					current.format = {
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
					actualbpp = bpc * 3u;
					current.format = {
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
					paletteBPC = bpc;
					actualbpp = 24;
					current.format = {
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
					actualbpp = bpc * 2u;
					current.format = {
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
					actualbpp = bpc * 4u;
					current.format = {
						.bitsPerPixel = bpc * 4u,
						.formatting = (FormatDetails)(0b01111 << 8 | (unsigned short)bpc)
					};
				}
				break;
			}

			uint8_t compression = 0;
			BaseRead(&compression, 1, true);
			if (compression != 0) { throw std::exception("Unsupported compression method found"); } //only compression 0 is defined by spec and supported by this decoder

			uint8_t filter = 0;
			BaseRead(&filter, 1, true);
			if (filter != 0) { throw std::exception("Unsupported filter found"); } //only filter 0 is defined by spec and supported by this decoder

			uint8_t interlacing = 0;
			BaseRead(&interlacing, 1, true);
			if (interlacing > 1) { throw std::exception("Unsupported interlacing method found"); }
			interlaced = interlacing; //should be interlaced for i file name scheme (something not right here)
			currentImageInfo.isInterlaced = interlaced;
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
			state.next = NextAction::Read_From_Zlib;
		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::GetNextIDAT() {
			FlagCurrentChunk(state.processedChunks);
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
					CheckCRC();
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
				unsigned int stop = _max;
				if (stop - _pointer > length) {
					stop = _pointer + length;
				}

				unsigned int amountWritten = stop - _pointer;
				for (; _pointer < stop; _pointer++) {
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

			uint8_t actualReadBpp = actualbpp;
			if (palette.size() > 0) {
				actualReadBpp = paletteBPC;
			}

			if (!interlaced) {
				out->image = vector<uint8_t>(out->dimensions.width * out->dimensions.height * ((actualbpp + 7) / 8));
			}

			Pixel* target = (Pixel*)out->image.data();
			uint24_t* rgbTarget = (uint24_t*)out->image.data();

			/* If pixel is smaller than byte, make an array to hold the bit values (loop backwards since left = high-order bits) */
			vector<uint8_t> pixelBits(0);
			if (actualReadBpp != sizeof(Pixel) * 8) {
				pixelBits = vector<uint8_t>(8 / actualReadBpp);
			}

			uint8_t bitAnd = 0;
			switch (actualReadBpp) {
			case 1:
				bitAnd = 1;
				break;
			case 2:
				bitAnd = 3;
				break;
			case 4:
				bitAnd = 15;
				break;
			default:
				break;
			}

			/* Do loop runs once per interlace pass (or only once for non-interlaced) 
			A lot of the stuff may be able to go outside this do loop
			*/
			do {

				width = out->dimensions.width;
				height = out->dimensions.height;
				int rwidth = width;

				if (interlaced) {
					width = passes[interlacePass].dimensions.width;
					height = passes[interlacePass].dimensions.height;
				}

				if (width == 0) { /* Empty interlace pass */
					passes[interlacePass] = passes[interlacePass - 1];

					/* Set output to this if receiving interlaced stuff, but don't explicitly return (only return right at end, or when new non-empty pass) */
					if ((interlaced && opt->receiveInterlaced) || (interlaced && interlacePass == 6)) {
						out->dimensions.width = passes[interlacePass].dimensions.width;
						out->dimensions.height = passes[interlacePass].dimensions.height;
						out->image = passes[interlacePass].image;
					}
					if (interlaced && interlacePass == 6) {
						currentImageInfo.final = true;
						state.next = NextAction::Finished;
						interlacePass++;
						throw ReturnInterlacedPass();
					}

					continue;
				}

				unsigned int rowIncrement = 1;
				unsigned int colIncrement = 1;

				unsigned int totalPixels = width * height;
				unsigned int currentPixelI = 1; /* 1-indexed here */
				unsigned int currentRow = 0;
				unsigned int currentPixelStart = 1;
				if (interlaced) {
					width = passes[interlacePass].dimensions.width;
					height = passes[interlacePass].dimensions.height;
					rwidth = passes[interlacePass].reduced.width;
					totalPixels = passes[interlacePass].reduced.width * passes[interlacePass].reduced.height;
					target = (Pixel*)passes[interlacePass].image.data();
					rgbTarget = (uint24_t*)passes[interlacePass].image.data();

					/* Put pixels from previous pass into correct place in this image's pass & prepare new pass indexing */
					if (interlacePass > 0) {
						Pixel* prevPass = (Pixel*)passes[interlacePass - 1].image.data();
						uint24_t* prevRgbPass = (uint24_t*)passes[interlacePass - 1].image.data();

						if (interlacePass == 1 || interlacePass == 3 || interlacePass == 5) {
							/* These passes insert columns */
							colIncrement = 2;
							currentPixelI = 2; //new data on odd col
							currentPixelStart = 2;

							unsigned int prevIndex = 0;
							for (unsigned int cRow = 0; cRow < height; cRow++) {
								for (unsigned int cCol = 0; cCol < width; cCol+= 2) { /* original data on even col */		
									if (palette.size() == 0) {
										target[(cRow * width) + cCol] = prevPass[prevIndex++];
									}
									else {
										rgbTarget[(cRow * width) + cCol] = prevRgbPass[prevIndex++];
									}
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
									if (palette.size() == 0) {
										target[(cRow * width) + cCol] = prevPass[prevIndex++];
									}
									else {
										rgbTarget[(cRow * width) + cCol] = prevRgbPass[prevIndex++];
									}
								}
							}
						}
					}
				}


				std::vector<uint8_t> prevRow(sizeof(__m128i) * (rwidth + 1)); /* +1, since will index (currentPixelI - 1) for upper left */
				Pixel* prevRowPixels = (Pixel*)prevRow.data();
				__m128i* prevRowView = (__m128i*)prevRow.data();

				std::vector<uint8_t> prev(sizeof(__m128i));
				Pixel* prevPixel = (Pixel*)prev.data();
				__m128i* prevPixelView = (__m128i*)prev.data();

				std::vector<uint8_t> current(sizeof(__m128i));
				Pixel* currentPixel = (Pixel*)current.data();
				__m128i* currentPixelView = (__m128i*)current.data();

				std::vector<uint8_t> result(sizeof(__m256i));
				__m256i* fullResultView = (__m256i*)result.data();
				__m128i* narrowResultView = (__m128i*)result.data();

				/*for narrowing to m128i from m256i*/
				__m256i shuffle = _mm256_setr_epi8(
					0, 2, 4, 6, 8, 10, 12, 14,
					-1, -1, -1, -1, -1, -1, -1, -1,
					16, 18, 20, 22, 24, 26, 28, 30,
					-1, -1, -1, -1, -1, -1, -1, -1);

				unsigned int readAmount = sizeof(uint8_t); /* Set to read filter byte */
				bool newColumn = true;
				while (deflate.TryRead(current.data(), readAmount)) {				
					readAmount = sizeof(Pixel);

					int loop = pixelBits.size() - 1;
					do {
						totalPixels--;

						if (newColumn) {
							newColumn = false;
							currentFilter = (PNG_Filter)current[0];
							if (!deflate.TryRead(current.data(), readAmount)) {
								throw exception("No data for new scanline");
							}
						}

						/* If actualbpp < 8, then will loop to use up bits in current.data accordingly (make a copy first and then re-set current.data with padded byte of bits value?)
						If then results in new scanline, ignore the rest of the bits in current byte
						The left-most pixel is in the high-order bits
						*/
						if (actualReadBpp != readAmount * 8) {
							/* Put data into pixelBits vector in correct order (low-order bits first, since looping pixelBits from back to front) */
							if (loop == pixelBits.size() - 1) {
								uint8_t index = 0;
								for (int i = 0; i < 8; i += actualReadBpp) {
									pixelBits[index] = (current[0] & (bitAnd << i)) >> i;

									/* Scale up by using left bit replication
									Begin by left shifting, then repeating most significant bits into the open bits
									*/
									if (palette.size() == 0) { /*shouldn't replicate for palette indices (not sure if this should be skipped for other things too or not)*/
										pixelBits[index] = pixelBits[index] << (8 - actualReadBpp);
										for (int j = 0; j < 8 / actualReadBpp; j++) {
											pixelBits[index] |= pixelBits[index] >> actualReadBpp;
										}
									}
									index++;
								}
							}

							current[0] = pixelBits[loop--];
						}
						else if (c16) { //reverse bit order for endianness (for 16-bit channel samples since 16-bit samples are stored in network byte order (MSB first)
							uint8_t temp[sizeof(Pixel)];

							for (int i = 0; i < sizeof(Pixel); i+=2) {
								temp[i] = current[i + 1];
								temp[i + 1] = current[i];
							}
							for (int i = 0; i < sizeof(Pixel); i++) { //copy temp to current
								current[i] = temp[i];
							}
						}

						
						switch (currentFilter) {
						case PNG_Filter::Filter_None:
							break;
						case PNG_Filter::Filter_Sub:
							*currentPixelView = _mm_add_epi8(*currentPixelView, *prevPixelView);
							break;
						case PNG_Filter::Filter_Up:
							*currentPixelView = _mm_add_epi8(*currentPixelView, *(prevRowView + 1)); //prevRowView points to upper left
							break;
						case PNG_Filter::Filter_Average: {
							/* Widen first (not allowing overflow until the final addition) */
							__m256i a = _mm256_cvtepu8_epi16(*prevPixelView);
							__m256i b = _mm256_cvtepu8_epi16(*(prevRowView + 1));
							__m256i pix = _mm256_cvtepu8_epi16(*currentPixelView);

							__m256i avg = _mm256_avg_epu16(a, b);
							*fullResultView = _mm256_add_epi8(pix, avg); //allow overflow here

							*fullResultView = _mm256_shuffle_epi8(*fullResultView, shuffle);
							*fullResultView = _mm256_permute4x64_epi64(*fullResultView, 8);

							*currentPixelView = *narrowResultView;
							break;
						}
						case PNG_Filter::Filter_Paeth: {
							/* Widen first (not allowing overflow until the final addition) */
							__m256i a = _mm256_cvtepu8_epi16(*prevPixelView);
							__m256i b = _mm256_cvtepu8_epi16(*(prevRowView + 1));
							__m256i c = _mm256_cvtepu8_epi16(*prevRowView);

							__m256i pix = _mm256_cvtepu8_epi16(*currentPixelView);

							__m256i pa = _mm256_sub_epi16(b, c);
							__m256i pb = _mm256_sub_epi16(a, c);
							__m256i pc = _mm256_sub_epi16(pa, pb);

							pa = _mm256_abs_epi16(pa);
							pb = _mm256_abs_epi16(pb);
							pc = _mm256_abs_epi16(pc);

							__m256i not_pa_le_pb = _mm256_cmpgt_epi16(pa, pb); //!(pa <= pb)
							__m256i not_pa_le_pc = _mm256_cmpgt_epi16(pa, pc); //!(pa <= pc)
							__m256i not_pb_le_pc = _mm256_cmpgt_epi16(pb, pc); //!(pb <= pc)

							__m256i not_take_a = _mm256_or_si256(not_pa_le_pb, not_pa_le_pc); //if pa > pb or pa > pc, don't take a (remember the signs are switched from <= to >)
							__m256i t = _mm256_blendv_epi8(b, c, not_pb_le_pc); //take b or c depending on whether or not !(pb <= pc) -- the second if statement

							__m256i chosen = _mm256_blendv_epi8(a, t, not_take_a); //final comparison here (if not_take_a then it will take either b or c)
							*fullResultView = _mm256_add_epi8(pix, chosen); //allow overflow here

							*fullResultView = _mm256_shuffle_epi8(*fullResultView, shuffle); //cannot use shuffle for the entire conversion, since they repeat across 32-bit boundaries
							*fullResultView = _mm256_permute4x64_epi64(*fullResultView, 8);

							*currentPixelView = *narrowResultView;
							break;
						}
						default:
							throw exception("Invalid filter type found!");
						}

						/* Write pixel to output */
						if (palette.size() == 0) {
							target[(currentRow * width) + (currentPixelI - 1)] = *currentPixel;
						}
						else { /*is palette index (resulting color will always be rgb8)*/
							if (current[0] >= palette.size())
								throw exception("Invalid palette index!");
							rgbTarget[(currentRow * width) + (currentPixelI - 1)] = palette[current[0]].colors;
						}
						

						/* Set prevRow (upper left), prevPixel */
						*prevRowView = *prevPixelView;
						prevRowView++; //not using rowIncrement because filtering works based off of prev scanline in same pass (ie. not complete image)

						*prevPixelView = *currentPixelView;

						currentPixelI += colIncrement;

						if (currentPixelI > width) {
							currentPixelI = currentPixelStart;
							currentRow += rowIncrement;
							newColumn = true;
							readAmount = sizeof(uint8_t); /* Set to read filter byte next iteration */
							*prevRowView = *prevPixelView;
							prevRowView = (__m128i*)prevRow.data();

							memset(prev.data(), 0, sizeof(__m128i));
							memset(pixelBits.data(), 0, pixelBits.size());
							loop = -1; //break out of loop (ignore low-order bits)
							memset(current.data(), 0, sizeof(__m128i)); //current[0] = 0;


							/* Then, set the ImageData image to the current pass (if receiveInterlaced; otherwise, only do this for the last pass (pass 7)) */
							if (totalPixels == 0 && ((interlaced && opt->receiveInterlaced) || (interlaced && interlacePass == 6))) {
								out->dimensions.width = passes[interlacePass].dimensions.width;
								out->dimensions.height = passes[interlacePass].dimensions.height;
								out->image = passes[interlacePass].image;

								if (interlacePass == 6) {
									currentImageInfo.final = true;
									state.next = NextAction::Finished;
								}
								interlacePass++;
								throw ReturnInterlacedPass();
							}
						}
					} while (loop >= 0);
				}

				if (totalPixels != 0) {
					throw exception("Not enough image data!");
				}

			} while (interlacePass < 7 && interlaced);
		}

		/* This should only be called once, after the first IDAT chunk header has been parsed */
		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::GetUnfilteredData() {
			state.next = NextAction::Read_Chunks;
			*out = current; /* Copy image details from current */

			unsigned int width = current.dimensions.width;
			unsigned int height = current.dimensions.height;
			if (interlaced && !iPreProcessed) {
				/* Give correct width & height and resize image vector, for each interlace pass (including setting width = 0 for empty passes) */
				iPreProcessed = true;

				for (int pass = 0; pass < 7; pass++) {
					passes[pass].dimensions.width = 1;
				}

				if (width < 5) {
					passes[1].dimensions.width = 0;
					if (width < 3) {
						passes[3].dimensions.width = 0;
						if (width < 2) {
							passes[5].dimensions.width = 0;
						}
					}
				}
				if (height < 5) {
					passes[2].dimensions.width = 0;
					if (height < 3) {
						passes[4].dimensions.width = 0;
						if (height < 2) {
							passes[6].dimensions.width = 0;
						}
					}
				}

				/* Calculate width & height for non-empty passes */
				ImageData prevPass;
				for (int pass = 0; pass < 7; pass++) {
					/* Get reduced count */
					unsigned int storeWidth = width;
					unsigned int storeHeight = height;

					if (passes[pass].dimensions.width == 0) {
						continue;
					}
					else {
						passes[pass].dimensions.width == 0;
					}

					switch (pass) {
					case 0:
						passes[pass].reduced.width = (storeWidth + 7) / 8;
						passes[pass].reduced.height = (storeHeight + 7) / 8;
						passes[pass].dimensions = passes[pass].reduced;
						break;
					case 1:
						storeWidth -= 4; //offset
						passes[pass].reduced.width = ((storeWidth + 7) / 8);
						passes[pass].reduced.height = prevPass.dimensions.height;
						passes[pass].dimensions = passes[pass].reduced;
						passes[pass].dimensions.width += prevPass.dimensions.width;
						break;
					case 2:
						storeHeight -= 4;
						passes[pass].reduced.width = prevPass.dimensions.width;
						passes[pass].reduced.height = ((storeHeight + 7) / 8);
						passes[pass].dimensions = passes[pass].reduced;
						passes[pass].dimensions.height += prevPass.dimensions.height;
						break;
					case 3:
						storeWidth -= 2;
						passes[pass].reduced.width = ((storeWidth +3) / 4);
						passes[pass].reduced.height = prevPass.dimensions.height;
						passes[pass].dimensions = passes[pass].reduced;
						passes[pass].dimensions.width += prevPass.dimensions.width;
						break;
					case 4:
						storeHeight -= 2;
						passes[pass].reduced.width = prevPass.dimensions.width;
						passes[pass].reduced.height = ((storeHeight + 3) / 4);
						passes[pass].dimensions = passes[pass].reduced;
						passes[pass].dimensions.height += prevPass.dimensions.height;
						break;
					case 5:
						storeWidth -= 1;
						passes[pass].reduced.width = ((storeWidth + 1) / 2);
						passes[pass].reduced.height = prevPass.dimensions.height;
						passes[pass].dimensions = passes[pass].reduced;
						passes[pass].dimensions.width += prevPass.dimensions.width;
						break;
					case 6:
						storeHeight -= 1;
						passes[pass].reduced.width = prevPass.dimensions.width;
						passes[pass].reduced.height = ((storeHeight + 1) / 2);
						passes[pass].dimensions = passes[pass].reduced;
						passes[pass].dimensions.height += prevPass.dimensions.height;
						break;
					}

					passes[pass].passNumber = pass;
					passes[pass].image = vector<uint8_t>(passes[pass].dimensions.width * passes[pass].dimensions.height * (current.format.bitsPerPixel / 8));
					prevPass = passes[pass];
				}
			}

			unsigned short bytesPerPixel = current.format.bitsPerPixel / 8;
			if (palette.size() > 0)
				bytesPerPixel = 1; //max palette range is 1-255 so 8 bit depth max
			switch (bytesPerPixel) {
			case 1:
				FilterPass<uint8_t>();
				break;
			case 2:
				FilterPass<uint16_t>();
				break;
			case 3:
				FilterPass<uint24_t>();
				break;
			case 4:
				FilterPass<uint32_t>();
				break;
			case 6:
				FilterPass<uint48_t>();
				break;
			case 8:
				FilterPass<uint64_t>();
				break;
			case 16:
				FilterPass<__m128i>();
				break;
			}

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
			switch (currentChunk.type) { //problem with |= operator
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
				bool isCritical = (((unsigned int)currentChunk.type & 0x000000FF) >> 3) & 0x1; //if first letter uppercase, chunk is critical (5th bit)
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