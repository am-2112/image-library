#include "zlib.h"

using namespace Generic;
using namespace std;

namespace ImageLibrary {
	namespace zlib {

		

		template<typename Backing>
		void ZLIBStream<Backing, Mode::Read>::Loop() {
			switch (state) {
			case State::Init:
				Init();
				break;
			default:
				break;
			}
		}

		template<typename Backing>
		void ZLIBStream<Backing, Mode::Read>::Init() {
			/* Begin by reading in header data */
			uint8_t CMF = 0; 
			src.src->Read(&CMF, 1);
			uint8_t FLG = 0;
			src.src->Read(&FLG, 1);

			uint8_t CM = CMF & 0xF; /* First 4 bytes; should be value 8 to denote DEFLATE compression method */
			if (CM != 8) { throw std::exception("[ZLIB] Unknown zlib compression method!"); }
			uint8_t CINFO = (CMF & 0xF0) >> 4; /* sliding window size (not needed to be read here) */

			uint8_t FCHECK = FLG & 0x1F; //check bits for CMF and FLG
			uint8_t FDICT = FLG & 0x20 >> 5; //preset dictionary; if present, need to seek past (only needed for encoding)
			uint8_t FLEVEL = FLG & 0xC0 >> 6; //compression level (also not needed)

			uint16_t check = ((uint16_t)CMF * 256) + FLG;
			if (check % 31 != 0) { throw std::exception("[ZLIB] Failed bit check!"); }
			if (FDICT) { src.src->Seek(4); } //skip dictionary
			NewBlock();
		}

		template<typename Backing>
		void ZLIBStream<Backing, Mode::Read>::NewBlock() {
			uint8_t header = 0;
			src.ReadBits(&header, 3);

			bool final = header & 0x1;
			if (final) {
				state = State::Finished;
				return;
			}
			type = (BlockType)((header & 0x6) >> 1);

			if (type == BlockType::Static) {
				BuildStatic();
			}
			else if (type == BlockType::Dynamic) {
				BuildDynamic();
			}
		}

		template<typename Backing>
		void ZLIBStream<Backing, Mode::Read>::BuildStatic() {

		}

		template<typename Backing>
		void ZLIBStream<Backing, Mode::Read>::BuildDynamic() {

		}

		/* 
		PNGStream will handle chunk file reading, and this ZLIBstream will call read many times to get individual bits and stuff (so _read is necessary)
		It has to be done this way since reading in bits may cause distance-copies, don't want to overwrite any data
		Also write a function to read from this specific data buffer (internal) - since the overriden one is for ext.
		Also write a function to write to this specific data buffer (internal)
		*/

		template<typename Backing>
		bool ZLIBStream<Backing, Mode::Read>::TryRead(uint8_t* out, const unsigned int length) {
			Read(out, length);
			if (last_read == length) {
				return true;
			}
			else {
				return false;
			}
		}

		/* May be easier to pull this code out into a state machine style loop function 
		This function will return uncompressed bytes from the sliding window 
		Need to remember to set last_read based on how much data was gathered
		Also, don't throw errors if stream finished (just return with last_read = 0)
		*/
		template<typename Backing>
		void ZLIBStream<Backing, Mode::Read>::Read(uint8_t* out, const unsigned int length) {
			
		}


		template class ZLIBStream<vector<uint8_t>, Mode::Read>;
		template class ZLIBStream<basic_ifstream<uint8_t, std::char_traits<uint8_t>>, Mode::Read>;
	}
}