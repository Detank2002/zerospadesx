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

#include "Sha256.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace spades {
	namespace {
		constexpr std::uint32_t kRound[64] = {
		  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
		  0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
		  0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
		  0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
		  0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
		  0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
		  0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
		  0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
		  0xc67178f2};

		inline std::uint32_t Rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
	} // namespace

	Sha256::Sha256()
	    : state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
	            0x1f83d9ab, 0x5be0cd19} {}

	void Sha256::Compress(const std::uint8_t* chunk) {
		std::uint32_t w[64];
		for (int i = 0; i < 16; ++i)
			w[i] = (std::uint32_t(chunk[i * 4]) << 24) | (std::uint32_t(chunk[i * 4 + 1]) << 16) |
			       (std::uint32_t(chunk[i * 4 + 2]) << 8) | std::uint32_t(chunk[i * 4 + 3]);
		for (int i = 16; i < 64; ++i) {
			const std::uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const std::uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}

		std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
		std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
		for (int i = 0; i < 64; ++i) {
			const std::uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
			const std::uint32_t ch = (e & f) ^ (~e & g);
			const std::uint32_t t1 = h + s1 + ch + kRound[i] + w[i];
			const std::uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
			const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
			const std::uint32_t t2 = s0 + maj;
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
		state[5] += f;
		state[6] += g;
		state[7] += h;
	}

	void Sha256::Update(const void* data, std::size_t size) {
		const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
		totalBytes += size;
		while (size > 0) {
			const std::size_t take = std::min<std::size_t>(size, block.size() - blockSize);
			std::memcpy(block.data() + blockSize, p, take);
			blockSize += take;
			p += take;
			size -= take;
			if (blockSize == block.size()) {
				Compress(block.data());
				blockSize = 0;
			}
		}
	}

	Sha256::Digest Sha256::Finish() {
		const std::uint64_t bits = totalBytes * 8;
		const std::uint8_t pad = 0x80;
		Update(&pad, 1);
		const std::uint8_t zero = 0;
		while (blockSize != 56)
			Update(&zero, 1);
		std::uint8_t length[8];
		for (int i = 0; i < 8; ++i)
			length[i] = static_cast<std::uint8_t>(bits >> (56 - i * 8));
		Update(length, 8);

		Digest out;
		for (int i = 0; i < 8; ++i) {
			out[i * 4] = static_cast<std::uint8_t>(state[i] >> 24);
			out[i * 4 + 1] = static_cast<std::uint8_t>(state[i] >> 16);
			out[i * 4 + 2] = static_cast<std::uint8_t>(state[i] >> 8);
			out[i * 4 + 3] = static_cast<std::uint8_t>(state[i]);
		}
		return out;
	}

	std::string Sha256::ToHex(const Digest& digest) {
		static const char* const kHex = "0123456789abcdef";
		std::string s;
		s.reserve(64);
		for (std::uint8_t b : digest) {
			s.push_back(kHex[b >> 4]);
			s.push_back(kHex[b & 15]);
		}
		return s;
	}

	bool Sha256::OfFile(const std::string& path, Digest& out) {
		std::FILE* f = std::fopen(path.c_str(), "rb");
		if (!f)
			return false;
		Sha256 h;
		std::vector<unsigned char> buf(1 << 16);
		std::size_t rd;
		while ((rd = std::fread(buf.data(), 1, buf.size(), f)) > 0)
			h.Update(buf.data(), rd);
		const bool ok = std::ferror(f) == 0;
		std::fclose(f);
		if (!ok)
			return false;
		out = h.Finish();
		return true;
	}
} // namespace spades
