/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>

namespace spades {
	namespace draw {
		// Process-wide cache of SPIR-V blobs keyed by resource path. Lets
		// PreloadShaders() pull shader binaries off disk at renderer init so
		// later pipeline builds (map load / first draw) skip file IO.
		namespace SpirvCache {

			inline std::mutex& GetMutex() {
				static std::mutex m;
				return m;
			}

			inline std::unordered_map<std::string, std::vector<uint32_t>>& GetMap() {
				static std::unordered_map<std::string, std::vector<uint32_t>> map;
				return map;
			}

			/** Multiview (vr_multiview): load each shader's `.mv.spv` variant, compiled
			 * with ZS_MULTIVIEW, wherever one exists. Set once, before the first
			 * pipeline is built, and never changed while the renderer lives. */
			inline bool& MultiviewFlag() {
				static bool on = false;
				return on;
			}
			inline void SetMultiview(bool on) { MultiviewFlag() = on; }

			inline std::vector<uint32_t> LoadExact(const std::string& filename);

			/** Vulkan's guaranteed push constant space, and all a Quest 2 (Adreno
			 * 650) has. A pipeline whose range is larger fails to build there with
			 * VK_ERROR_UNKNOWN, after working on every desktop GPU (256 bytes). */
			constexpr uint32_t kMaxPushConstants = 128;

			/** The size of the push constant block `code` declares (0 for none),
			 * from its Offset/MatrixStride/ArrayStride decorations. */
			inline uint32_t PushConstantSize(const std::vector<uint32_t>& code) {
				if (code.size() < 5 || code[0] != 0x07230203)
					return 0;
				struct Type {
					uint32_t op = 0, a = 0, b = 0; // opcode, then its operands
					std::vector<uint32_t> members;
				};
				std::unordered_map<uint32_t, Type> types;
				std::unordered_map<uint32_t, uint32_t> constants, arrayStride;
				// struct id -> member -> (offset, matrix stride)
				std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>>>
				  members;
				uint32_t pushType = 0;
				for (size_t i = 5; i < code.size();) {
					const uint32_t count = code[i] >> 16, op = code[i] & 0xFFFF;
					if (count == 0 || i + count > code.size())
						break;
					const uint32_t* w = &code[i];
					switch (op) {
						case 21: case 22: types[w[1]] = {op, w[2]}; break; // int, float
						case 23: case 24: types[w[1]] = {op, w[2], w[3]}; break; // vector, matrix
						case 28: types[w[1]] = {op, w[2], w[3]}; break; // array (length id)
						case 30: { // struct
							Type t{op};
							t.members.assign(w + 2, w + count);
							types[w[1]] = t;
							break;
						}
						case 32: types[w[1]] = {op, w[2], w[3]}; break; // pointer
						case 43: constants[w[2]] = w[3]; break;
						case 71: if (w[2] == 6) arrayStride[w[1]] = w[3]; break;
						case 72: // member decorate: Offset 35, MatrixStride 7
							if (w[3] == 35) members[w[1]][w[2]].first = w[4];
							if (w[3] == 7) members[w[1]][w[2]].second = w[4];
							break;
						case 59: // variable in the PushConstant storage class
							if (w[3] == 9 && types[w[1]].op == 32)
								pushType = types[w[1]].b;
							break;
					}
					i += count;
				}
				std::function<uint32_t(uint32_t, uint32_t)> size = [&](uint32_t id, uint32_t matrixStride) -> uint32_t {
					const Type& t = types[id];
					switch (t.op) {
						case 21: case 22: return t.a / 8;
						case 23: return t.b * size(t.a, 0);
						case 24: return t.b * (matrixStride ? matrixStride : size(t.a, 0));
						case 28: return constants[t.b] * (arrayStride[id] ? arrayStride[id] : size(t.a, 0));
						case 30: {
							uint32_t end = 0;
							for (uint32_t m = 0; m < t.members.size(); m++) {
								auto d = members[id][m];
								end = std::max(end, d.first + size(t.members[m], d.second));
							}
							return end;
						}
					}
					return 0;
				};
				return pushType ? size(pushType, 0) : 0;
			}

			/** Refuses a shader with more than kMaxPushConstants: on every GPU, not
			 * only on the ones with no more (VulkanPushConstants.h). */
			inline void CheckPushConstants(const std::vector<uint32_t>& code, const char* name) {
				if (uint32_t pc = PushConstantSize(code); pc > kMaxPushConstants)
					SPRaise("Shader %s has %u bytes of push constants; Vulkan only promises "
					        "%u (see VulkanPushConstants.h)",
					        name, pc, kMaxPushConstants);
			}

			inline std::vector<uint32_t> Load(const std::string& requested) {
				std::string filename = requested;
				if (MultiviewFlag() && filename.size() > 4 &&
				    filename.compare(filename.size() - 4, 4, ".spv") == 0) {
					std::string variant = filename.substr(0, filename.size() - 4) + ".mv.spv";
					if (FileManager::FileExists(variant.c_str()))
						filename = variant;
				}
				return LoadExact(filename);
			}

			inline std::vector<uint32_t> LoadExact(const std::string& filename) {
				{
					std::lock_guard<std::mutex> lock{GetMutex()};
					auto it = GetMap().find(filename);
					if (it != GetMap().end())
						return it->second;
				}

				std::unique_ptr<IStream> stream = FileManager::OpenForReading(filename.c_str());
				if (!stream) {
					SPRaise("Failed to open shader file: %s", filename.c_str());
				}
				size_t size = stream->GetLength();
				std::vector<uint32_t> code(size / 4);
				stream->Read(code.data(), size);
				CheckPushConstants(code, filename.c_str());

				{
					std::lock_guard<std::mutex> lock{GetMutex()};
					GetMap().emplace(filename, code);
				}
				return code;
			}

			// Best-effort warm-up; missing files are logged, not fatal.
			inline void Preload(std::initializer_list<const char*> files) {
				for (const char* f : files) {
					try {
						Load(f);
					} catch (const std::exception& ex) {
						SPLog("SpirvCache: failed to preload '%s': %s", f, ex.what());
					}
				}
			}

		} // namespace SpirvCache
	} // namespace draw
} // namespace spades
