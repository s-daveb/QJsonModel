/* QUtf8.hpp
 * Copyright © 2024 Saul D. Beniquez
 * License:
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <QJsonValue>

namespace QUtf8Functions {
/// returns 0 on success; errors can only happen if \a u is a surrogate:
/// Error if \a u is a low surrogate;
/// if \a u is a high surrogate, Error if the next isn't a low one,
/// EndOfString if we run into the end of the string.
template<typename Traits, typename OutputPtr, typename InputPtr>
inline int
toUtf8(ushort unicodeChar, OutputPtr& dst, InputPtr& src, InputPtr end)
{
	if (!Traits::kSkipAsciiHandling && unicodeChar < 0x80) {
		// U+0000 to U+007F (US-ASCII) - one byte
		Traits::appendByte(dst, static_cast<uchar>(unicodeChar));
		return 0;
	} else if (unicodeChar < 0x0800) {
		// U+0080 to U+07FF - two bytes
		// first of two bytes
		Traits::appendByte(
		    dst, 0xc0 | static_cast<uchar>(unicodeChar >> 6)
		);
	} else {
		if (!QChar::isSurrogate(unicodeChar)) {
			// U+0800 to U+FFFF (except U+D800-U+DFFF) - three bytes
			if (!Traits::kAllowNonCharacters &&
			    QChar::isNonCharacter(unicodeChar))
				return Traits::kError;
			// first of three bytes
			Traits::appendByte(
			    dst, 0xe0 | static_cast<uchar>(unicodeChar >> 12)
			);
		} else {
			// U+10000 to U+10FFFF - four bytes
			// need to get one extra codepoint
			if (Traits::availableUtf16(src, end) == 0)
				return Traits::kEndOfString;
			ushort low = Traits::peekUtf16(src);
			if (!QChar::isHighSurrogate(unicodeChar))
				return Traits::kError;
			if (!QChar::isLowSurrogate(low))
				return Traits::kError;
			Traits::advanceUtf16(src);
			uint ucs4 = QChar::surrogateToUcs4(unicodeChar, low);
			if (!Traits::kAllowNonCharacters &&
			    QChar::isNonCharacter(ucs4))
				return Traits::kError;
			// first byte
			Traits::appendByte(
			    dst, 0xf0 | (static_cast<uchar>(ucs4 >> 18) & 0xf)
			);
			// second of four bytes
			Traits::appendByte(
			    dst, 0x80 | (static_cast<uchar>(ucs4 >> 12) & 0x3f)
			);
			// for the rest of the bytes
			unicodeChar = static_cast<ushort>(ucs4);
		}
		// second to last byte
		Traits::appendByte(
		    dst, 0x80 | (static_cast<uchar>(unicodeChar >> 6) & 0x3f)
		);
	}
	// last byte
	Traits::appendByte(dst, 0x80 | (unicodeChar & 0x3f));
	return 0;
}
inline bool isContinuationByte(uchar byte)
{
	return (byte & 0xc0) == 0x80;
}
/// returns the number of characters consumed (including \a b) in case of
/// success; returns negative in case of error: Traits::kError or
/// Traits::kEndOfString
template<typename Traits, typename OutputPtr, typename InputPtr>
inline int fromUtf8(uchar byte, OutputPtr& dst, InputPtr& src, InputPtr end)
{
	int charsNeeded;
	uint minUChar;
	uint unicodeChar;
	if (!Traits::skipAsciiHandling && byte < 0x80) {
		// US-ASCII
		Traits::appendUtf16(dst, byte);
		return 1;
	}
	if (!Traits::isTrusted && Q_UNLIKELY(byte <= 0xC1)) {
		// an UTF-8 first character must be at least 0xC0
		// however, all 0xC0 and 0xC1 first bytes can only produce
		// overlong sequences
		return Traits::kError;
	} else if (byte < 0xe0) {
		charsNeeded = 2;
		minUChar = 0x80;
		unicodeChar = byte & 0x1f;
	} else if (byte < 0xf0) {
		charsNeeded = 3;
		minUChar = 0x800;
		unicodeChar = byte & 0x0f;
	} else if (byte < 0xf5) {
		charsNeeded = 4;
		minUChar = 0x10000;
		unicodeChar = byte & 0x07;
	} else {
		// the last Unicode character is U+10FFFF
		// it's encoded in UTF-8 as "\xF4\x8F\xBF\xBF"
		// therefore, a byte higher than 0xF4 is not the UTF-8 first byte
		return Traits::kError;
	}
	int bytesAvailable = Traits::availableBytes(src, end);
	if (Q_UNLIKELY(bytesAvailable < charsNeeded - 1)) {
		// it's possible that we have an error instead of just unfinished
		// bytes
		if (bytesAvailable > 0 &&
		    !isContinuationByte(Traits::peekByte(src, 0)))
			return Traits::kError;
		if (bytesAvailable > 1 &&
		    !isContinuationByte(Traits::peekByte(src, 1)))
			return Traits::kError;
		return Traits::kEndOfString;
	}
	// first continuation character
	byte = Traits::peekByte(src, 0);
	if (!isContinuationByte(byte))
		return Traits::kError;
	unicodeChar <<= 6;
	unicodeChar |= byte & 0x3f;
	if (charsNeeded > 2) {
		// second continuation character
		byte = Traits::peekByte(src, 1);
		if (!isContinuationByte(byte))
			return Traits::kError;
		unicodeChar <<= 6;
		unicodeChar |= byte & 0x3f;
		if (charsNeeded > 3) {
			// third continuation character
			byte = Traits::peekByte(src, 2);
			if (!isContinuationByte(byte))
				return Traits::kError;
			unicodeChar <<= 6;
			unicodeChar |= byte & 0x3f;
		}
	}
	// we've decoded something; safety-check it
	if (!Traits::isTrusted) {
		if (unicodeChar < minUChar)
			return Traits::kError;
		if (QChar::isSurrogate(unicodeChar) ||
		    unicodeChar > QChar::LastValidCodePoint)
			return Traits::kError;
		if (!Traits::kAllowNonCharacters &&
		    QChar::isNonCharacter(unicodeChar))
			return Traits::kError;
	}
	// write the UTF-16 sequence
	if (!QChar::requiresSurrogates(unicodeChar)) {
		// UTF-8 decoded and no surrogates are required
		// detach if necessary
		Traits::appendUtf16(dst, static_cast<ushort>(unicodeChar));
	} else {
		// UTF-8 decoded to something that requires a surrogate pair
		Traits::appendUcs4(dst, unicodeChar);
	}
	Traits::advanceByte(src, charsNeeded - 1);
	return charsNeeded;
}
} // namespace QUtf8Functions

struct QUtf8BaseTraits {
	static const bool kIsTrusted = false;
	static const bool kAllowNonCharacters = true;
	static const bool kSkipAsciiHandling = false;
	static const int kError = -1;
	static const int kEndOfString = -2;
	static bool isValidCharacter(uint unsignedInt)
	{
		return static_cast<int>(unsignedInt) >= 0;
	}
	static void appendByte(uchar*& ptr, uchar byteVal) { *ptr++ = byteVal; }
	static uchar peekByte(const uchar* ptr, int n = 0) { return ptr[n]; }
	static qptrdiff availableBytes(const uchar* ptr, const uchar* end)
	{
		return end - ptr;
	}
	static void advanceByte(const uchar*& ptr, int n = 1) { ptr += n; }
	static void appendUtf16(ushort*& ptr, ushort unicodeChar)
	{
		*ptr++ = unicodeChar;
	}
	static void appendUcs4(ushort*& ptr, uint unicodeChar)
	{
		appendUtf16(ptr, QChar::highSurrogate(unicodeChar));
		appendUtf16(ptr, QChar::lowSurrogate(unicodeChar));
	}
	static ushort peekUtf16(const ushort* ptr, int n = 0) { return ptr[n]; }
	static qptrdiff availableUtf16(const ushort* ptr, const ushort* end)
	{
		return end - ptr;
	}
	static void advanceUtf16(const ushort*& ptr, int n = 1) { ptr += n; }
	// it's possible to output to UCS-4 too
	static void appendUtf16(uint*& ptr, ushort unicodeChar)
	{
		*ptr++ = unicodeChar;
	}
	static void appendUcs4(uint*& ptr, uint unicodeChar)
	{
		*ptr++ = unicodeChar;
	}
};

// clang-format off
// vim: set foldmethod=syntax foldminlines=10 textwidth=80 ts=8 sts=0 sw=8 noexpandtab ft=cpp.doxygen :
