/*
 Copyright (c) 2026 ZeroSpades VR contributors

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

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "VRState.h"

// The VR presence extension: what a VR player's head and hands are doing, so
// every other client using this build - in a headset or on a monitor - can
// draw them. It rides the protocol-extension mechanism (packet 60): a server
// running Server/vr_presence.py advertises it, and only then does a client
// send or expect these packets. A stock server never sees one.
//
// Packet 64 + extension id, like Teamplay. Byte 1 is the sub packet.
//
//   Pose, client to server:  [122][0][payload]
//   Pose, server to client:  [122][0][player id][payload]
//   Config, server to client:[122][1][flags][max pose rate, Hz]
//
// Pose payload, little endian:
//   flags     u8   bit0 left hand, bit1 right hand, bit2 body position follows,
//                  bit3 the tools are in the left hand
//   head      pos 3 x i16, rot 4 x i16
//   left      pos 3 x i16, rot 4 x i16
//   right     pos 3 x i16, rot 4 x i16
//   body      3 x f32      the sender's body position (eye), world
//   buttons   u8           bit0 right trigger, bit1 left trigger,
//                          bit2 right grip,    bit3 left grip
//
// Positions are offsets from the sender's eye in world blocks, in units of
// 1/512 (so +-64 blocks). Rotations are unit quaternions in world axes, each
// component times 32767. Everything is in the game's world frame, so a
// receiver needs nothing but the sender's player position to place them.
//
// Server/vr_presence.py mirrors this byte for byte, and
// Server/test_vr_presence.py checks the two agree against a real piqueserver.

namespace spades {
	namespace client {

		// Extension 58, packet 122 (ExtensionTypeVRPresence in NetClient.h and
		// PacketTypeVRPresence in NetProtocol.h carry the same numbers).
		constexpr std::uint8_t kVRPresenceExtensionId = 58;
		constexpr std::uint8_t kVRPresencePacketType = 64 + kVRPresenceExtensionId;
		constexpr std::uint8_t kVRPresenceVersion = 1;

		enum VRPresenceSubPacket : std::uint8_t {
			VRSubPose = 0,
			VRSubConfig = 1,
		};

		enum : std::uint8_t {
			VRPoseLeftHand = 1 << 0,
			VRPoseRightHand = 1 << 1,
			VRPoseBody = 1 << 2,
			/** The sender holds the tools in the left hand. */
			VRPoseToolInLeftHand = 1 << 3,
		};

		enum : std::uint8_t {
			VRButtonRightTrigger = 1 << 0,
			VRButtonLeftTrigger = 1 << 1,
			VRButtonRightGrip = 1 << 2,
			VRButtonLeftGrip = 1 << 3,
		};

		/** Config flags, server to client. */
		enum : std::uint8_t {
			/** The server moves the body to the reported position (room-scale). */
			VRConfigBodySync = 1 << 0,
		};

		constexpr std::size_t kVRPosePayloadBytes = 1 + 3 * (6 + 8) + 12 + 1; // 56

		/** One VR player's pose, in world axes, relative to their eye. */
		struct VRNetPose {
			std::uint8_t flags = 0;
			Vector3 headOffset{0.0F, 0.0F, 0.0F};
			VRQuat headRotation;
			Vector3 handOffset[2]{{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}};
			VRQuat handRotation[2];
			Vector3 body{0.0F, 0.0F, 0.0F};
			std::uint8_t buttons = 0;

			bool HasHand(int h) const {
				return (flags & (h == VRHandLeft ? VRPoseLeftHand : VRPoseRightHand)) != 0;
			}
		};

		namespace vrnet {
			inline std::int16_t PackPos(float v) {
				float s = std::round(v * 512.0F);
				if (!(s > -32767.0F))
					s = -32767.0F; // also catches NaN
				if (s > 32767.0F)
					s = 32767.0F;
				return static_cast<std::int16_t>(s);
			}
			inline float UnpackPos(std::int16_t v) { return static_cast<float>(v) / 512.0F; }
			inline std::int16_t PackUnit(float v) {
				float s = std::round(v * 32767.0F);
				if (!(s > -32767.0F))
					s = -32767.0F;
				if (s > 32767.0F)
					s = 32767.0F;
				return static_cast<std::int16_t>(s);
			}
			inline float UnpackUnit(std::int16_t v) { return static_cast<float>(v) / 32767.0F; }

			inline void PutI16(std::vector<char>& out, std::int16_t v) {
				auto u = static_cast<std::uint16_t>(v);
				out.push_back(static_cast<char>(u & 0xFF));
				out.push_back(static_cast<char>(u >> 8));
			}
			inline void PutF32(std::vector<char>& out, float f) {
				std::uint32_t u;
				std::memcpy(&u, &f, 4);
				for (int i = 0; i < 4; i++)
					out.push_back(static_cast<char>((u >> (8 * i)) & 0xFF));
			}
			inline std::int16_t GetI16(const char* p) {
				auto u = static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[0]) |
				                                    (static_cast<std::uint8_t>(p[1]) << 8));
				return static_cast<std::int16_t>(u);
			}
			inline float GetF32(const char* p) {
				std::uint32_t u = 0;
				for (int i = 0; i < 4; i++)
					u |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[i])) << (8 * i);
				float f;
				std::memcpy(&f, &u, 4);
				return f;
			}

			inline void PutPose(std::vector<char>& out, const Vector3& pos, const VRQuat& q) {
				PutI16(out, PackPos(pos.x));
				PutI16(out, PackPos(pos.y));
				PutI16(out, PackPos(pos.z));
				VRQuat n = q.Normalized();
				PutI16(out, PackUnit(n.x));
				PutI16(out, PackUnit(n.y));
				PutI16(out, PackUnit(n.z));
				PutI16(out, PackUnit(n.w));
			}
			inline void GetPose(const char* p, Vector3& pos, VRQuat& q) {
				pos = MakeVector3(UnpackPos(GetI16(p)), UnpackPos(GetI16(p + 2)),
				                  UnpackPos(GetI16(p + 4)));
				q = VRQuat(UnpackUnit(GetI16(p + 6)), UnpackUnit(GetI16(p + 8)),
				           UnpackUnit(GetI16(p + 10)), UnpackUnit(GetI16(p + 12)))
				      .Normalized();
			}

			/** Appends the pose payload (no packet or sub packet id). */
			inline void EncodePose(std::vector<char>& out, const VRNetPose& pose) {
				out.push_back(static_cast<char>(pose.flags));
				PutPose(out, pose.headOffset, pose.headRotation);
				PutPose(out, pose.handOffset[0], pose.handRotation[0]);
				PutPose(out, pose.handOffset[1], pose.handRotation[1]);
				PutF32(out, pose.body.x);
				PutF32(out, pose.body.y);
				PutF32(out, pose.body.z);
				out.push_back(static_cast<char>(pose.buttons));
			}

			/** Reads a payload of at least kVRPosePayloadBytes. */
			inline VRNetPose DecodePose(const char* p) {
				VRNetPose pose;
				pose.flags = static_cast<std::uint8_t>(p[0]);
				GetPose(p + 1, pose.headOffset, pose.headRotation);
				GetPose(p + 15, pose.handOffset[0], pose.handRotation[0]);
				GetPose(p + 29, pose.handOffset[1], pose.handRotation[1]);
				pose.body = MakeVector3(GetF32(p + 43), GetF32(p + 47), GetF32(p + 51));
				pose.buttons = static_cast<std::uint8_t>(p[55]);
				return pose;
			}
		} // namespace vrnet
	} // namespace client
} // namespace spades
