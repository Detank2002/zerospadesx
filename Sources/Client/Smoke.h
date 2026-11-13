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
#include <memory>
#include <unordered_map>

#include "IAudioChunk.h"
#include "ILocalEntity.h"
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class Client;
		class IAudioDevice;
		class NetPacketReader;

		/**
		 * Client-side state of the *Smoke* protocol extension (extension id 0x3F,
		 * packet id 0x7F), docs/extensions/smoke.md: smoke the server puts in the
		 * world, a cloud or a rising column, in a colour, opacity and size, for a
		 * time. Only drawn; the server decides what it means for the game.
		 *
		 *     0  Smoke        S->C  id(1) style(1) x y z(4 each) radius(1) r g b
		 *                           opacity(1 each) duration(2) fade(1) flags(1)
		 *     1  Clear Smoke  S->C  id(1)   255: every smoke
		 */
		struct SmokeParams {
			enum Style : std::uint8_t { Cloud = 0, Column = 1 };
			static constexpr int kAnonymous = 255;

			int id = kAnonymous;
			int style = Cloud;
			Vector3 origin;
			float radius = 5.0F;   // blocks
			Vector4 color;         // RGB 0-1, A the opacity
			float duration = 10.0F; // s it keeps coming
			float fade = 5.0F;     // s it takes to go after
			bool sound = false;
		};

		/** A smoke: emits puffs for its duration, then lets them fade. */
		class SmokeEntity : public ILocalEntity {
		public:
			SmokeEntity(Client& client, IAudioDevice* audio, Handle<IAudioChunk> sound,
			            const SmokeParams& params, std::shared_ptr<bool> stop);

			bool Update(float dt) override;
			void Render3D() override {} // the puffs are entities of their own

		private:
			Client& client;
			IAudioDevice* audio;
			Handle<IAudioChunk> sound;
			SmokeParams params;
			std::shared_ptr<bool> stop; // set by Clear Smoke
			float age = 0.0F;
			float puffTimer = 0.0F;
			float soundTimer = 0.0F;

			void Puff(float untilGone);
		};

		/** The server's smokes, by id, so Clear Smoke can stop them. */
		class SmokeRegistry {
		public:
			static constexpr std::uint8_t kExtensionId = 0x3F;
			static constexpr std::uint8_t kPacketId = 64 + kExtensionId;
			static constexpr std::uint8_t kSubSmoke = 0;
			static constexpr std::uint8_t kSubClear = 1;

			/** The stop flag for a new smoke; an id in use stops the old one. */
			std::shared_ptr<bool> Start(int id);
			/** Stops one smoke, or every one (255); their puffs fade. */
			void Clear(int id);

		private:
			std::unordered_map<int, std::shared_ptr<bool>> smokes;
		};

		/** A Smoke packet (after its id) applied to the client's world: a smoke
		 * started or cleared. A malformed one is dropped; an unknown sub packet,
		 * and an unknown style (drawn as a cloud), are how it grows. */
		void ApplySmokePacket(NetPacketReader& r, Client& client);
	} // namespace client
} // namespace spades
