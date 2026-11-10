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

#include "ServerExtensionLog.h"

#include <cctype>
#include <cstdint>
#include <string>

namespace spades {
	namespace client {
		std::string ServerExtensionKey(const std::string& address) {
			std::string a = address;
			if (a.compare(0, 7, "aos:///") == 0)
				a.erase(0, 7);
			else if (a.compare(0, 6, "aos://") == 0)
				a.erase(0, 6);

			std::string host = a, port = "32887";
			const std::size_t colon = a.find(':');
			if (colon != std::string::npos) {
				host = a.substr(0, colon);
				port = a.substr(colon + 1);
				// "1.2.3.4:32887:0.75": the protocol after the port
				const std::size_t more = port.find(':');
				if (more != std::string::npos)
					port.erase(more);
			}

			bool digits = !host.empty();
			for (char c : host)
				digits = digits && std::isdigit(static_cast<unsigned char>(c));
			if (digits) {
				// The master list's form: the address as ENet keeps it, the first
				// octet in the lowest byte.
				std::uint32_t v = 0;
				for (char c : host)
					v = v * 10 + static_cast<std::uint32_t>(c - '0');
				host = std::to_string(v & 0xFF) + "." + std::to_string((v >> 8) & 0xFF) + "." +
				       std::to_string((v >> 16) & 0xFF) + "." + std::to_string((v >> 24) & 0xFF);
			} else {
				for (char& c : host)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return host + ":" + port;
		}
	}
}
