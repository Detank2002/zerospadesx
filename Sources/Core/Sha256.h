/*
 Copyright (c) 2026 ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace spades {
	/** SHA-256 (FIPS 180-4), fed in pieces. */
	class Sha256 {
	public:
		using Digest = std::array<std::uint8_t, 32>;

		Sha256();
		void Update(const void* data, std::size_t size);
		/** The digest of everything fed so far. The object is spent afterwards. */
		Digest Finish();

		/** The digest as 64 lower-case hex digits, as sha256sum prints it. */
		static std::string ToHex(const Digest& digest);

		/** The digest of a whole file, or false if it cannot be read. */
		static bool OfFile(const std::string& path, Digest& out);

	private:
		std::array<std::uint32_t, 8> state;
		std::array<std::uint8_t, 64> block;
		std::size_t blockSize = 0;
		std::uint64_t totalBytes = 0;

		void Compress(const std::uint8_t* chunk);
	};
} // namespace spades
