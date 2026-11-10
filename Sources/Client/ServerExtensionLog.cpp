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
#include <ctime>
#include <map>

#include <json/json.h>

#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>

namespace spades {
	namespace client {
		namespace {
			constexpr auto kPath = "/server_extensions.json";
			constexpr std::int64_t kForgetAfter = 30 * 24 * 60 * 60; // seconds

			struct Known {
				std::vector<std::uint8_t> ids;
				std::int64_t seen = 0;
			};

			std::map<std::string, Known>& Log() {
				static std::map<std::string, Known> log;
				static bool loaded = false;
				if (loaded)
					return log;
				loaded = true;
				try {
					if (!FileManager::FileExists(kPath))
						return log;
					Json::Value root;
					Json::Reader reader;
					if (!reader.parse(FileManager::ReadAllBytes(kPath), root, false) ||
					    !root.isObject())
						return log;
					for (const std::string& key : root.getMemberNames()) {
						const Json::Value& entry = root[key];
						if (!entry.isObject() || !entry["ext"].isArray())
							continue;
						Known known;
						known.seen = static_cast<std::int64_t>(entry["seen"].asDouble());
						for (const Json::Value& id : entry["ext"])
							if (id.isIntegral() && id.asInt() >= 0 && id.asInt() < 256)
								known.ids.push_back(static_cast<std::uint8_t>(id.asInt()));
						log[key] = std::move(known);
					}
				} catch (const std::exception& ex) {
					SPLog("Could not read %s: %s", kPath, ex.what());
				}
				return log;
			}

			void Save() {
				const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
				Json::Value root(Json::objectValue);
				for (const auto& entry : Log()) {
					// The forgotten ones are left out of the file too.
					if (now - entry.second.seen > kForgetAfter)
						continue;
					Json::Value v(Json::objectValue);
					v["seen"] = static_cast<double>(entry.second.seen);
					Json::Value ids(Json::arrayValue);
					for (std::uint8_t id : entry.second.ids)
						ids.append(static_cast<int>(id));
					v["ext"] = ids;
					root[entry.first] = v;
				}
				try {
					Json::StyledWriter writer;
					FileManager::OpenForWriting(kPath)->Write(writer.write(root));
				} catch (const std::exception& ex) {
					SPLog("Could not write %s: %s", kPath, ex.what());
				}
			}
		} // namespace

		void RecordServerExtensions(const std::string& key, const std::vector<std::uint8_t>& ids) {
			if (key.empty())
				return;
			Known& known = Log()[key];
			known.ids = ids;
			known.seen = static_cast<std::int64_t>(std::time(nullptr));
			Save();
		}

		std::vector<std::uint8_t> FindServerExtensions(const std::string& key) {
			auto& log = Log();
			auto it = log.find(key);
			if (it == log.end())
				return {};
			const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
			if (now - it->second.seen > kForgetAfter)
				return {};
			return it->second.ids;
		}
	} // namespace client
} // namespace spades
