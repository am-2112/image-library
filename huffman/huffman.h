#pragma once

#include "../interface//data-source.h"

namespace Generic {
	namespace huffman {
		template<unsigned short max_count_size, unsigned short max_symbol_size>
		struct Huffman {
			short count[max_count_size] = {};
			short symbol[max_symbol_size] = {};

			int construct(const short* codeLengths, const unsigned int codeLengthsSize = max_symbol_size) {
				int symbol;
				int length;
				int codesLeft;
				short offs[max_count_size] = {};

				/* Count number of codes of each length */
				for (symbol = 0; symbol < codeLengthsSize; symbol++) {
					count[codeLengths[symbol]]++;
				}
				if (count[0] == codeLengthsSize) { //no codes
					return 0; //complete, but decode will fail
				}

				/* Check for over-subscribed or incomplete set of length */
				codesLeft = 1;
				for (length = 1; length < max_count_size; length++) {
					codesLeft <<= 1;
					codesLeft -= count[length];
					if (codesLeft < 0) { return codesLeft; } //over-subscribed
				}

				/* Generate offsets into symbol table for each length for sorting */
				offs[1] = 0;
				for (length = 1; length < max_count_size - 1; length++) {
					offs[length + 1] = offs[length] + count[length]; //running total
				}

				/* Put symbols in table - sorted by length, by symbol order within each length */
				for (symbol = 0; symbol < codeLengthsSize; symbol++) {
					if (codeLengths[symbol] != 0) { //ignore symbols of 0 count
						this->symbol[offs[codeLengths[symbol]]++] = symbol;
					}
				}

				return codesLeft;
			}

			template<typename Backing, typename Type>
			int decode(BitReader<Backing, Type>* ms) {
				int len; //current number of bits in code
				int code; //len bits being decoded
				int first; //first code of length len
				int count; //number of codes of length len
				int index; //index of first code of length len in symbol table

				code = first = index = 0;
				uint8_t bit = 0;
				for (len = 1; len < max_count_size; len++) {
					bit = 0;
					ms->ReadBits(&bit, 1);
					code |= bit;
					count = this->count[len];
					if (code - count < first) {
						return symbol[index + (code - first)];
					}

					index += count;
					first += count;
					first <<= 1;
					code <<= 1;
				}

				return -1; //out of codes
			}
		};
	}
}