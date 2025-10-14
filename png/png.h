#pragma once

#include "../interface/image-stream-interface.h"
#include "../zlib/zlib.h"
#include <unordered_map>

namespace ImageLibrary {
	namespace PNG {

		template<typename Backing, Generic::Mode mode>
		class PNGStream : Generic::Data<Backing, uint8_t, mode>, ImageStreamInterface<Backing, mode> {};

		enum class NextAction : uint8_t {
			Read_Signature = 0,
			Read_Chunks,
			Read_From_Zlib,
			Return_To_Zlib,
			Finished,
			Fatal_Error
		};

		/* Can use or operations to hold flags for each chunk (whether a chunk of that type has been found / processed before - since most should only happen once [CRC checked for all though]) 
		Critical chunks come first (including generic flag for unknown critical chunk) and ancillary chunks come after (including generic flag for unknown ancillary chunk)
		*/
		enum class ChunkFlag: unsigned int{
			NONE = 0,
			IHDR = 0b1,
			PLTE = 0b01,
			IDAT = 0b001,
			IEND = 0b0001,
			UNKNOWN_CRITICAL = 0b00001,
			tRNS = 0b000001,
			cHRM = 0b0000001,
			gAMA = 0b00000001,
			iCCP = 0b000000001,
			sBIT = 0b0000000001,
			sRGB = 0b00000000001,
			cICP = 0b000000000001,
			mDCV = 0b0000000000001,
			iTXt = 0b00000000000001,
			tEXt = 0b000000000000001,
			zTXt = 0b0000000000000001,
			bKGD = 0b00000000000000001,
			hlST = 0b000000000000000001,
			pHYs = 0b0000000000000000001,
			sPLT = 0b00000000000000000001,
			eXlf = 0b000000000000000000001,
			tIME = 0b0000000000000000000001,
			acTL = 0b00000000000000000000001,
			fcTL = 0b000000000000000000000001,
			fdAT = 0b0000000000000000000000001,
			UNKNOWN_ANCILLARY = 0b00000000000000000000000001,
		};
		inline ChunkFlag& operator|=(ChunkFlag& a, ChunkFlag b) {
			return a = (ChunkFlag)((unsigned int)a | (unsigned int)b);
		}

		enum class Filter : uint8_t {
			FILTER_NONE = 0,
			FILTER_SUB,
			FILTER_UP,
			FILTER_AVERAGE,
			FILTER_PAETH
		};

		/* Stored in little-endian (as stored in file) */
		enum class ChunkType : unsigned int {
			NONE = 0,
			IHDR = 0x52444849,
			PLTE = 0x45544C50,
			IDAT = 0x54414449,
			IEND = 0x444E4549,
			tRNS = 0x534E5274,
			cHRM = 0x4D524863,
			gAMA = 0x414D4167,
			iCCP = 0x50434369,
			sBIT = 0x54494273,
			sRGB = 0x42475273,
			cICP = 0x50434963,
			mDCV = 0x5643446D,
			iTXt = 0x74585469,
			tEXt = 0x74584574,
			zTXt = 0x7458547A,
			bKGD = 0x44474B62,
			hlST = 0x54536C68,
			pHYs = 0x73594870,
			sPLT = 0x544C5073,
			eXlf = 0x666C5865,
			tIME = 0x454D4974,
			acTL = 0x4C546361,
			fcTL = 0x4C546366,
			fdAT = 0x54416466,
		};

		enum class Color_Type : uint8_t {
			Greyscale = 0,
			Truecolor = 2,
			IndexedColor = 3,
			GreyscaleAlpha = 4,
			TruecolorAlpha = 6,
		};

		/* Contains extra error flags relating to png streams specifically */
		struct PNGStreamState : ImageStreamState {
			ChunkFlag chunkErrors = ChunkFlag::NONE;
			ChunkFlag processedChunks = ChunkFlag::NONE;
			NextAction next = NextAction::Read_Signature;
			std::vector<std::string> err_recoverable;
		};

		struct ChunkHeader {
			unsigned int length;
			ChunkType type = ChunkType::NONE;
			unsigned int CRC;
		};

		enum class Palette : uint8_t {
			Red,
			Green,
			Blue
		};

		enum class PNG_Filter : uint8_t {
			Filter_None,
			Filter_Sub,
			Filter_Up,
			Filter_Average,
			Filter_Paeth
		};

		class ReturnInterlacedPass : std::exception {};

		struct ImagePass : ImageData {
			uint8_t passNumber;
			size reduced;
		};

		template<typename Backing>
		class PNGStream<Backing, Generic::Mode::Read> : public Generic::Data<Backing, uint8_t, Generic::Mode::Read>, public ImageStreamInterface<Backing, Generic::Mode::Read> {
		private:
			const static uint64_t signature = 0x0A1A0A0D474E5089; // 0x89504E470D0A1A0A;
			const static short buffer_size = 8192;
			PNGStreamState state;
		
			zlib::ZLIBStream<Backing, Generic::Mode::Read> deflate = zlib::ZLIBStream<Backing, Generic::Mode::Read>(this);
			bool zlib_started = false;
			
			ImageData* out;
			const ImageOptions* opt;
			ImageData current;
			ImageReturnInfo currentImageInfo;
			ImageFormat baseFormat;
			Color_Type color_type;
			bool c16 = false; //flag for if channels have 16-bit samples and therefore will be stored in network-byte order

			struct PaletteEntry {
				union {
					uint8_t color[3];
					Generic::uint24_t colors;
				};	
			};
			std::vector<PaletteEntry> palette;
			uint8_t paletteBPC;

			std::unordered_map<ChunkType, ChunkHeader> chunkHistory;
			ChunkHeader prevChunk; //specifically to ensure that multiple IDAT chunks are consecutive
			ChunkHeader currentChunk;
			unsigned int crc; //will calculate crc for each chunk based on data inside and allow comparison to crc recorded in currentChunk

			/* chunk data is handled this way in case of extremely large (and/or erroneous) chunk length values */
			/* Also, for IDAT, it will be faster to chunk read (if from file, but will do this anyway) and use _current to pass data to zlib */
			uint8_t _current[buffer_size] = {};
			unsigned short _pointer = 0;
			unsigned short _max = 0;
			unsigned int _remaining_length = 0;
			unsigned int _last_read_count = 0;

			bool interlaced = false;
			std::vector<ImagePass> passes = std::vector<ImagePass>(7);
			uint8_t interlacePass = 0; /* 0-6 */
			bool iPreProcessed = false;
			PNG_Filter currentFilter;

			bool firstIDAT = true;
			short actualbpp = 0;
		private:
			void BaseRead(uint8_t* out, const int length, const bool updateCRC); //will also update current crc if needed for validation
			void CheckCRC();

			void ReadChunkHeaders();

			/* Ancillary vs Critical by checking 5th bit of first byte (ie. case of first letter) */
			void ProcessChunk();
			void ProcessAncillaryChunk();
			void ProcessCriticalChunk();
			void Loop();

			void IHDRFillMetadata();
			void PLTEGetPalette();
			void BeginReadIDAT();

			void UpdateCurrentBuffer();
			void GetNextIDAT();

			void GetUnfilteredData();

			void FlagCurrentChunk(ChunkFlag& toChange);

			template<typename Pixel>
			void FilterPass();
		public:
			using Generic::Data<Backing, uint8_t, Generic::Mode::Read>::Data; //inherit Data constructor

			ImageReturnInfo ReadData(ImageData* out, const ImageOptions* const options) override;
			ImageStreamState QueryState() override;


			/* Extension methods */
			PNGStreamState ExtQueryState();


			/* for internal use (will return compressed IDAT data to zlib decompression stream)
			whenever this is called, it will filter the deflated data from the sliding window that is about to be overwritten, before providing new data (or longjmp if it needs to return interlaced pass)
			*/
			void Read(uint8_t* out, const unsigned int length) override;
			int GetReadCount() override;
			void Seek(const unsigned int amount) override;
		};


		template<typename Backing>
		class PNGStream<Backing, Generic::Mode::Write> : public Generic::Data<Backing, uint8_t, Generic::Mode::Write>, public ImageStreamInterface<Backing, Generic::Mode::Write> {
		private:
			const static uint64_t signature = 0x89504E470D0A1A0A;
		public:
			using Generic::Data<Backing, uint8_t, Generic::Mode::Read>::Data; //inherit Data constructor

			ImageStreamState EncodeData(const ImageData& in, const ImageOptions& options) override;
			ImageStreamState QueryState();
		};
	}
}