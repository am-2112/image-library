//implememting specification outlined in https://www.ietf.org/rfc/rfc1951.txt (DEFLATE Compressed Data Format Specification version 1.3)
/* (remember to put huffman code in Generic namespace - maybe also create a folder and new .h and .cpp files for the huffman stuff too?) */

#include "../interface/data-source.h"
#include <csetjmp>
#include "../huffman/huffman.h"
#include <array>

namespace ImageLibrary {
	namespace zlib {
		const static unsigned short sliding_32k = 32768;
		const static unsigned short clamp_32k = 32767;

		enum class State : uint8_t {
			Init,
			Finished,
			Decoding,
			NewBlock,
			WaitingForRead,
		};

		enum class BlockType : uint8_t {
			Stored,
			Static,
			Dynamic
		};

		/* Backing determines the backing buffer for the source to the zlibstream */
		template<typename Backing, Generic::Mode mode>
		class ZLIBStream : Generic::Data<std::vector<uint8_t>, uint8_t, mode> {};

		template<typename Backing>
		class ZLIBStream<Backing, Generic::Mode::Read> : Generic::Data<std::vector<uint8_t>, uint8_t, Generic::Mode::Read> {
		private:
			Generic::BitReader<Backing, uint8_t> src;

			static const short MAXLCODES = 286; //max number of literal/length codes
			static const short MAXDCODES = 30; //max number of distance codes
			static const short MAXCODES = MAXLCODES + MAXDCODES;
			static const short MAXCODELENGTHS = 19;
			static const short FIXLCODES = 288; //number of fixed literal/length codes
			static const short MAXBITS = 16;

			short lengths[MAXCODES] = {};
			static constexpr std::array<short, FIXLCODES> staticLengths{ []() consteval {
				std::array<short, FIXLCODES> result{};
				int symbol;
				for (symbol = 0; symbol < 144; symbol++) {
					result[symbol] = 8;
				}
				for (; symbol < 256; symbol++) {
					result[symbol] = 9;
				}
				for (; symbol < 280; symbol++) {
					result[symbol] = 7;
				}
				for (; symbol < FIXLCODES; symbol++) {
					result[symbol] = 8;
				}
				return result;
			}() };
			static constexpr std::array<short, MAXDCODES> staticDistances{ []() consteval {
				std::array<short, MAXDCODES> result{};
				int symbol;
				for (symbol = 0; symbol < MAXDCODES; symbol++) {
					result[symbol] = 5;
				}
				return result;
			}() };
			/*short lenCount[MAXBITS] = {}, lenSymbolsStatic[FIXLCODES] = {}, lenSymbolsDynamic[MAXLCODES] = {};
			short distCount[MAXBITS] = {}, distSymbols[MAXDCODES] = {}; */

			static constexpr const short lens[29] = { //size base for length codes 257..285 
				3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
				35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
			static constexpr const short lext[29] = { //extra bits for length codes 257..285
				0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
				3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
			static constexpr const short dists[30] = { //offset base for distance codes 0..29 (dist at least 1 for a length dist pair)
				1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
				257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
				8193, 12289, 16385, 24577 };
			static constexpr const short dext[30] = { //extra bits for distance codes 0..29
				0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
				7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
				12, 12, 13, 13
			};

			unsigned int ext_pointer = 0;
			unsigned int write_pointer = 0;
			unsigned int written_current_period = 0; /* Goes down as external reads from sliding window */
			unsigned int max = 0;
			uint8_t byte = 0; //for storing partial reads
			uint8_t bit_pointer = 0; //0-7 indexing individual bits
			bool bytePresent = false; //set if partial byte stored

			State state;
			BlockType type;
			Generic::huffman::Huffman<MAXBITS, FIXLCODES> staticLengthTable;
			Generic::huffman::Huffman<MAXBITS, MAXLCODES> dynamicLengthTable;
			Generic::huffman::Huffman<MAXBITS, MAXDCODES> distTable;

			bool pending_copy = false;
			unsigned int copy_amount_remaining = 0;
			unsigned int copyLocation = 0;

			bool final = false;
			unsigned long long amountWritten = 0;

			unsigned short literalDataLength = 0;
		private:
			void Loop();

			void Init();
			void NewBlock();

			void BuildStatic();
			void BuildDynamic();	

			void Decode();
			void LengthDistPairCopy();
			inline void Write(uint8_t byte);

			void ReadSlidingWindow(uint8_t* out, const unsigned int length);
		public:
			/* Gets source to compressed data and constructs 32kb sliding window */
			ZLIBStream(Generic::Data<Backing, uint8_t, Generic::Mode::Read>* source) : Generic::Data<std::vector<uint8_t>, uint8_t, Generic::Mode::Read>(sliding_32k), src(source) {};

			void Read(uint8_t* out, const unsigned int length) override;
			bool TryRead(uint8_t* out, const unsigned int length) override;
		};

		template<typename Backing>
		class ZLIBStream<Backing, Generic::Mode::Write> : Generic::Data<std::vector<uint8_t>, uint8_t, Generic::Mode::Write> {

		};
	}
}