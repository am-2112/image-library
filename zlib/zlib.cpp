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
			case State::Decoding:
				Decode();
				break;
			case State::NewBlock:
				NewBlock();
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
			state = State::Decoding;
		}

		template<typename Backing>
		void ZLIBStream<Backing, Mode::Read>::NewBlock() {
			uint8_t header = 0;
			src.ReadBits(&header, 3);

			final = header & 0x1;
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
			bool failure = staticLengthTable.construct(staticLengths.data(), FIXLCODES);
			if (failure) { throw exception("[ZLIB] Unable to construct huffman table (static)"); }

			failure = distTable.construct(staticDistances.data(), MAXDCODES);
			if (failure) { throw exception("[ZLIB] Unable to construct huffman table (static)"); }
		}

		template<typename Backing>
		void ZLIBStream<Backing, Mode::Read>::BuildDynamic() {
			static constexpr short order[19] =
				{ 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

			short n_lengths = 0;
			uint8_t n_dist = 0;
			uint8_t n_codes = 0;
			src.ReadBits((uint8_t*)&n_lengths, 5);
			src.ReadBits((uint8_t*)&n_dist, 5);
			src.ReadBits((uint8_t*)&n_codes, 4);

			n_lengths += 257;
			n_dist += 1;
			n_codes += 4;
			if (n_lengths > MAXLCODES || n_dist > MAXDCODES) { throw exception("[ZLIB] Too many codes (dynamic)!"); }

			//read code length code lengths; missing lengths are zero
			int index = 0;
			uint8_t bit = 0;
			for (; index < n_codes; index++) {
				bit = 0;
				src.ReadBits(&bit, 3);

				lengths[order[index]] = bit;
			}
			for (; index < MAXCODELENGTHS; index++) { //if the codelengths have not all been defined, set the rest to 0 (since they must not exist)
				lengths[order[index]] = 0;
			}

			bool failure = dynamicLengthTable.construct(lengths, MAXCODELENGTHS);
			if (failure) { throw exception("[ZLIB] Unable to construct huffman table (dynamic)"); }

			//read length/literal and distance code length tables
			index = 0;
			while (index < n_lengths + n_dist) {
				int symbol; //decoded value
				int len = 0; //last length to repeat (assume 0)

				symbol = dynamicLengthTable.decode(&src);
				bool repeat = false;
				switch (symbol) {
				case -1: //invalid symbol
					throw exception("[ZLIB] Invalid symbol found (dynamic)");
					break;
				case 16: //repeat last length 3 to 6 times (fall through here)
					repeat = true;

					if (index == 0) { throw exception("[ZLIB] Invalid index into lengths (dynamic)"); }
					len = lengths[index - 1];

					symbol = 0;
					src.ReadBits((uint8_t*)&symbol, 2);
					symbol += 3;

					break;
				case 17: //repeat value 0 for 3 to 10 times
					repeat = true;

					symbol = 0;
					src.ReadBits((uint8_t*)&symbol, 3);
					symbol += 3;

					break;
				case 18: //repeat value 0 for 11 to 138 times
					repeat = true;

					symbol = 0;
					src.ReadBits((uint8_t*)&symbol, 7);
					symbol += 11;

					break;
				default: //must be under 16 (so set it to the symbol)
					lengths[index++] = symbol;
					break;
				}

				if (repeat) {
					if (index + symbol > n_lengths + n_dist) { throw exception("[ZLIB] Too many lengths (dynamic)"); }
					while (symbol--) {
						lengths[index++] = len;
					}
				}
			}

			if (lengths[256] == 0) { throw exception("[ZLIB] No end-of-block code found (dynamic)"); }

			/* Build huffman tables for literal/length codes, and distance codes */
			int err = 0;
			err = dynamicLengthTable.construct(lengths, n_lengths);
			if (err && (err < 0 || n_lengths != dynamicLengthTable.count[0] + dynamicLengthTable.count[1])) //incomplete codes ok for a single length 1 code
				throw exception("[ZLIB] Incomplete codes (dynamic)");

			err = distTable.construct(lengths + n_lengths, n_dist);
			if (err && (err < 0 || n_dist != distTable.count[0] + distTable.count[1])) //incomplete codes ok for a single length 1 code
				throw exception("[ZLIB] Incomplete codes (dynamic)");
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

		template<typename Backing>
		void ZLIBStream<Backing, Mode::Read>::Read(uint8_t* out, const unsigned int length) {
			last_read = 0;

			/* Decode more data if there is room in the sliding window */
			while (state != State::WaitingForRead && written_current_period < length && state != State::Finished) {
				Loop();
			}
			if (written_current_period >= length) {
				/* Read from sliding window */
				ReadSlidingWindow(out, length);
				if (state == State::WaitingForRead && length >= copy_amount_remaining) {
					state = State::Decoding;
				}
			}
		}

		/* Will fill available elements in sliding window 
		When it encounters eob, it will set state (NewBlock, or Finished if final == true) and then return
		When it cannot write any more (full), it will set state WaitingForRead
		*/
		template<typename Backing>
		void ZLIBStream<Backing, Mode::Read>::Decode() {
			while (written_current_period < sliding_32k) {
				if (pending_copy) {
					if (written_current_period + copy_amount_remaining > sliding_32k) {
						LengthDistPairCopy();
						pending_copy = false;
					}
					else {
						state = State::WaitingForRead;
						return;
					}
				}

				/* If stored, just return uncompressed byte; otherwise, need to decode first */
				if (type == BlockType::Stored) {
					uint8_t byte = 0;
					src.src->Read(&byte, 1);
					if (byte == 255) {
						if (final) {
							state = State::Finished;
						}
						else {
							state = State::NewBlock;
						}
						return;
					}
					else {
						Write(byte);
					}
				}
				else {
					int symbol = 0;

					if (type == BlockType::Static) {
						symbol = staticLengthTable.decode(&src);
					}
					else {
						symbol = dynamicLengthTable.decode(&src);
					}
					if (symbol == 256) {	
						if (final) {
							state = State::Finished;
						}
						else {
							state = State::NewBlock;
						}
						return;
					}
					else {
						if (symbol < 256) { //return literal
							Write(symbol);
						}
						else {
							//get and compute length
							symbol -= 257;
							if (symbol >= 29) { throw exception("[ZLIB] Invalid fixed code"); }

							uint8_t bit = 0;
							src.ReadBits(&bit, lext[symbol]);
							unsigned int len = lens[symbol] + bit;

							//get and check distance
							symbol = distTable.decode(&src);
							if (symbol < 0) { throw exception("[ZLIB] Invalid dist symbol"); }

							unsigned int dist = dists[symbol] + bit;
							if (dist > amountWritten) { throw exception("[ZLIB] Back-reference too far back"); }

							//begin copy process
							int location = write_pointer - dist;
							if (location < 0) {
								location = sliding_32k + location;
							}
							copy_amount_remaining = len;
							copyLocation = location;

							if (copy_amount_remaining >= written_current_period) {
								LengthDistPairCopy();
							}
							else {
								pending_copy = true;
								state = State::WaitingForRead;
								return;
							}
						}
					}
				}
			}
			state = State::WaitingForRead;
		}

		template<typename Backing>
		inline void ZLIBStream<Backing, Mode::Read>::Write(uint8_t byte) {
			source[write_pointer] = byte;
			write_pointer = (write_pointer + 1) % sliding_32k;
			written_current_period++;
			amountWritten == sliding_32k ? amountWritten = sliding_32k : amountWritten++;
		}

		template<typename Backing>
		void ZLIBStream<Backing, Mode::Read>::LengthDistPairCopy() {
			uint8_t copyValue = 0;
			while (copy_amount_remaining--) {
				copyValue = source[copyLocation];
				copyLocation = (copyLocation + 1) % sliding_32k;
				Write(copyValue);
			}
		}

		/* For external reads (internally, will use current_index and custom implementation of distance-copy pairs) */
		template<typename Backing>
		void ZLIBStream<Backing, Mode::Read>::ReadSlidingWindow(uint8_t* out, const unsigned int length) {
			if (length > written_current_period)
				throw exception("[ZLIB] Attempted to read outside of sliding window");

			/* Can either use 1 or 2 memcpy's depending on if pointer needs to wraparound or not */
			if (ext_pointer + length < sliding_32k) {
				memcpy(out, source.data() + ext_pointer, length);
				ext_pointer += length;
			}
			else {
				unsigned int leftInSliding = sliding_32k - ext_pointer;
				memcpy(out, source.data() + ext_pointer, leftInSliding);
				unsigned int remaining = length - leftInSliding;
				memcpy(out + leftInSliding, source.data(), remaining);
				ext_pointer = remaining;
			}

			written_current_period -= length;
			last_read += length;
		}

		template class ZLIBStream<vector<uint8_t>, Mode::Read>;
		template class ZLIBStream<basic_ifstream<uint8_t, std::char_traits<uint8_t>>, Mode::Read>;
	}
}