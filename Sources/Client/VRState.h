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

#include <algorithm>
#include <string>

#include <Core/Math.h>

// What the game knows about a VR headset. Nothing in here depends on OpenXR,
// so the client, the renderer and the tests compile the same with or without
// it; the OpenXR backend (Sources/VR) fills these in.

namespace spades {
	namespace client {

		/** A rotation as a unit quaternion in the Hamilton convention, v' = q v q*.
		 *
		 * Deliberately not Core's `Quaternion`: that one rotates the other way round
		 * (`MakeRotation` negates the sine) and carries the grenade's network
		 * encoding. Keeping the two apart means neither can be handed to code that
		 * expects the other. */
		struct VRQuat {
			float x = 0.0F, y = 0.0F, z = 0.0F, w = 1.0F;

			VRQuat() = default;
			VRQuat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

			VRQuat operator*(const VRQuat& o) const {
				return VRQuat(w * o.x + x * o.w + y * o.z - z * o.y,
				              w * o.y - x * o.z + y * o.w + z * o.x,
				              w * o.z + x * o.y - y * o.x + z * o.w,
				              w * o.w - x * o.x - y * o.y - z * o.z);
			}
			VRQuat Conjugate() const { return VRQuat(-x, -y, -z, w); }
			VRQuat Normalized() const;
			Vector3 Rotate(const Vector3& v) const;

			/** The rotation whose matrix has these (orthonormal) columns. */
			static VRQuat FromAxes(const Vector3& ax, const Vector3& ay, const Vector3& az);
			/** Rotation by `angle` radians about the unit vector `axis`. */
			static VRQuat FromAxisAngle(const Vector3& axis, float angle);
		};

		struct VRPose {
			Vector3 position{0.0F, 0.0F, 0.0F};
			VRQuat orientation;
			bool valid = false;

			/** The pose's basis vector `i` in the space the pose is expressed in. */
			Vector3 Axis(int i) const {
				Vector3 v{i == 0 ? 1.0F : 0.0F, i == 1 ? 1.0F : 0.0F, i == 2 ? 1.0F : 0.0F};
				return orientation.Rotate(v);
			}
		};

		enum VRHandIndex { VRHandLeft = 0, VRHandRight = 1 };

		/** One controller, in tracking space: OpenXR's +X right, +Y up, +Z back,
		 * metres, floor at Y = 0 when the runtime has a stage. */
		struct VRControllerState {
			bool active = false;
			VRPose grip;
			VRPose aim;
			Vector3 linearVelocity{0.0F, 0.0F, 0.0F};
			bool velocityValid = false;

			float trigger = 0.0F;
			float squeeze = 0.0F;
			Vector2 thumbstick{0.0F, 0.0F};
			bool thumbstickClick = false;
			/** A on the right hand, X on the left. */
			bool buttonPrimary = false;
			/** B on the right hand, Y on the left. */
			bool buttonSecondary = false;
			bool menu = false;
		};

		struct VRFrameState {
			/** An XR frame was begun this iteration and has to be ended by the
			 * renderer's Flip. */
			bool running = false;
			/** The runtime wants pixels this frame. False while the headset is off
			 * the face or another app has focus; the frame is still submitted, empty. */
			bool shouldRender = false;
			/** Input is only delivered while the session is focused. */
			bool focused = false;

			/** Between the eyes, tracking space. */
			VRPose head;
			VRPose eyes[2];

			/** The frustum each eye is drawn with: the runtime's own, which is not
			 * centred (an eye sees further to its own side than towards the nose).
			 * Left and down are negative. With vr_symmetricFrustum it is instead the
			 * smallest centred frustum that covers the runtime's. */
			float eyeLeft[2] = {-1.0F, -1.0F}, eyeRight[2] = {1.0F, 1.0F};
			float eyeDown[2] = {-1.0F, -1.0F}, eyeUp[2] = {1.0F, 1.0F};
			bool eyeAsymmetric = false;
			/** The half-widths of the smallest centred frustum covering each eye's:
			 * for code that only needs a bound. */
			float eyeTanX[2] = {1.0F, 1.0F};
			float eyeTanY[2] = {1.0F, 1.0F};
			int eyeWidth = 0;
			int eyeHeight = 0;
			/** Seconds between the headset's frames: what a frame must fit in. */
			float displayPeriod = 1.0F / 90.0F;

			VRControllerState hands[2];

			/** Where the HUD panel hangs, in view space: centred straight ahead. */
			float hudDistance = 1.5F;
			float hudWidth = 1.6F;
			/** The front panel, in tracking space, this frame: hudDistance ahead
			 * of the head, turned with it, or (hudAnchored) standing in the room
			 * where it was put down while a menu is up. +Z faces the viewer. */
			VRPose hudPanel;
			bool hudAnchored = false;

			/** The wrist panel, in tracking space and facing the eyes. It is up
			 * only while the back of the off hand's wrist is turned towards them
			 * (VRPlaceWristPanel); `wristShown` says whether it is this frame. */
			bool wristShown = false;
			VRPose wristPanel;
			float wristWidth = 0.2F;
			/** The other hand is at the panel (VRWristReached): it grows into the
			 * wrist menu. `wristGrow` eases 0..1 as it does. */
			bool wristExpanded = false;
			float wristGrow = 0.0F;
		};

		/** The wrist panel's 2D canvas, in pixels. The HUD is drawn onto it as
		 * onto a small screen; its width in metres is vr_wristWidth. */
		constexpr int kVRWristCanvasWidth = 960;
		constexpr int kVRWristCanvasHeight = 600;

		/** The shape of a rumble over its time, where the runtime can play one
		 * (XR_FB_haptic_amplitude_envelope); elsewhere it is a plain rumble at its
		 * peak for its length, as every rumble was before. */
		enum class VRHapticShape {
			Flat,      // the same all through
			Kick,      // full at once, dying away: a shot
			DoubleTap, // two short knocks: a magazine seating
		};
		/** The shape's amplitudes, evenly spread over its time, into `out` (at
		 * most `maxSamples`; 500 a second, at least 2). Returns how many. */
		int VRHapticEnvelope(VRHapticShape shape, float amplitude, float seconds, float* out,
		                     int maxSamples);

		/** An input trace (vr_traceRecord, vr_traceReplay): one text line per
		 * frame holding what the headset and controllers said that frame (the
		 * head, both eyes, each controller's poses, velocity, trigger, grip,
		 * stick and buttons), in tracking space, as numbers separated by spaces.
		 * Text so a test can be written by a script as easily as recorded. The
		 * first line of a file is kVRTraceHeader. */
		constexpr const char* kVRTraceHeader = "zsvrtrace 1";
		std::string VRTraceFormat(const VRFrameState& fs);
		/** Reads a line into `fs`, changing only what a trace holds. False (and
		 * `fs` untouched) if the line is not a whole frame. */
		bool VRTraceParse(const std::string& line, VRFrameState& fs);

		/** The headset, as the game sees it. Null when there is no VR session. */
		class IVRSystem {
		public:
			virtual ~IVRSystem() = default;
			virtual const VRFrameState& GetFrameState() = 0;
			/** Rumble a controller. `amplitude` is 0..1. */
			virtual void Vibrate(int hand, float amplitude, float seconds) = 0;
			/** The big map is open and wants to be held (vr_heldMap): the wrist
			 * panel goes into the off hand from the next frame (VRPlaceHeldMap). */
			virtual void SetHeldMap(bool held) { (void)held; }
			/** The front panel shows the game's view-following overlays (names,
			 * markers through the panel) and so follows the head, for the next
			 * frame. Asked for each frame it is wanted: otherwise (any menu, the
			 * main screen) the panel stands in the room (VRFrameState::hudAnchored). */
			virtual void SetHudFollowsHead() {}
			/** Rumble with a shape (VRHapticShape); a plain rumble without one. */
			virtual void Vibrate(int hand, VRHapticShape shape, float amplitude, float seconds) {
				(void)shape;
				Vibrate(hand, amplitude, seconds);
			}
			/** True once after the session started or the user recentred from the
			 * runtime's menu: the game should re-anchor the play space. */
			virtual bool ConsumeRecenterRequest() { return false; }
			/** The controllers' interaction profile, as the runtime names it
			 * ("/interaction_profiles/valve/index_controller"); empty until known. */
			virtual std::string GetControllerProfile() const { return {}; }
			/** The headset, as the runtime names it ("Oculus Quest2"). */
			virtual std::string GetHeadsetName() const { return {}; }
		};

		IVRSystem* GetVRSystem();
		/** What the client tag says about the headset: "PCVR" with a PC
		 * headset's session running, "QuestVR" on a standalone (Android) one,
		 * empty on a monitor, `vr_enable` with no headset included. Added to the
		 * version string servers show (NetClient::SendVersion). */
		const char* VRClientTag();
		void SetVRSystem(IVRSystem*);

		/** The hand that holds the tools (vr_leftHanded). */
		int VRToolHand();

		/** Walking at `angle` (radians, clockwise from the facing) with only the
		 * server's eight directions: which one to hold now (0 forward, then
		 * clockwise in eighths). It alternates between the two either side, each
		 * choice held 1/20s, in the mix whose average walk is `angle`. `lean`,
		 * `hold` and `second` carry it from frame to frame; zero `lean` and `hold`
		 * when the walking stops. */
		int VRWalkDither(float angle, float dt, float& lean, float& hold, bool& second);

		/** True while a headset frame is being drawn this iteration. */
		inline bool IsVRFrameActive() {
			IVRSystem* vr = GetVRSystem();
			return vr && vr->GetFrameState().running;
		}

		/** Where a hand's aim ray meets the HUD panel.
		 * @param panelAspect the panel's height over its width
		 * @param uv          0..1 across the panel from its top-left corner
		 * @param distance    along the ray, in metres
		 * @return false when the ray misses the panel or points away from it */
		bool VRRayHitsHudPanel(const VRFrameState& fs, int hand, float panelAspect, Vector2& uv,
		                       float& distance);

		/** Places the wrist panel just over the back of `hand`'s wrist, facing
		 * the eyes and upright as they see it, and says whether it is up: the
		 * back of the wrist is turned towards the eyes, within arm's length and
		 * in view, the way you look at a watch. `wasShown` loosens each test, so
		 * the panel does not flicker on the edge of one. */
		bool VRPlaceWristPanel(const VRFrameState& fs, int hand, bool wasShown, VRPose& panel,
		                       bool inUse = false);

		/** The big map held in `hand` like a tablet (vr_heldMap): the wrist
		 * panel, widened to kVRHeldMapWidth, with its bottom edge just above the
		 * hand and its face to the eyes, level to the head. False without a hand
		 * or head pose. */
		constexpr float kVRHeldMapWidth = 0.32F;
		bool VRPlaceHeldMap(const VRFrameState& fs, int hand, VRPose& panel);

		/** Whether `hand` (the one without the watch) has come to the shown
		 * wrist panel to use it: its grip within 22cm of the panel, and it stays
		 * until it is 40cm away. */
		bool VRWristReached(const VRFrameState& fs, int hand, bool wasReached);

		/** Where `hand`'s aim ray meets the wrist panel, as `VRRayHitsHudPanel`
		 * does for the HUD panel. `distance` is from the controller, metres. */
		bool VRRayHitsWristPanel(const VRFrameState& fs, int hand, Vector2& uv, float& distance);

		/** Aim smoothing (vr_aimSmooth): a one-euro filter on a direction.
		 * Its cutoff rises with how fast the aim turns, so a still hand's tremor
		 * is filtered hard and a swing passes almost untouched.
		 *
		 * `Step` must be given the aim the hands give this frame, never what it
		 * returned: fed its own output it sees no motion and holds still (0.1.7
		 * did that, and the gun froze pointing one way). */
		class VRAimFilter {
		public:
			/** `raw` is a unit vector; `amount` 0..1, 0 passes `raw` through. */
			Vector3 Step(const Vector3& raw, float dt, float amount);
			void Reset() { active = false; }
			bool IsActive() const { return active; }
			const Vector3& Get() const { return value; }

		private:
			bool active = false;
			Vector3 value{1.0F, 0.0F, 0.0F};
			float speed = 0.0F; // rad/s, smoothed
		};

		/**
		 * Gun recoil in a headset. A monitor's recoil turns the view
		 * up by the weapon's amount a shot, and it stays up until the mouse pulls
		 * it down. A headset cannot turn the view, so the gun turns instead, as in
		 * Pavlov, Onward and H3VR: the aim follows the gun, so recoil costs
		 * accuracy as it does on a monitor.
		 *
		 * Each shot's turn is split in two. A kick springs back in about a fifth
		 * of a second: the snap you feel. A climb (the larger part) does not
		 * start back until a moment after the last shot (kClimbHold), so while
		 * the trigger is held on an SMG it builds as the view does on a monitor
		 * (13 degrees in three seconds standing; a monitor's 21 has no hand
		 * steadying it), and is held down the same way: by pulling the hand
		 * down. Once you stop it comes back within a second; a rifle's settles
		 * between shots. Both are critically damped springs, so nothing
		 * overshoots below where you aimed.
		 */
		class VRRecoil {
		public:
			/** One shot: `pitch` up and `yaw` to the left (radians, the monitor's
			 * per-shot recoil, already scaled for bracing). */
			void Kick(float pitch, float yaw);
			void Step(float dt);
			void Reset() { *this = VRRecoil(); }
			/** The gun's turn now, radians: up, and left. */
			float Pitch() const { return kick[0] + climb[0]; }
			float Yaw() const { return kick[1] + climb[1]; }

			/** How the turn is shared and how fast each part returns (rad/s). */
			static constexpr float kKickShare = 0.4F;
			static constexpr float kKickRate = 18.0F;
			static constexpr float kClimbRate = 4.0F;
			/** Seconds after a shot before the climb starts back: longer than an
			 * SMG's 0.1 s between shots, so a burst keeps all of it. */
			static constexpr float kClimbHold = 0.12F;
			/** The climb stops building here (radians, about 30 degrees). */
			static constexpr float kClimbMax = 0.52F;

		private:
			float sinceShot = 1.0E3F;
			// [0] pitch, [1] yaw; each a spring's position and velocity.
			float kick[2] = {0.0F, 0.0F}, kickVel[2] = {0.0F, 0.0F};
			float climb[2] = {0.0F, 0.0F}, climbVel[2] = {0.0F, 0.0F};
		};

		/** The factor on a shot's recoil (as Player worked it out for a monitor)
		 * for how the gun is held. Never less than a monitor player gets: recoil
		 * is the client's own doing, nothing on the server checks it, so a client
		 * that softened it would be a cheat. A braced gun (two hands or the
		 * stock) counts as aiming down the sights, which on a monitor only spares
		 * the doubling for walking; Player doubled it when the sights were not
		 * up, so for a braced gun that doubling is taken back, and nothing else
		 * is. */
		float VRRecoilHoldScale(bool braced, bool walking, bool sightsUp);

		/** The comfort vignette (vr_vignette): how much of it the body's
		 * artificial motion asks for, 0 to 1. `groundSpeed` is the body's
		 * horizontal speed and `fallSpeed` its speed downwards, both blocks per
		 * second from its velocity (the stick, a fall, a push; walking in the room
		 * moves the body without a velocity, so it never counts). `turnRate` is
		 * smooth turning's rate, radians per second (a snap turn is a cut, and
		 * gives none). Standing still gives 0. */
		float VRVignetteTarget(float groundSpeed, float fallSpeed, float turnRate);
		/** Steps the vignette towards `target`: in quickly (0.15s), out slowly
		 * (0.5s), so it does not flicker between the steps of a walk. */
		float VRVignetteStep(float current, float target, float dt);
		/** The half-angle of the clear circle in the middle of the view, radians,
		 * at `amount` of the vignette with the setting at `strength` (0 to 1):
		 * 60 degrees (past a headset's view sideways: only its corners close) at
		 * 0, down to 20 degrees at full amount and strength; the default strength
		 * of 0.5 closes to 40. */
		float VRVignetteClearAngle(float amount, float strength);

		/** A held tool is solid against the map. `solid` says whether a point
		 * (world space) is inside a block. The hand is stopped where the line
		 * from `anchor` (the eyes: known clear) to it first meets a block, and the
		 * tool, reaching `reach` along `forward` from the hand, is pulled back
		 * along itself until its tip is clear, as a gun pushed at a wall slides
		 * back in the hands. Returns the offset to move the hand by. `margin`
		 * keeps what is stopped that far off the block. */
		template <class Solid>
		Vector3 VRToolPushback(const Vector3& anchor, const Vector3& hand, const Vector3& forward,
		                       float reach, float margin, Solid solid) {
			const float step = 0.04F;
			Vector3 at = hand;
			// The hand, on the line from the eyes.
			Vector3 d = hand - anchor;
			const float len = d.GetLength();
			if (len > 1.0E-4F) {
				d = d * (1.0F / len);
				for (float t = step; t < len + step; t += step) {
					const float u = t < len ? t : len;
					if (solid(anchor + d * u)) {
						at = anchor + d * std::max(0.0F, u - step - margin);
						break;
					}
				}
			}
			// The tool, along itself from the (stopped) hand.
			if (reach > 0.0F) {
				for (float t = step; t < reach + step; t += step) {
					const float u = t < reach ? t : reach;
					if (solid(at + forward * u)) {
						const float back = std::min(reach, reach - (u - step) + margin);
						at = at - forward * back;
						break;
					}
				}
			}
			return at - hand;
		}

		/** The places on the body tools are carried (VRHolsterAt). */
		enum VRHolster {
			VRHolsterNone = -1,
			VRHolsterGun,
			VRHolsterSpade,
			VRHolsterBlocks,
			VRHolsterGrenades
		};

		/** Which holster `hand`'s grip is at, if any: the gun over that hand's
		 * shoulder, the spade over the other one, blocks at that hand's hip and
		 * grenades at the other. Placed from the eyes, turned with the head but
		 * level. `hover` (the holster the hand was at last frame) gets a little
		 * more room to leave than to enter, so its edge does not flicker. */
		int VRHolsterAt(const VRFrameState& fs, int hand, int hover);

		/** The shoulder stock, between frames (VRStockUpdate). */
		struct VRStockState {
			bool on = false;
			/** 0 the hands' aim, 1 the stock's, eased between. */
			float blend = 0.0F;
		};

		/** One frame of the shoulder stock. `allowed`: a rifle in both hands.
		 * `distance`: the gun hand from the shoulder point, metres. `angle`: how
		 * far the stock's line (shoulder through front hand) is off the hands'
		 * own (gun hand through front hand), radians. It takes hold only with
		 * the hand right at the shoulder and the gun already along the stock's
		 * line, so taking hold hardly moves it, and eases in and out over a
		 * fifth of a second. Testers found the old stock (any hand within 22cm,
		 * at once) grabbing and tilting the gun as they moved. Returns true the
		 * frame it takes hold (the buzz). */
		bool VRStockUpdate(VRStockState& s, bool allowed, float distance, float angle, float dt);

		/** The gun hand's grip, between frames (VRGripUpdate). */
		struct VRGripState {
			bool prev = false;
			/** Held since it took a tool from a holster. */
			bool fromHolster = false;
			/** Held since it began as the tool's second action (dig, a line). */
			bool action = false;
		};

		/** One frame of the gun hand's grip. `grip`: it is squeezed. `slot`: the
		 * holster (VRHolster) the hand is at, already left out where none may
		 * count (VRGripMayHolster). Returns the holster whose tool to take this
		 * frame, or VRHolsterNone. A holster counts only for a grip that begins
		 * there; a grip that began as the tool's action stays that until let go,
		 * wherever the hand goes (a hand hanging at the hip while a wall was
		 * drawn took blocks, and the line stopped). */
		int VRGripUpdate(VRGripState& s, bool grip, int slot);

		/** Whether a holster may count now: not while a grip is held (it began
		 * elsewhere), not with the gun in both hands, not while the spade or
		 * blocks have a block in reach (`toolHasReach`: the grip is theirs). */
		inline bool VRGripMayHolster(const VRGripState& s, bool twoHanded, bool toolHasReach) {
			return !s.action && !s.fromHolster && !twoHanded && !toolHasReach;
		}

		/** Tracking axes to the game's axes before the body yaw is applied:
		 * OpenXR's forward (-Z) becomes +x, right (+X) becomes +y and up (+Y)
		 * becomes -z, since the game's z points down. A proper rotation
		 * (determinant +1), so handedness survives. */
		inline Vector3 VRTrackingToGameAxes(const Vector3& v) { return MakeVector3(-v.z, v.x, -v.y); }
	} // namespace client
} // namespace spades
