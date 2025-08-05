#include "png.h"

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
				BeginReadIDAT();
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
					out->format = {
						.bitsPerPixel = bpc,
						.formatting = (FormatDetails)(0b1 | (unsigned short)bpc >> 5)
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
						.formatting = (FormatDetails)(0b0111 | (unsigned short)bpc >> 5)
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
						.formatting = (FormatDetails)(0b10001 | (unsigned short)bpc >> 5)
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
						.formatting = (FormatDetails)(0b01111 | (unsigned short)bpc >> 5)
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
			/*if (!zlib_started) {
				deflate = ZLIBStream<Backing, Generic::Mode::Read>(this);
			}*/
		}

		//as soon as _remaining_length == 0, can CheckCRC early to avoid further processing if invalid crc computed
		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::UpdateCurrentBuffer() {
			_pointer = 0;
			if (_remaining_length > buffer_size) {
				_max = buffer_size;
				BaseRead(_current, buffer_size, true);
				_remaining_length -= buffer_size;
			}
			else {
				_max = _remaining_length;
				BaseRead(_current, _remaining_length, true);
				_remaining_length = 0;
			}
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
					UpdateCurrentBuffer();
				}
				else {
					throw std::exception("Reached end of stream!");
				}
			}
			_pointer = (_pointer + amount) % buffer_size;
		}

		template<typename Backing>
		int PNGStream<Backing, Mode::Read>::GetReadCount() {
			return _last_read_count;
		}

		//may need additional template parameters for intrinsics (like in other program implementation)
		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::GetUnfilteredData() {
			while (deflate.TryRead(nullptr, 0)) {
				//defilter data and put it into return image buffer
				Filter();
				ConvertFormat();
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
		void PNGStream<Backing, Mode::Read>::Filter() {

		}

		template<typename Backing>
		void PNGStream<Backing, Mode::Read>::ConvertFormat() {

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