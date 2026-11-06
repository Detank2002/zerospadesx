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

#include <curl/curl.h>

namespace spades {
	namespace gui {
		/**
		 * Gives a curl handle the CA certificates to verify HTTPS servers with.
		 *
		 * Android's libcurl has none of its own: it was built without a CA bundle,
		 * and the OpenSSL it uses looks for certificates in a folder on the machine
		 * that built it. There the handle gets the device's own trust store, so
		 * every HTTPS request would otherwise fail peer verification. Elsewhere the
		 * library's default is kept.
		 */
		void SetupCurlTls(CURL*);
	} // namespace gui
} // namespace spades
