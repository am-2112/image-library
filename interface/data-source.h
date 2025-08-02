#pragma once

#include <vector>
#include <fstream>

namespace Generic {
	enum Mode : bool {
		Read,
		Write
	};

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

		void Read(Type* out, const int length);
		int GetReadCount();

		Type Peek();
		void Seek(const unsigned int amount);
	};

	/* Write-only */
	template <typename Backing, typename Type>
	struct Data<Backing, Type, Write> {
		Backing source;

		void Write(const Type* in, const int length);
		void Flush();
	};



	/* ======= Implementations ======= */

	/* Read-only file access */
	template<typename Type>
	struct Data<std::basic_ifstream<Type, std::char_traits<Type>>, Type, Read> {
		std::basic_ifstream<Type, std::char_traits<Type>> source;

		Data(std::string filePath) : source(filePath) {}

		void Read(Type* out, const int length) {
			source.read(out, length);
		}
		int GetReadCount() {
			return source.gcount();
		}

		Type Peek() {
			return source.peek();
		}
		void Seek(const unsigned int amount) {
			source.seekg(amount);
		}
	};

	/* Write-only file access */
	template<typename Type>
	struct Data<std::basic_ofstream<Type, std::char_traits<Type>>, Type, Write> {
		std::basic_ofstream<Type, std::char_traits<Type>> source;

		Data(std::string filePath) : source(filePath) {}

		void Write(const Type* in, const int length) {
			source.write(in, length);
		}
		void Flush() {
			source.flush();
		}
	};

	/* Vector implementation */
	template<typename Type>
	struct Data<std::vector<Type>, Type, Read> {
	private:
		unsigned int current_index = 0;
		unsigned int last_read = 0;
	public:
		std::vector<Type> source;

		Data(unsigned int length) : source(length) {}
		Data(Type* ptr, unsigned int length) : source(ptr, length) {}

		void Read(Type* out, const int length) {
			if (length > source.size() - current_index)
				throw std::exception("Reading outside bounds of array!");

			unsigned int last_index = current_index;
			for (Type* ptr = out; ptr < out + length; ptr++) {
				*ptr = source[current_index++];
			}
			last_read = current_index - last_index;
		}
		int GetReadCount() {
			return last_read;
		}

		Type Peek() {
			return source[current_index];
		}
		void Seek(const unsigned int amount) {
			if (amount > source.size() - current_index)
				throw std::exception("Seeking beyond stream!");

			current_index += amount;
		}
	};

	/* Vector implementation */
	template<typename Type>
	struct Data<std::vector<Type>, Type, Write> {
	public:
		std::vector<Type> source;

		Data() : source() {}

		void Write(const Type* in, const int length) {
			source.reserve(length);
			
			for (const Type* ptr = in; ptr < in + length; ptr++) {
				source.emplace_back(*ptr);
			}
		}

		/* No need to flush anything when writing to a vector in memory */
		void Flush() {}
	};
}