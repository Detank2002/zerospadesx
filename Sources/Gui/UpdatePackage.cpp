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

#include "UpdatePackage.h"

#include <cctype>

#include <monocypher/monocypher-ed25519.h>

#ifndef ZEROSPADES_UPDATE_PACKAGE
#define ZEROSPADES_UPDATE_PACKAGE ""
#endif

namespace spades {
	namespace gui {
		const UpdatePublicKey& ReleaseSigningKey() {
			// The release key's public half. Its private half is the
			// UPDATE_SIGNING_KEY secret, which release.yml signs SHA256SUMS with.
			static const UpdatePublicKey key = {
			  0xda, 0xc6, 0x9b, 0x9e, 0xdb, 0x09, 0x03, 0x33, 0x53, 0x83, 0x0a,
			  0xf8, 0x0c, 0x13, 0xa1, 0xfb, 0x0f, 0x36, 0xfb, 0xf1, 0x4a, 0x93,
			  0x7c, 0x50, 0xf9, 0xcf, 0xab, 0xeb, 0x81, 0x0d, 0x5c, 0xd1};
			return key;
		}

		std::string SelfUpdatePackage() { return ZEROSPADES_UPDATE_PACKAGE; }

		std::string UpdateAssetName(const std::string& tag, const std::string& package) {
			return "ZeroSpadesX-" + tag + "-" + package + ".zip";
		}

		bool VerifyUpdateSums(const std::string& sums, const std::string& signature,
		                      const UpdatePublicKey& key) {
			if (signature.size() != 64)
				return false;
			return crypto_ed25519_check(reinterpret_cast<const std::uint8_t*>(signature.data()),
			                            key.data(),
			                            reinterpret_cast<const std::uint8_t*>(sums.data()),
			                            sums.size()) == 0;
		}

		std::optional<std::string> FindUpdateSum(const std::string& sums,
		                                         const std::string& fileName) {
			std::size_t start = 0;
			while (start < sums.size()) {
				std::size_t nl = sums.find('\n', start);
				std::string line =
				  sums.substr(start, (nl == std::string::npos ? sums.size() : nl) - start);
				start = nl == std::string::npos ? sums.size() : nl + 1;
				if (!line.empty() && line.back() == '\r')
					line.pop_back();

				// "<64 hex> <space or *><name>"
				if (line.size() < 66 || line[64] != ' ')
					continue;
				std::string name = line.substr(65);
				if (!name.empty() && name[0] == '*')
					name.erase(0, 1);
				else if (!name.empty() && name[0] == ' ')
					name.erase(0, 1);
				if (name != fileName)
					continue;

				std::string hex = line.substr(0, 64);
				for (char& c : hex) {
					if (!std::isxdigit(static_cast<unsigned char>(c)))
						return {};
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				return hex;
			}
			return {};
		}

		bool IsSafeUpdatePath(const std::string& path) {
			if (path.empty() || path.size() > 200 || path[0] == '/')
				return false;
			std::size_t start = 0;
			while (start <= path.size()) {
				std::size_t slash = path.find('/', start);
				const std::size_t end = slash == std::string::npos ? path.size() : slash;
				const std::string part = path.substr(start, end - start);
				if (part.empty() || part == "." || part == "..")
					return false;
				for (char c : part) {
					const unsigned char u = static_cast<unsigned char>(c);
					if (!std::isalnum(u) && c != '.' && c != '-' && c != '_' && c != '+' &&
					    c != ' ')
						return false;
				}
				if (slash == std::string::npos)
					break;
				start = slash + 1;
			}
			return true;
		}
	} // namespace gui
} // namespace spades
