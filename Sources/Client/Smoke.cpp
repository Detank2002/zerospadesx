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

// The puffs, their drift and the hiss are Serius_Sim's smoke grenade, made
// into one emitter the server drives.

#include "Smoke.h"

#include <algorithm>
#include <cmath>

#include "Client.h"
#include "IAudioDevice.h"
#include "NetProtocol.h"
#include "SmokeSpriteEntity.h"
#include <Core/Debug.h>
#include <Core/Settings.h>

DEFINE_SPADES_SETTING(cg_smokeVolume, "1");

namespace spades {
	namespace client {
		namespace {
			// The hiss lasts about 9 s; while a smoke keeps coming it is played
			// again a little before it ends, so the two overlap rather than cut.
			constexpr float kSoundPeriod = 8.5F;
			// A puff lives this long at most, so a long smoke keeps a steady
			// number of them rather than piling up; it is topped up as they go.
			constexpr float kMaxPuffLife = 8.0F;
			constexpr float kPuffFadeIn = 0.25F;
		} // namespace

		SmokeEntity::SmokeEntity(Client& client, IAudioDevice* audio, Handle<IAudioChunk> sound,
		                         const SmokeParams& params, std::shared_ptr<bool> stop)
		    : client(client), audio(audio), sound(std::move(sound)), params(params),
		      stop(std::move(stop)) {}

		void SmokeEntity::Puff(float untilGone) {
			const float r = params.radius;
			// How much of a grenade's cloud (radius 5) this one is: puffs as
			// big and as many in proportion, within what reads as smoke.
			const float k = std::max(0.2F, std::min(r / 5.0F, 4.0F));

			Vector3 off = RandomUnitVector();
			off.z = std::fabs(off.z) * 0.4F; // z is down: near the ground, not under it
			Vector3 vel;
			float size, growth, damp;
			if (params.style == SmokeParams::Column) {
				// A thin plume rising
				off.x *= r * 0.15F;
				off.y *= r * 0.15F;
				vel = MakeVector3((SampleRandomFloat() - 0.5F) * 0.6F,
				                  (SampleRandomFloat() - 0.5F) * 0.6F,
				                  -7.0F - SampleRandomFloat() * 5.0F);
				size = (0.9F + SampleRandomFloat() * 0.5F) * std::max(0.6F, k);
				growth = 0.22F;
				damp = 0.9F;
			} else {
				// A wide cloud, spreading low
				off.x *= r;
				off.y *= r;
				vel = MakeVector3((SampleRandomFloat() - 0.5F) * 8.0F * k,
				                  (SampleRandomFloat() - 0.5F) * 8.0F * k,
				                  -4.35F - SampleRandomFloat() * 4.3F);
				size = (3.2F + SampleRandomFloat() * 1.5F) * std::max(0.5F, std::min(k, 1.5F));
				growth = 0.8F;
				damp = 0.94F;
			}

			auto ent = stmp::make_unique<SmokeSpriteEntity>(client, params.color, 60.0F,
			                                               SmokeSpriteEntity::Type::Steady);
			ent->SetTrajectory(params.origin + off, vel, damp, 0.0F);
			ent->SetRotation(SampleRandomFloat() * kTwoPi);
			ent->SetRadius(size, growth, 0.3F);
			const float life = std::max(0.5F, std::min(untilGone, kMaxPuffLife)) +
			                   SampleRandomFloat() * 0.5F;
			ent->SetLifeTime(life, kPuffFadeIn, std::min(params.fade, life * 0.5F) + 0.01F);
			ent->SetBlockHitAction(BlockHitAction::Ignore);
			client.AddLocalEntity(std::move(ent));
		}

		bool SmokeEntity::Update(float dt) {
			age += dt;
			if (*stop || age >= params.duration)
				return false; // the puffs out there fade by themselves

			// The hiss, again while it keeps coming
			if (params.sound && audio && sound) {
				soundTimer -= dt;
				if (soundTimer <= 0.0F) {
					soundTimer += kSoundPeriod;
					AudioParam p;
					p.volume = static_cast<float>(cg_smokeVolume);
					audio->Play(sound.GetPointerOrNull(), params.origin, p);
				}
			}

			// A grenade's cloud (radius 5) puffs 40 times a second, a column 14;
			// a bigger or smaller one in proportion to its area.
			const float k = std::max(0.2F, std::min(params.radius / 5.0F, 4.0F));
			const float interval = params.style == SmokeParams::Column
			                         ? 0.07F / std::max(1.0F, k)
			                         : std::max(0.01F, 0.025F / (k * k));
			puffTimer -= dt;
			int budget = 64; // after a long frame, catch up only so far
			while (puffTimer <= 0.0F && budget-- > 0) {
				puffTimer += interval;
				Puff(params.duration + params.fade - age);
			}
			if (puffTimer < 0.0F)
				puffTimer = 0.0F;
			return true;
		}

		std::shared_ptr<bool> SmokeRegistry::Start(int id) {
			auto flag = std::make_shared<bool>(false);
			if (id == SmokeParams::kAnonymous)
				return flag;
			Clear(id);
			smokes[id] = flag;
			return flag;
		}

		void SmokeRegistry::Clear(int id) {
			if (id == SmokeParams::kAnonymous) {
				for (auto& s : smokes)
					*s.second = true;
				smokes.clear();
				return;
			}
			auto it = smokes.find(id);
			if (it != smokes.end()) {
				*it->second = true;
				smokes.erase(it);
			}
		}

		void ApplySmokePacket(NetPacketReader& r, Client& client) {
			if (r.GetNumRemainingBytes() < 1) {
				SPLog("Dropped a truncated Smoke packet");
				return;
			}
			const int sub = r.ReadByte();
			switch (sub) {
				case SmokeRegistry::kSubSmoke: {
					// id style xyz radius rgb opacity duration fade flags
					if (r.GetNumRemainingBytes() < 1 + 1 + 12 + 1 + 4 + 2 + 1 + 1) {
						SPLog("Dropped a Smoke of the wrong size");
						return;
					}
					SmokeParams p;
					p.id = r.ReadByte();
					p.style = r.ReadByte();
					if (p.style != SmokeParams::Column)
						p.style = SmokeParams::Cloud; // an unknown style: a cloud
					const float x = r.ReadFloat(), y = r.ReadFloat(), z = r.ReadFloat();
					p.origin = MakeVector3(x, y, z);
					p.radius = std::max(1.0F, static_cast<float>(r.ReadByte()));
					const float cr = r.ReadByte() / 255.0F, cg = r.ReadByte() / 255.0F,
					            cb = r.ReadByte() / 255.0F, ca = r.ReadByte() / 255.0F;
					p.color = MakeVector4(cr, cg, cb, ca);
					p.duration = static_cast<float>(r.ReadShort()) / 10.0F;
					p.fade = static_cast<float>(r.ReadByte()) / 10.0F;
					p.sound = (r.ReadByte() & 1) != 0;
					if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
						SPLog("Dropped a Smoke somewhere that is nowhere");
						return;
					}
					client.StartSmoke(p);
				} break;
				case SmokeRegistry::kSubClear: {
					if (r.GetNumRemainingBytes() < 1) {
						SPLog("Dropped a Clear Smoke of the wrong size");
						return;
					}
					client.ClearSmoke(r.ReadByte());
				} break;
				default:
					SPLog("Ignoring an unknown Smoke sub packet %d", sub);
					break;
			}
		}
	} // namespace client
} // namespace spades
