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
#include <cstdint>
#include <optional>
#include <string>

// What a release can tell this build about its own package, kept apart from
// the download so it can be tested without a network.
//
// Every release carries SHA256SUMS (sha256sum's output for every package) and
// SHA256SUMS.sig, the Ed25519 signature of SHA256SUMS made with the release
// key (a CI secret). A build trusts a package only when the signature holds
// for the public key built into it, and the package's SHA-256 is the one the
// signed list gives for that release's name of it.

namespace spades {
	namespace gui {
		using UpdatePublicKey = std::array<std::uint8_t, 32>;

		/** The public half of the release key. */
		const UpdatePublicKey& ReleaseSigningKey();

		/**
		 * The package this build updates itself from: "windows-x64",
		 * "windows-x86" or "linux-x64", set by CI for the packages it publishes
		 * (ZEROSPADES_UPDATE_PACKAGE). Empty for every other build (a local
		 * build, Nix, macOS, the Quest), which only points at the release page.
		 */
		std::string SelfUpdatePackage();

		/** The release's file name of `package` for `tag`, as release.yml names it. */
		std::string UpdateAssetName(const std::string& tag, const std::string& package);

		/** True if `signature` (64 raw bytes) is `key`'s Ed25519 signature of `sums`. */
		bool VerifyUpdateSums(const std::string& sums, const std::string& signature,
		                      const UpdatePublicKey& key);

		/**
		 * The SHA-256 (64 lower-case hex digits) that `sums`, in sha256sum's
		 * format, gives for `fileName`; nothing if it lists none or a malformed one.
		 */
		std::optional<std::string> FindUpdateSum(const std::string& sums,
		                                         const std::string& fileName);

		/**
		 * True if `path`, a file's name in a package, may be written under the
		 * game's folder: relative, `/`-separated, made of plain names (no `.` or
		 * `..`, no drive or backslash).
		 */
		bool IsSafeUpdatePath(const std::string& path);
	} // namespace gui
} // namespace spades
