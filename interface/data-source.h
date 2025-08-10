#pragma once

#include <vector>
#include <fstream>

namespace Generic {
	enum Mode : uint8_t {
		Read,
		Write,
	};

	static uint32_t ConvertEndian(const uint8_t* pointer) {
		return *pointer << 24 | *(pointer + 1) << 16 | *(pointer + 2) << 8 | *(pointer + 3);
	}

	/* ======= General Definitions ======= */

	/* Primary Template */
	template <typename Backing, typename Type, Mode mode>
	struct Data {
		Backing source;
	};

	/* Read-only */
	template <typename Backing, typename Type>
	struct Data<Backing, Type, Read> {
		Backing source;

		virtual void Read(Type* out, const unsigned int length);
		virtual bool TryRead(Type* out, const unsigned int length); //false if reaches eos
		virtual int GetReadCount();

		virtual Type Peek();
		virtual void Seek(const unsigned int amount);
		virtual void SeekBack(const unsigned int amount);
	};

	/* Write-only */
	template <typename Backing, typename Type>
	struct Data<Backing, Type, Write> {
		Backing source;

		virtual void Write(const Type* in, const unsigned int length);
		virtual void Flush();
	};



	/* ======= Implementations ======= */

	/* Read-only file access */
	template<typename Type>
	struct Data<std::basic_ifstream<Type, std::char_traits<Type>>, Type, Read> {
		std::basic_ifstream<Type, std::char_traits<Type>> source;

		Data(std::string filePath) : source(filePath, std::ios_base::binary) {}

		virtual void Read(Type* out, const unsigned int length) {
			source.read(out, length);
		}
		virtual bool TryRead(Type* out, const unsigned int length) {
			source.read(out, length);
			if (source.gcount() == length) {
				return true;
			}
			else {
				return false;
			}
		}
		virtual int GetReadCount() {
			return source.gcount();
		}

		virtual Type Peek() {
			return source.peek();
		}
		virtual void Seek(const unsigned int amount) {
			source.seekg(amount, std::ios_base::cur);
		}
		virtual void SeekBack(const unsigned int amount) {
			source.seekg(-amount, std::ios_base::cur);
		}
	};

	/* Write-only file access */
	template<typename Type>
	struct Data<std::basic_ofstream<Type, std::char_traits<Type>>, Type, Write> {
		std::basic_ofstream<Type, std::char_traits<Type>> source;

		Data(std::string filePath) : source(filePath) {}

		virtual void Write(const Type* in, const unsigned int length) {
			source.write(in, length);
		}
		virtual void Flush() {
			source.flush();
		}
	};

	/* Vector implementation */
	template<typename Type>
	struct Data<std::vector<Type>, Type, Read> {
	protected:
		unsigned int current_index = 0;
		unsigned int last_read = 0;
	public:
		std::vector<Type> source;

		Data(unsigned int length) : source(length) {}
		Data(Type* ptr, unsigned int length) : source(ptr, length) {}

		virtual void Read(Type* out, const unsigned int length) {
			unsigned int l = length;
			if (length > source.size() - current_index)
				l = source.size() - current_index;

			memcpy(out, source.data() + current_index, l);
			current_index += l;
			last_read = l;
		}
		virtual bool TryRead(Type* out, const unsigned int length) {
			Read(out, length);
			if (last_read == length) {
				return true;
			}
			else {
				return false;
			}
		}
		virtual int GetReadCount() {
			return last_read;
		}

		virtual Type Peek() {
			return source[current_index];
		}
		virtual void Seek(const unsigned int amount) {
			if (amount > source.size() - current_index)
				throw std::exception("Seeking beyond stream!");

			current_index += amount;
		}
		virtual void SeekBack(const unsigned int amount) {
			if (amount > current_index)
				throw std::exception("Seeking beyond stream!");

			current_index -= amount;
		}
	};

	/* Vector implementation */
	template<typename Type>
	struct Data<std::vector<Type>, Type, Write> {
	public:
		std::vector<Type> source;

		Data() : source() {}

		virtual void Write(const Type* in, const unsigned int length) {
			source.reserve(length);
			
			for (const Type* ptr = in; ptr < in + length; ptr++) {
				source.emplace_back(*ptr);
			}
		}

		/* No need to flush anything when writing to a vector in memory */
		virtual void Flush() {}
	};



	/* ======= Extensions ======= */
	template<typename Backing, typename Type>
	struct BitReader {
	protected:
		uint8_t byte = 0; //for storing partial reads
		uint8_t bit_pointer = 0; //0-7 indexing individual bits
		bool bytePresent = false; //set if partial byte stored
	public:
		Data<Backing, Type, Read>* src;
	public:
		BitReader(Data<Backing, Type, Read>* source) : src(source) {};

		void ReadBits(uint8_t* out, const unsigned int bitsNeeded) {
			unsigned int fullBytes = 0;
			uint8_t bitsPresent = 0;
			if (bytePresent) { bitsPresent = 8 - bit_pointer; }

			unsigned int bitsLeft = bitsNeeded;
			uint8_t remaining_bits = 0;

			/* Fast byte aligned copy to *out */
			if (bitsNeeded > bitsPresent) {
				fullBytes = (bitsNeeded - bitsPresent) / 8;
				bitsLeft = (fullBytes * 8) + bitsPresent;
				remaining_bits = bitsNeeded - bitsLeft;

				if (fullBytes > 0) {
					src->Read(out, fullBytes);
					if (src->GetReadCount() < fullBytes) {
						throw std::exception("Unable to read enough from source");
					}
				}
			}
			else {
				bitsPresent = bitsNeeded;
			}

			uint8_t* copyEnd = out + fullBytes;
			if (bytePresent) {
				if (fullBytes > 0) {
					/* shift copied data if necessary */
					uint8_t* currentPtr = copyEnd - 1;
					while (currentPtr > out) {
						uint8_t store = *(currentPtr - 1);
						*(currentPtr + 1) |= store >> (8 - bitsPresent); /* Shifting more significant bits to next byte */
						*currentPtr = (store << bitsPresent); /* Shifting less significant bits up in current byte */

						currentPtr--;
					}
				}


				/* Insert aligned data from present byte (using bit_pointer) */
				for (uint8_t out_bit_pointer = 0; out_bit_pointer < bitsPresent; out_bit_pointer++) {
					int current_bit = (byte >> bit_pointer++) & 0x1;
					*out |= (current_bit << out_bit_pointer);
				}
			}
			out = copyEnd;

			/* Read remaining bits (not aligned) */
			if (remaining_bits > 0) {
				src->Read(&byte, 1);
				if (src->GetReadCount() == 0) {
					throw std::exception("Unable to read enough from source");
				}
				bit_pointer = 0;
				bytePresent = true;

				for (uint8_t out_bit_pointer = bitsPresent; out_bit_pointer < remaining_bits + bitsPresent; out_bit_pointer++) {
					if (out_bit_pointer != 0 && out_bit_pointer % 8 == 0) {
						out++;
					}

					int current_bit = (byte >> bit_pointer++) & 0x1;
					*out |= (current_bit << (out_bit_pointer % 8));
				}
			}
		}

		void ResetBitPointer() {
			byte = 0;
			bit_pointer = 0;
			bytePresent = false;
		}
	};
}