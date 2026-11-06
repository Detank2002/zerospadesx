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

#include "CurlTls.h"

#if defined(__ANDROID__)
#include <cstdio>
#include <string>

#include <dirent.h>

#include <Core/Debug.h>
#endif

namespace spades {
	namespace gui {
#if defined(__ANDROID__)
		namespace {
			// Appends the PEM certificate blocks of a file of the trust store. Each
			// one is a certificate in PEM followed by a text dump of it, which is
			// left out.
			bool AppendPemBlocks(const std::string& path, std::string& out) {
				std::FILE* f = std::fopen(path.c_str(), "rb");
				if (!f)
					return false;
				std::string all;
				char buf[4096];
				std::size_t rd;
				while ((rd = std::fread(buf, 1, sizeof(buf), f)) > 0)
					all.append(buf, rd);
				std::fclose(f);

				static const std::string begin = "-----BEGIN CERTIFICATE-----";
				static const std::string end = "-----END CERTIFICATE-----";
				bool found = false;
				std::size_t pos = 0;
				while ((pos = all.find(begin, pos)) != std::string::npos) {
					std::size_t stop = all.find(end, pos);
					if (stop == std::string::npos)
						break;
					stop += end.size();
					out.append(all, pos, stop - pos);
					out += '\n';
					found = true;
					pos = stop;
				}
				return found;
			}

			// Every certificate of the device's trust store, as one PEM bundle. Read
			// once, by the first request.
			const std::string& AndroidCaBundle() {
				static const std::string bundle = [] {
					std::string out;
					// Android 14 moved the store into the Conscrypt module, which
					// is updated apart from the system; before it, it is in /system.
					for (const char* dir :
					     {"/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts"}) {
						DIR* d = ::opendir(dir);
						if (!d)
							continue;
						int count = 0;
						while (auto* e = ::readdir(d)) {
							if (e->d_name[0] == '.')
								continue;
							if (AppendPemBlocks(std::string(dir) + "/" + e->d_name, out))
								++count;
						}
						::closedir(d);
						if (count > 0) {
							SPLog("[Android] HTTPS: %d CA certificates from %s", count, dir);
							break;
						}
					}
					if (out.empty())
						SPLog("[Android] HTTPS: no CA certificates found; downloads over "
						      "HTTPS will fail");
					return out;
				}();
				return bundle;
			}
		} // namespace
#endif

		void SetupCurlTls(CURL* handle) {
#if defined(__ANDROID__)
			const std::string& bundle = AndroidCaBundle();
			if (bundle.empty())
				return;
			curl_blob blob;
			blob.data = const_cast<char*>(bundle.data());
			blob.len = bundle.size();
			// The bundle lives as long as the process.
			blob.flags = CURL_BLOB_NOCOPY;
			curl_easy_setopt(handle, CURLOPT_CAINFO_BLOB, &blob);
#else
			(void)handle;
#endif
		}
	} // namespace gui
} // namespace spades
