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

#include <cstdint>
#include <string>
#include <vector>

namespace spades {
	namespace client {
		/**
		 * The extensions each server was seen with, for the server list to
		 * show: the master list carries none, so a server is known only once
		 * it was joined. Kept in `server_extensions.json`; a server not joined
		 * for 30 days is forgotten, its extensions perhaps gone.
		 */

		/**
		 * A server's key, the same for the master list's identifier
		 * ("aos://16777343:32887") and an address typed in ("127.0.0.1:32887"),
		 * found with no DNS lookup: "host:port", the host dotted when numeric,
		 * else in lower case.
		 */
		std::string ServerExtensionKey(const std::string& address);

		/** Records the extensions both the server and this client have. */
		void RecordServerExtensions(const std::string& key, const std::vector<std::uint8_t>& ids);

		/** The extensions a server was last seen with, or none if not known (or
		 * not seen for 30 days). */
		std::vector<std::uint8_t> FindServerExtensions(const std::string& key);
	} // namespace client
} // namespace spades
