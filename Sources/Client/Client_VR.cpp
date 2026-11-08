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

// Playing with a headset. Two jobs:
//
//  - Before the world update, UpdateVR reads the controllers and turns them
//    into the input a keyboard would have produced (through KeyEvent, with the
//    player's own key bindings, so every action goes down the same path it
//    always did), sets the facing the server sees, and walks the body wherever
//    the player physically walked.
//  - When the scene is set up, ApplyVRToSceneDefinition puts the camera in the
//    head and hands the renderer one view per eye.
//
// The facing the server sees follows the head, and follows the gun hand while
// a tool is in use. The server checks every hit against that facing, and AoS
// scales walking speed by it and refuses to climb while looking steeply down;
// a hand held low would otherwise slow and pin the player. See VR.md.

#include <algorithm>
#include <cmath>

#include "Client.h"
#include <Training/TrainingModes.h>

#include <Core/Bitmap.h>
#include <Core/Settings.h>
#include <Core/Strings.h>

#include "ChatWindow.h"
#include "ClientPlayer.h"
#include "ClientUI.h"
#include "LimboView.h"
#include "MapView.h"
#include "Fonts.h"
#include "IFont.h"
#include "IAudioChunk.h"
#include "IAudioDevice.h"
#include "NetClient.h"
#include "Weapon.h"
#include "GameMap.h"
#include "World.h"
#include <Gui/TrainingHost.h>

// World blocks per real metre. 1.4 puts a 1.6m eye close to the game's 2.25.
DEFINE_SPADES_SETTING(vr_worldScale, "1.4");
// Degrees per flick of the turn stick; 0 turns smoothly instead.
DEFINE_SPADES_SETTING(vr_snapTurn, "45");
DEFINE_SPADES_SETTING(vr_smoothTurnSpeed, "150");
// How far below the standing height the head has to go to crouch, metres.
// 0.27: testers found 0.35 (as vr_crouchDepth, renamed so the new default
// reaches configs that saved the old one) too deep a duck.
DEFINE_SPADES_SETTING(vr_crouchAt, "0.27");
// Playing sitting down: the head dropping never crouches (leaning forward in a
// chair would), only the crouch button does.
DEFINE_SPADES_SETTING(vr_seated, "0");
// Walking in the room moves the body. Off, the view stays at the body's eye
// whatever the head does (it turns, it does not move: no peeking, see VR.md).
DEFINE_SPADES_SETTING(vr_roomScale, "1");
// Hand speed, metres per second, that throws a grenade as far as a normal throw.
DEFINE_SPADES_SETTING(vr_throwSpeed, "5");
// Hold the tools in the left hand.
DEFINE_SPADES_SETTING(vr_leftHanded, "0");
// The player HUD (health, ammo, tools, minimap) on a panel over the off hand's
// wrist, shown when you look at it; 0 puts it on the front panel instead.
DEFINE_SPADES_SETTING(vr_wristHud, "1");
DEFINE_SPADES_SETTING(vr_wristWidth, "0.24");
// Degrees the tools point below the controller's own aim: held naturally, a
// Touch controller's aim ray points higher than a gun in that hand would.
DEFINE_SPADES_SETTING(vr_aimTilt, "15");
// Squeeze the off hand's grip on a gun's barrel to hold it in both hands.
DEFINE_SPADES_SETTING(vr_twoHandedGrip, "1");
// The off hand's trigger, held on a gun's barrel, braces it on the shoulder.
DEFINE_SPADES_SETTING(vr_shoulderStock, "1");
// 0 to 1: how much the gun's aim is steadied when the hand is nearly still.
// Off by default: testers wanted the gun and its laser rigid in the hand
// (renamed from vr_aimSmooth, 0.5, so saved configs take the new default).
DEFINE_SPADES_SETTING(vr_aimSmooth, "0");
// Swinging the spade faster than this (m/s) hits, as the trigger does; 0: off.
DEFINE_SPADES_SETTING(vr_swingSpeed, "2.5");
// Tools on the body: squeeze the gun hand's grip over a shoulder or at a hip.
DEFINE_SPADES_SETTING(vr_holsters, "1");
// The game's frame rate on the wrist.
DEFINE_SPADES_SETTING(vr_showFps, "1");
// Dead or following someone: the view holds still and moves in blinks.
DEFINE_SPADES_SETTING(vr_comfortCamera, "1");
// Walking, falling or turning smoothly close in the edges of the view; how far,
// 0 (off) to 1. Room-scale walking and snap turns never do.
DEFINE_SPADES_SETTING(vr_vignette, "0.5");
// The big map is held in the off hand like a tablet, not hung in front of you.
DEFINE_SPADES_SETTING(vr_heldMap, "1");
// B drops the magazine; the off hand takes one from the belt and puts it in.
// Off: B reloads, as R does. (Was vr_physicalReload, on by default: renamed
// so the new default reaches configs that saved the old one. Testers found two
// actions for every reload too many.)
DEFINE_SPADES_SETTING(vr_reloadGesture, "0");
// The off hand's X and Y step through the tools, for when the holsters are out
// of reach (seated). Off: X grabs a block's colour and Y opens the big map.
DEFINE_SPADES_SETTING(vr_toolButtons, "0");
// Sprint while the walking stick is held in, instead of a click toggling it.
DEFINE_SPADES_SETTING(vr_sprintHold, "0");
// A magnifying scope on the rifle, seen when an eye looks down its iron sights.
DEFINE_SPADES_SETTING(vr_scope, "1");
// Held tools do not go through blocks: they stop at them, and slide back.
// Off by default for now (was vr_solidTools, on; renamed so the new default
// reaches configs that saved the old one): aiming round cover, the gun
// stopped at the corner and slid back, and the aim went with it.
DEFINE_SPADES_SETTING(vr_toolCollision, "0");

SPADES_SETTING(cg_keyAttack);
SPADES_SETTING(cg_keyAltAttack);
SPADES_SETTING(cg_keyJump);
SPADES_SETTING(cg_keyCrouch);
SPADES_SETTING(cg_keySprint);
SPADES_SETTING(cg_keyReloadWeapon);
SPADES_SETTING(cg_keyMoveForward);
SPADES_SETTING(cg_keyMoveBackward);
SPADES_SETTING(cg_keyMoveLeft);
SPADES_SETTING(cg_keyMoveRight);
SPADES_SETTING(cg_keyScoreboard);
SPADES_SETTING(cg_keyToggleMapZoom);
SPADES_SETTING(cg_keyPaletteLeft);
SPADES_SETTING(cg_keyPaletteRight);
SPADES_SETTING(cg_mouseSensitivity);

namespace spades {
	namespace client {

		namespace {
			/** Trigger hysteresis, as for the menu pointer. */
			bool TriggerState(float value, bool wasDown) {
				return wasDown ? value > 0.55F : value > 0.75F;
			}

			/** The name KeyEvent receives for a key setting. The space bar is bound
			 * by name ("Space") but arrives as " ". */
			std::string KeyEventName(const std::string& setting) {
				if (EqualsIgnoringCase(setting, "space") || EqualsIgnoringCase(setting, "spacebar") ||
				    EqualsIgnoringCase(setting, "spacekey"))
					return " ";
				return setting;
			}

			float Yaw(const Vector3& v) { return std::atan2(v.y, v.x); }
		} // namespace

		int VRToolHand() { return (int)vr_leftHanded ? VRHandLeft : VRHandRight; }

		Vector3 Client::VRToWorld(const Vector3& v) const {
			Vector3 g = VRTrackingToGameAxes(v) * vrRig.scale;
			float c = std::cos(vrRig.bodyYaw), s = std::sin(vrRig.bodyYaw);
			return MakeVector3(g.x * c - g.y * s, g.x * s + g.y * c, g.z);
		}

		void Client::VRSetKey(const std::string& setting, bool down) {
			if (setting.empty())
				return;
			std::string key = KeyEventName(setting);
			bool& held = vrHeldKeys[key];
			if (held == down)
				return;
			held = down;
			KeyEvent(key, down);
		}

		void Client::VRReleaseAllKeys() {
			// Copy first: KeyEvent may open a menu that changes nothing here, but
			// the map must not be walked while it is written to.
			std::vector<std::string> held;
			for (const auto& k : vrHeldKeys)
				if (k.second)
					held.push_back(k.first);
			vrHeldKeys.clear();
			for (const auto& k : held)
				KeyEvent(k, false);
		}

		void Client::RecenterVR(const VRFrameState& fs, bool deliberate) {
			// A recentre the player did not ask for (the app getting focus, the
			// runtime's own recentre) can come while they are bent over or the
			// headset is off their head. It may raise the standing height but not
			// drop it by a crouch or more. Holding both sticks in is believed.
			const float height = fs.head.position.y;
			const bool keepHeight =
			  vrRig.calibrated && !deliberate &&
			  height < vrRig.standingHeight - std::max(0.1F, (float)vr_crouchAt);
			if (!keepHeight)
				vrRig.standingHeight = height;
			vrRig.anchorX = fs.head.position.x;
			vrRig.anchorZ = fs.head.position.z;

			// The first time, turn the play space so the way the player is looking
			// is the way their character faces. Later recentres only move the
			// floor and the body, never the heading, so nothing swings round.
			if (!vrRig.calibrated) {
				float headYaw = Yaw(VRTrackingToGameAxes(-fs.head.Axis(2)));
				float facing = 0.0F;
				if (world) {
					auto p = world->GetLocalPlayer();
					if (p && p->GetFront().GetLength2D() > 0.01F)
						facing = Yaw(p->GetFront());
				}
				vrRig.bodyYaw = facing - headYaw;
			}
			vrRig.calibrated = true;
			SPLog("[VR] Play space recentred (%s; eye height %.2fm%s)",
			      deliberate ? "sticks held" : "automatic", vrRig.standingHeight,
			      keepHeight ? ", kept: the head was lower than a crouch" : "");
		}

		void Client::UpdateVRRigTransforms(const VRFrameState& fs, const Vector3& origin) {
			vrRig.camera = origin;
			vrRig.headAxes[0] = VRToWorld(-fs.head.Axis(2)).Normalize();
			vrRig.headAxes[1] = VRToWorld(fs.head.Axis(0)).Normalize();
			vrRig.headAxes[2] = VRToWorld(-fs.head.Axis(1)).Normalize();

			// The hands keep their real offset from the eyes, scaled with the
			// world, whatever the body did: that is what makes them feel like
			// your own hands.
			for (int h = 0; h < 2; h++) {
				const VRControllerState& c = fs.hands[h];
				vrRig.handValid[h] = c.grip.valid;
				if (!c.grip.valid)
					continue;
				const VRPose& orient = c.aim.valid ? c.aim : c.grip;
				vrRig.handPos[h] = origin + VRToWorld(c.grip.position - fs.head.position);
				vrRig.aimPos[h] = origin + VRToWorld((c.aim.valid ? c.aim.position
				                                                  : c.grip.position) -
				                                     fs.head.position);
				// Turned down about the hand's right by vr_aimTilt. Everything held
				// follows these axes (the gun you see, where it shoots, what others
				// see), so they agree with each other at any tilt.
				Vector3 fwd = VRToWorld(-orient.Axis(2)).Normalize();
				Vector3 down = VRToWorld(-orient.Axis(1)).Normalize();
				const float tilt = DEG2RAD(Clamp((float)vr_aimTilt, -45.0F, 45.0F));
				const float ct = std::cos(tilt), st = std::sin(tilt);
				vrRig.handAxes[h][0] = (fwd * ct + down * st).Normalize();
				vrRig.ownForward[h] = vrRig.handAxes[h][0];
				vrRig.handAxes[h][1] = VRToWorld(orient.Axis(0)).Normalize();
				vrRig.handAxes[h][2] = (down * ct - fwd * st).Normalize();
				vrRig.handVelocity[h] =
				  c.velocityValid ? VRToWorld(c.linearVelocity) : MakeVector3(0, 0, 0);
			}
			// Again when the scene is built (ApplyVRToSceneDefinition), so the gun
			// drawn is the gun aimed. The filter's state moves once a frame
			// (UpdateVRAimAssist); here its result is only put back.
			if (vrRig.twoHanded)
				ApplyVRTwoHandedAim();
			if (vrRig.aimFilter.IsActive())
				SetVRToolForward(vrRig.aimFilter.Get());
			ApplyVRRecoil();
			ApplyVRToolCollision();
		}

		float Client::VRToolReach(Player& p) const {
			// Read off the voxel models, grip to tip, in blocks.
			switch (p.GetTool()) {
				case Player::ToolWeapon:
					switch (p.GetWeaponType()) {
						case SMG_WEAPON: return 0.6F;
						case SHOTGUN_WEAPON: return 0.8F;
						default: return 0.85F;
					}
				case Player::ToolSpade: return 0.85F;
				case Player::ToolBlock: return 0.2F;
				case Player::ToolGrenade: return 0.1F;
			}
			return 0.0F;
		}

		void Client::ApplyVRToolCollision() {
			// A hand in a headset can go anywhere the arm reaches, walls
			// included; the game's own tools stopped at the view. Here the
			// hands stop at blocks, and so does whatever the tool hand holds:
			// pushed at a wall, a gun slides back through the hands, as in Boneworks
			// or Pavlov. The ray the tool acts on starts at the moved hand, so
			// nothing is shot, dug or built from inside a wall. Players are not
			// solid: they would be in the way all the time.
			vrRig.toolBlocked = false;
			if (!(int)vr_toolCollision || !vrRig.solidHands || !world || !world->GetMap())
				return;
			const GameMap& map = *world->GetMap();
			auto solid = [&](const Vector3& v) { return map.ClipBox(v.x, v.y, v.z); };
			if (solid(vrRig.camera))
				return; // the eyes are in something already: no clear side to push to
			const int th = VRToolHand(), oh = 1 - th;
			const float margin = 0.03F;
			if (vrRig.handValid[th]) {
				Vector3 push = VRToolPushback(vrRig.camera, vrRig.handPos[th], vrRig.handAxes[th][0],
				                              vrRig.toolReach, margin, solid);
				if (push.GetLength() > 1.0E-4F) {
					vrRig.handPos[th] += push;
					vrRig.aimPos[th] += push;
					vrRig.toolBlocked = true;
					// The hand on the barrel goes with the gun.
					if (vrRig.twoHanded && vrRig.handValid[oh]) {
						vrRig.handPos[oh] += push;
						vrRig.aimPos[oh] += push;
					}
				}
			}
			if (vrRig.handValid[oh]) {
				Vector3 push = VRToolPushback(vrRig.camera, vrRig.handPos[oh], vrRig.handAxes[oh][0],
				                              0.0F, margin, solid);
				vrRig.handPos[oh] += push;
				vrRig.aimPos[oh] += push;
			}
		}

		void Client::SetVRToolForward(const Vector3& forward) {
			const int th = VRToolHand();
			if (!vrRig.handValid[th] || forward.GetLength() < 1.0E-4F)
				return;
			const Vector3 f = forward.Normalize();
			// The hand keeps its roll: its down, squared to the new forward.
			Vector3 down = vrRig.handAxes[th][2];
			down = down - f * Vector3::Dot(down, f);
			if (down.GetLength() < 1.0E-3F)
				down = MakeVector3(0.0F, 0.0F, 1.0F) - f * f.z;
			down = down.Normalize();
			vrRig.handAxes[th][0] = f;
			vrRig.handAxes[th][1] = Vector3::Cross(down, f).Normalize();
			vrRig.handAxes[th][2] = down;
		}

		Vector3 Client::VRTwoHandedForward() const {
			// The hands' line, turned towards the stock's as far as it is eased in.
			const int th = VRToolHand(), oh = 1 - th;
			Vector3 hands = vrRig.handPos[oh] - vrRig.handPos[th];
			hands = hands.GetLength() > 1.0E-4F ? hands.Normalize() : vrRig.ownForward[th];
			if (vrRig.stock.blend <= 0.0F)
				return hands;
			Vector3 stock = vrRig.handPos[oh] - VRShoulderPoint();
			stock = stock.GetLength() > 1.0E-4F ? stock.Normalize() : hands;
			const Vector3 v = hands * (1.0F - vrRig.stock.blend) + stock * vrRig.stock.blend;
			return v.GetLength() > 1.0E-4F ? v.Normalize() : hands;
		}

		Vector3 Client::VRShoulderPoint() const {
			// Where a stock sits: below the eyes on the gun hand's side, a little
			// back, in a frame that turns with the head but stays level, so
			// nodding does not swing the gun.
			Vector3 fwd = vrRig.headAxes[0];
			fwd.z = 0.0F;
			fwd = fwd.GetLength() > 0.01F ? fwd.Normalize() : MakeVector3(1, 0, 0);
			const Vector3 right = MakeVector3(-fwd.y, fwd.x, 0.0F);
			const float side = VRToolHand() == VRHandRight ? 1.0F : -1.0F;
			const float s = vrRig.scale;
			return vrRig.camera + MakeVector3(0.0F, 0.0F, 0.20F * s) + right * (0.13F * side * s) -
			       fwd * (0.04F * s);
		}

		void Client::UpdateVRAimAssist(Player& p, float dt) {
			const int th = VRToolHand(), oh = 1 - th;

			// The stock. Pavlov, H3VR and Onward all have one: held in both hands
			// with the gun hand at the shoulder, the gun aims from the shoulder
			// through the front hand. The body sways much less than two wrists,
			// so the line is steadier; it is what a real stock does. Any gun, while
			// the off hand's trigger is held on its barrel (testers asked for a
			// stock weld on every gun), eased in (VRStockUpdate). Until 0.3 the
			// rifle also took the stock by itself, with its hand brought to the
			// shoulder along the stock's line, and took it again, unasked, when the
			// gun came back that way after a brace (testers' report): gone.
			const bool brace = (int)vr_shoulderStock && vrRig.twoHanded && p.IsToolWeapon() &&
			                   vrRig.handValid[th] && vrRig.handValid[oh] && vrRig.braceHeld;
			if (VRStockUpdate(vrRig.stock, brace, brace ? 0.0F : 1.0E3F, brace ? 0.0F : 3.2F, dt))
				VRPulse(th, 0.15F, 0.03F);
			vrRig.shouldered = vrRig.stock.on;

			// The aim the hands give this frame: from the shoulder through the
			// front hand, from hand to hand, or the gun hand's own. Worked out
			// afresh, never read back off the hand's axes: those already carry
			// last frame's smoothed aim, and a filter fed its own output holds
			// still (0.1.7's frozen rifle).
			Vector3 raw = vrRig.ownForward[th];
			if (vrRig.twoHanded && vrRig.handValid[oh])
				raw = VRTwoHandedForward();

			// Adaptive smoothing (VRAimFilter), as VR shooters' aim smoothing
			// does it. Guns only: a lagging spade or block would feel wrong.
			const float amount = p.IsToolWeapon() && vrRig.handValid[th] ? (float)vr_aimSmooth
			                                                             : 0.0F;
			SetVRToolForward(vrRig.aimFilter.Step(raw, dt, amount));

			// Recoil after the smoothing: the filter is fed the hands' aim, never
			// the kick, or it would smooth the kick away.
			vrRig.recoilOn = p.IsToolWeapon() && vrRig.handValid[th];
			if (vrRig.recoilOn)
				vrRig.recoil.Step(dt);
			else
				vrRig.recoil.Reset();
			ApplyVRRecoil();
		}

		void Client::ApplyVRRecoil() {
			if (!vrRig.recoilOn)
				return;
			const float pitch = vrRig.recoil.Pitch(), yaw = vrRig.recoil.Yaw();
			if (pitch == 0.0F && yaw == 0.0F)
				return;
			const Vector3 f = vrRig.handAxes[VRToolHand()][0];
			// Up (the map's -z) squared to the aim: the muzzle lifts towards the
			// sky whatever the wrist's roll, as the view does on a monitor.
			Vector3 up = MakeVector3(0.0F, 0.0F, -1.0F) + f * f.z;
			if (up.GetLength() < 1.0E-3F)
				up = vrRig.handAxes[VRToolHand()][2] * -1.0F;
			up = up.Normalize();
			Vector3 g = f * std::cos(pitch) + up * std::sin(pitch);
			// Sideways about the vertical, the way Player::Turn turns the view.
			const float c = std::cos(yaw), sn = std::sin(yaw);
			g = MakeVector3(g.x * c - g.y * sn, g.x * sn + g.y * c, g.z);
			SetVRToolForward(g);
		}

		void Client::PlayerRecoiledInVR(Player& p, float yaw, float pitch) {
			if (!p.IsLocalPlayer() || !IsVRFrameActive())
				return;
			// Exactly a monitor's recoil, with bracing as its aiming down the sights
			// (VRRecoilHoldScale). There is no setting for it: softer recoil than
			// a monitor's would be a cheat.
			const float scale =
			  VRRecoilHoldScale(vrRig.twoHanded || vrRig.shouldered, p.IsWalking(),
			                    p.GetWeaponInput().secondary);
			vrRig.recoil.Kick(pitch * scale, yaw * scale);
		}

		namespace {
			// Quick chat on the wrist menu: what gets said most, one touch each.
			const char* const kQuickPhrases[8] = {"Help!",      "Enemy spotted!", "Need blocks",
			                                      "On my way",  "Follow me",      "Defend here",
			                                      "Thanks!",    "gg"};
		} // namespace

		const char* Client::VRQuickPhrase(int i) { return kQuickPhrases[i & 7]; }

		std::vector<Client::VRWristButton> Client::VRWristMenuLayout() const {
			// The wrist canvas (960x600): chat on the left, colours on the right.
			std::vector<VRWristButton> b;
			const float m = 16.0F, gap = 10.0F;
			const float lw = 460.0F, bw = (lw - gap) * 0.5F;
			b.push_back({VRWristButton::Chat, 0, AABB2(m, m, bw, 84.0F), {}});
			b.push_back({VRWristButton::TeamChat, 0, AABB2(m + bw + gap, m, bw, 84.0F), {}});
			const float py = m + 84.0F + 18.0F, ph = 70.0F;
			for (int i = 0; i < 8; i++) {
				const float x = m + static_cast<float>(i % 2) * (bw + gap);
				const float y = py + static_cast<float>(i / 2) * (ph + gap);
				b.push_back({VRWristButton::Phrase, i, AABB2(x, y, bw, ph), {}});
			}
			const float qy = py + 4.0F * (ph + gap) + 6.0F;
			b.push_back({VRWristButton::QuickTarget, 0, AABB2(m, qy, lw, 64.0F), {}});
			// The game's menu and the team and weapon screen: the headset's own
			// menu buttons belong to the runtime (Quest, SteamVR).
			const float tw = (lw - gap * 2.0F) / 3.0F;
			b.push_back({VRWristButton::Menu, 0, AABB2(m, qy + 74.0F, tw, 66.0F), {}});
			// The headlamp; a squeeze of the off hand at the head switches it too.
			b.push_back({VRWristButton::Light, 0, AABB2(m + tw + gap, qy + 74.0F, tw, 66.0F), {}});
			b.push_back(
			  {VRWristButton::Team, 0, AABB2(m + (tw + gap) * 2.0F, qy + 74.0F, tw, 66.0F), {}});

			// No palette (testers: clutter on the watch); X and Y step through the
			// colours (vr_toolButtons 0).
			return b;
		}

		void Client::UpdateVRWrist(const VRFrameState& fs, float dt, bool menu) {
			// The other hand at the wrist grows the panel into a menu
			// (OpenXRSystem); here the tool hand uses it. Point and pull the
			// trigger, or just touch a button with the controller's tip.
			const int th = VRToolHand();
			vrRig.wristSendCooldown = std::max(0.0F, vrRig.wristSendCooldown - dt);
			const bool trig = TriggerState(fs.hands[th].trigger, vrRig.wristTriggerHeld);
			const bool trigPressed = trig && !vrRig.wristTriggerHeld;
			vrRig.wristTriggerHeld = trig;
			if (!trig)
				vrRig.wristTriggerOwned = false;

			vrRig.wristMenu = !menu && world && fs.wristShown && fs.wristExpanded;
			if (!menu && world && fs.wristShown)
				MarkTrainingVRStep(training::VRStepWrist);
			vrRig.wristPointing = false;
			int hover = -1;
			Vector2 uv;
			float dist = 1.0F;
			if (vrRig.wristMenu && VRRayHitsWristPanel(fs, th, uv, dist) && dist < 0.6F) {
				vrRig.wristPointing = true;
				vrRig.wristPointDistance = dist;
				vrRig.wristCursor =
				  MakeVector2(uv.x * static_cast<float>(kVRWristCanvasWidth),
				              uv.y * static_cast<float>(kVRWristCanvasHeight));
				const std::vector<VRWristButton> buttons = VRWristMenuLayout();
				for (std::size_t i = 0; i < buttons.size(); i++)
					if (buttons[i].rect.Contains(vrRig.wristCursor))
						hover = static_cast<int>(i);
				if (hover != vrRig.wristHover && hover >= 0)
					VRPulse(th, 0.15F, 0.015F);
				// A touch: the tip comes to the panel (2cm), and has to leave it
				// (5cm) before it can touch again.
				const bool poke = dist < 0.02F && vrRig.wristPokeArmed;
				if (poke)
					vrRig.wristPokeArmed = false;
				if (trigPressed)
					vrRig.wristTriggerOwned = true;
				if ((trigPressed || poke) && hover >= 0) {
					VRPulse(th, 0.5F, 0.04F);
					PressVRWristButton(buttons[static_cast<std::size_t>(hover)]);
				}
			}
			if (dist > 0.05F)
				vrRig.wristPokeArmed = true;
			vrRig.wristHover = hover;
		}

		void Client::PressVRWristButton(const VRWristButton& b) {
			switch (b.kind) {
				case VRWristButton::Chat:
					// The chat field opens in front, with the keyboard under it.
					scriptedUI->EnterGlobalChatWindow();
					break;
				case VRWristButton::TeamChat: scriptedUI->EnterTeamChatWindow(); break;
				case VRWristButton::Phrase:
					// Once a second at most: a hand resting on the panel should not
					// flood the chat.
					if (vrRig.wristSendCooldown <= 0.0F && activeNet) {
						activeNet->SendChat(_Tr("Client", kQuickPhrases[b.index]),
						                    !vrRig.wristToTeam);
						vrRig.wristSendCooldown = 1.0F;
					}
					break;
				case VRWristButton::QuickTarget: vrRig.wristToTeam = !vrRig.wristToTeam; break;
				case VRWristButton::Colour: SetBlockColor(b.color); break;
				case VRWristButton::Menu: scriptedUI->EnterClientMenu(); break;
				case VRWristButton::Light: ToggleLocalFlashlight(); break;
				case VRWristButton::Team:
					// As the limbo key (cg_keyLimbo) does.
					inGameLimbo = true;
					limboMenu->Open();
					break;
			}
		}


		bool Client::UpdateVRReload(Player& p, const VRFrameState& fs) {
			// Pressing B is a reload in the game; here it is where one begins. B
			// drops the magazine; the off hand's grip takes a new one from the
			// belt (anywhere at the waist); bringing it to the magazine well, just
			// under the front of the gun, puts it in, and that is the game's reload,
			// with its usual time to finish. B again at any point just reloads.
			// Nothing here reaches the server: it sees the ordinary reload.
			const int toolHand = VRToolHand(), offHand = 1 - toolHand;
			const VRControllerState& T = fs.hands[toolHand];
			const VRControllerState& O = fs.hands[offHand];
			const bool bPressed = T.buttonSecondary && !vrRig.prevSecondary[toolHand];
			const bool offPressed = O.squeeze > 0.6F && !vrRig.prevOffSqueeze;
			vrRig.prevOffSqueeze = O.squeeze > 0.6F;

			if (!(int)vr_reloadGesture) {
				vrRig.reloadStage = 0;
				return T.buttonSecondary;
			}
			Weapon& w = p.GetWeapon();
			if (!p.IsToolWeapon() || w.IsReloading()) {
				vrRig.reloadStage = 0;
				return false;
			}

			if (bPressed) {
				if (vrRig.reloadStage != 0) {
					vrRig.reloadStage = 0;
					return true;
				}
				if (w.GetStock() > 0 && w.GetAmmo() < w.GetClipSize()) {
					vrRig.reloadStage = 1;
					VRPulse(toolHand, 0.4F, 0.05F);
				}
				return false;
			}

			if (vrRig.reloadStage == 1 && offPressed && O.grip.valid && fs.head.valid) {
				// At the waist: well below the eyes, and close to the body.
				Vector3 d = O.grip.position - fs.head.position;
				const bool atBelt = d.y < -0.35F && std::sqrt(d.x * d.x + d.z * d.z) < 0.45F;
				if (atBelt) {
					vrRig.reloadStage = 2;
					VRPulse(offHand, 0.5F, 0.05F);
				}
			} else if (vrRig.reloadStage == 2) {
				if (O.squeeze < 0.3F) {
					vrRig.reloadStage = 1; // dropped it: fetch another
				} else if (vrRig.handValid[0] && vrRig.handValid[1]) {
					const float s = vrRig.scale;
					const Vector3 well = vrRig.handPos[toolHand] +
					                     vrRig.handAxes[toolHand][0] * (0.10F * s) +
					                     vrRig.handAxes[toolHand][2] * (0.10F * s);
					if ((vrRig.handPos[offHand] - well).GetLength() < 0.12F * s) {
						vrRig.reloadStage = 0;
						VRPulse(-1, 0.7F, 0.08F, VRHapticShape::DoubleTap);
						return true;
					}
				}
			}
			return false;
		}

		bool Client::IsVRComfortMode(ClientCameraMode mode) {
			// Free flight is steered by your own stick, like walking; these are not.
			return mode == ClientCameraMode::ThirdPersonLocal ||
			       mode == ClientCameraMode::ThirdPersonFollow ||
			       mode == ClientCameraMode::FirstPersonFollow;
		}

		void Client::UpdateVRComfortCamera(float dt) {
			// Moving the view when nothing of yours moved it is what makes people
			// sick in VR: the eyes see motion the inner ear does not feel. The
			// flat game's death and spectator cameras do exactly that, gliding
			// after a corpse or another player. In a headset the view instead
			// stays where it is, and catches up in a cut: to black at once, the
			// jump, and back in over 0.3s. Cuts are fine; motion is not.
			const ClientCameraMode mode = vrRig.cameraMode;
			const Vector3 target = vrRig.flatCameraOrigin;
			const bool comfort = (int)vr_comfortCamera && IsVRComfortMode(mode);

			// Any change of view (dying, respawning, switching whom you follow) is a
			// cut already: blink it.
			if (mode != vrRig.lastCameraMode) {
				vrRig.lastCameraMode = mode;
				vrRig.fade = 1.0F;
				vrRig.fadingOut = false;
				vrRig.comfortAnchor = target;
				vrRig.comfortAnchored = comfort;
				vrRig.comfortAwayTime = 0.0F;
			}

			if (!comfort) {
				vrRig.comfortAnchored = false;
			} else {
				if (!vrRig.comfortAnchored) {
					vrRig.comfortAnchor = target;
					vrRig.comfortAnchored = true;
				}
				// Catch up when what you are watching is far off, or has been a
				// little off for a while.
				const float away = (target - vrRig.comfortAnchor).GetLength();
				vrRig.comfortAwayTime = away > 1.5F ? vrRig.comfortAwayTime + dt : 0.0F;
				if (!vrRig.fadingOut && vrRig.fade <= 0.0F &&
				    (away > 6.0F || vrRig.comfortAwayTime > 2.0F))
					vrRig.fadingOut = true;
			}

			if (vrRig.fadingOut) {
				vrRig.fade += dt / 0.12F;
				if (vrRig.fade >= 1.0F) {
					vrRig.fade = 1.0F;
					vrRig.fadingOut = false;
					vrRig.comfortAnchor = target;
					vrRig.comfortAwayTime = 0.0F;
				}
			} else if (vrRig.fade > 0.0F) {
				vrRig.fade = std::max(0.0F, vrRig.fade - dt / 0.3F);
			}
		}

		void Client::UpdateVRPresenceNotice() {
			if (vrPresenceCheckAt < 0.0F || time < vrPresenceCheckAt)
				return;
			vrPresenceCheckAt = -1.0F;
			if (!net)
				return;
			if (net->IsVRPresenceAvailable()) {
				SPLog("[VR] The server runs vr_presence");
				return;
			}
			// The training course runs on the game's own server, where the only
			// other players are its targets: who sees your arms does not matter
			// there, and the notice was only in the way.
			if (gui::TrainingHost::Get().IsActive()) {
				SPLog("[VR] The training course: no vr_presence notice");
				return;
			}
			chatWindow->AddMessage(ChatWindow::ColoredMessage(
			  _Tr("Client",
			      "VR: this server does not run vr_presence. Other players see you as a "
			      "normal player, without your arms or the gun in your hand, so a gun or "
			      "tool your eye cannot see along acts from your eye, as a monitor "
			      "player's does."),
			  MsgColorSysInfo));
			// And in front of you for ten seconds, as the training tips are.
			vrPresenceNoticeUntil = time + 10.0F;
			SPLog("[VR] The server does not run vr_presence: told the player");
		}

		void Client::DrawVRTutorial() {
			// Called per eye: a box 2m ahead and a little below where you look,
			// projected for this eye, so it has its own depth and is not pinned to
			// infinity. The no-vr_presence notice for its ten seconds, otherwise
			// the training course's tip for where you are; nothing elsewhere.
			std::string title, tip;
			Vector4 accent = MakeVector4(1, 0.85F, 0.3F, 1);
			if (time < vrPresenceNoticeUntil) {
				title = _Tr("Client", "No VR presence on this server");
				tip = _Tr("Client", "Other players see you as a normal player, without your arms "
				                    "or the gun in your hand. So a gun or tool your eye cannot "
				                    "see along acts from your eye, as a monitor player's does.");
				accent = MakeVector4(0.45F, 0.8F, 1.0F, 1);
			} else if (!TrainingTip(true, title, tip)) {
				return;
			}

			const SceneDefinition& d = lastSceneDef;
			const float s = vrRig.scale;
			Vector2 c, e;
			const Vector3 at = d.viewOrigin + d.viewAxis[2] * (2.0F * s) - d.viewAxis[1] * (0.45F * s);
			if (!Project(at, c) || !Project(at + d.viewAxis[0] * (0.7F * s), e))
				return;
			// 1.4m wide at 2m, whatever the eye's resolution.
			const float halfW = (e - c).GetLength();
			if (halfW < 40.0F)
				return;
			IFont& font = fontManager->GetGuiFont();
			const float k = halfW * 2.0F / 420.0F; // the text is laid out 420 wide
			const float pad = 12.0F * k;

			// Words to lines of at most 396 layout pixels.
			std::vector<std::string> lines = WrapTipText(font, tip, 396.0F);
			const float lh = font.Measure("Ag").y;
			const float boxH = (lh * (float)lines.size() + 24.0F + lh) * k;
			const AABB2 box(c.x - halfW, c.y - boxH * 0.5F, halfW * 2.0F, boxH);
			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.75F));
			renderer->DrawImage(nullptr, box);
			renderer->SetColorAlphaPremultiplied(accent);
			renderer->DrawImage(nullptr, AABB2(box.GetMinX(), box.GetMinY(), 4.0F * k, boxH));
			const std::string& header = title;
			float y = box.GetMinY() + pad;
			font.Draw(header, MakeVector2(box.GetMinX() + pad, y), k, accent);
			y += lh * k;
			for (const auto& l : lines) {
				font.Draw(l, MakeVector2(box.GetMinX() + pad, y), k, MakeVector4(1, 1, 1, 1));
				y += lh * k;
			}
		}

		void Client::DrawVRVignette() {
			// Called per eye, with Project set to that eye: the clear circle is
			// centred on where the eye looks straight ahead, which is not the
			// middle of an off-centre eye's picture.
			const float strength = Clamp((float)vr_vignette, 0.0F, 1.0F);
			const float amount = vrRig.vignette;
			if (strength <= 0.0F || amount <= 0.001F)
				return;
			// Clear inside 65% of the image's radius, black at its edge.
			constexpr float kInner = 0.65F;
			if (!vrVignetteImage) {
				const int n = 256;
				auto bmp = Handle<Bitmap>::New(n, n);
				uint32_t* px = bmp->GetPixels();
				for (int y = 0; y < n; y++)
					for (int x = 0; x < n; x++) {
						const float dx = (x + 0.5F) / n * 2.0F - 1.0F;
						const float dy = (y + 0.5F) / n * 2.0F - 1.0F;
						float t = (std::sqrt(dx * dx + dy * dy) - kInner) / (1.0F - kInner);
						t = Clamp(t, 0.0F, 1.0F);
						t = t * t * (3.0F - 2.0F * t);
						px[y * n + x] = static_cast<uint32_t>(t * 255.0F + 0.5F) << 24;
					}
				vrVignetteImage = renderer->CreateImage(*bmp);
			}
			const SceneDefinition& d = lastSceneDef;
			const float clear = VRVignetteClearAngle(amount, strength);
			Vector2 c, e;
			if (!Project(d.viewOrigin + d.viewAxis[2], c) ||
			    !Project(d.viewOrigin + d.viewAxis[2] + d.viewAxis[0] * std::tan(clear), e))
				return;
			const float r = (e - c).GetLength() / kInner;
			const float w = renderer->ScreenWidth(), h = renderer->ScreenHeight();
			// Fades in over the first quarter, so it never pops in at the edge.
			renderer->SetColorAlphaPremultiplied(
			  MakeVector4(0, 0, 0, std::min(1.0F, amount * 4.0F)));
			renderer->DrawImage(vrVignetteImage, AABB2(c.x - r, c.y - r, r * 2.0F, r * 2.0F));
			// Black beyond the image, all round.
			auto rect = [&](float x0, float y0, float x1, float y1) {
				x0 = std::max(x0, 0.0F), y0 = std::max(y0, 0.0F);
				x1 = std::min(x1, w), y1 = std::min(y1, h);
				if (x1 > x0 && y1 > y0)
					renderer->DrawImage(nullptr, AABB2(x0, y0, x1 - x0, y1 - y0));
			};
			rect(0.0F, 0.0F, w, c.y - r);
			rect(0.0F, c.y + r, w, h);
			rect(0.0F, c.y - r, c.x - r, c.y + r);
			rect(c.x + r, c.y - r, w, c.y + r);
		}

		float Client::VRHeadClearance(const Vector3& eye, const Vector3& offset) const {
			if (!world || !world->GetMap())
				return 1.0F;
			const GameMap& map = *world->GetMap();
			// The eye keeps this far (blocks) from any solid block: the near plane,
			// and a little more so a wall does not fill the view.
			const float r = 0.12F;
			const Vector3 probes[7] = {{0, 0, 0},  {r, 0, 0},  {-r, 0, 0}, {0, r, 0},
			                           {0, -r, 0}, {0, 0, r}, {0, 0, -r}};
			auto clear = [&](const Vector3& at) {
				for (const Vector3& d : probes)
					if (map.ClipBox(at.x + d.x, at.y + d.y, at.z + d.z))
						return false;
				return true;
			};
			if (clear(eye + offset))
				return 1.0F;
			// Already in something at the body's own eye (a tight crawlspace): no
			// better place to put it.
			if (!clear(eye))
				return 1.0F;
			float lo = 0.0F, hi = 1.0F;
			for (int i = 0; i < 8; i++) {
				float mid = (lo + hi) * 0.5F;
				if (clear(eye + offset * mid))
					lo = mid;
				else
					hi = mid;
			}
			return lo;
		}

		void Client::ApplyVRTwoHandedAim() {
			const int th = VRToolHand(), oh = 1 - th;
			if (!vrRig.handValid[0] || !vrRig.handValid[1])
				return;
			SetVRToolForward(VRTwoHandedForward());
		}

		void Client::UpdateVRTwoHandedGrip(Player& p, const VRFrameState& fs) {
			// A rifle held by its grip alone wobbles with the wrist; held at the
			// barrel as well, it points where the two hands say, which is steadier
			// and what every VR shooter does. The off hand's grip takes the gun
			// when that hand is on the barrel: ahead of the gun hand along its aim
			// and close to that line. Letting go, or pulling the hands apart, hands
			// it back to the one hand.
			const int th = VRToolHand(), oh = 1 - th;
			const float squeeze = fs.hands[oh].squeeze;
			const bool wasHeld = vrRig.offGripHeld;
			vrRig.offGripHeld = squeeze > (wasHeld ? 0.35F : 0.6F);
			const bool pressed = vrRig.offGripHeld && !wasHeld;
			const bool was = vrRig.twoHanded;
			const float s = vrRig.scale; // blocks per metre

			bool hold = was;
			if (!(int)vr_twoHandedGrip || !p.IsToolWeapon() || !vrRig.handValid[0] ||
			    !vrRig.handValid[1] || !vrRig.offGripHeld) {
				hold = false;
			} else {
				Vector3 d = vrRig.handPos[oh] - vrRig.handPos[th];
				if (!was && pressed) {
					const Vector3& fwd = vrRig.handAxes[th][0];
					float along = Vector3::Dot(d, fwd);
					float off = (d - fwd * along).GetLength();
					hold = along > 0.08F * s && along < 0.7F * s && off < 0.18F * s;
				}
				if (hold && d.GetLength() > 0.9F * s)
					hold = false;
			}

			vrRig.twoHanded = hold;
			if (hold)
				MarkTrainingVRStep(training::VRStepTwoHands);
			if (hold && !was) {
				ApplyVRTwoHandedAim();
				VRPulse(oh, 0.5F, 0.05F);
			} else if (!hold && was) {
				// Back to the gun hand's own aim.
				UpdateVRRigTransforms(fs, vrRig.camera);
			}
		}

		bool Client::GetLocalVRHand(int hand, Vector3& position, Vector3 axes[3]) const {
			if (!vrRig.active || hand < 0 || hand > 1 || !vrRig.handValid[hand])
				return false;
			position = vrRig.handPos[hand];
			for (int i = 0; i < 3; i++)
				axes[i] = vrRig.handAxes[hand][i];
			return true;
		}

		void Client::UpdateVR(float dt) {
			SPADES_MARK_FUNCTION();

			IVRSystem* vr = GetVRSystem();
			if (!vr || !vr->GetFrameState().running) {
				if (vrRig.active) {
					// Back on the monitor: let go of everything the controllers held.
					VRReleaseAllKeys();
					if (world) {
						auto p = world->GetLocalPlayer();
						if (p)
							p->ClearVRToolRay();
					}
					vrRig.active = false;
				}
				return;
			}

			const VRFrameState& fs = vr->GetFrameState();
			vrRig.active = true;
			vr->SetHeldMap((int)vr_heldMap && (int)vr_wristHud && largeMapView &&
			               largeMapView->IsZoomed());
			if (dt > 0.0F)
				vrRig.fps = vrRig.fps <= 0.0F ? 1.0F / dt : Mix(vrRig.fps, 1.0F / dt, 0.05F);
			UpdateVRComfortCamera(dt);
			UpdateVRPresenceNotice();
			vrRig.scopeActive = false; // asked for again while the scene is built
			// No sneak in a headset: the keyboard's V is not read (see
			// Client::KeyEvent), and the off hand's Y is the map now.
			playerInput.sneak = false;
			vrRig.scale = Clamp((float)vr_worldScale, 0.5F, 4.0F);
			vrRig.poseSendTimer -= dt;
			if (!fs.head.valid)
				return;

			const int toolHand = VRToolHand();
			const int offHand = 1 - toolHand;
			const VRControllerState& T = fs.hands[toolHand];
			const VRControllerState& O = fs.hands[offHand];

			// Both sticks held in for a second recentre, as does the runtime's own
			// recentre and the app first getting focus. A second, because both
			// sticks go in together easily by accident (they are crouch and
			// sprint), and a recentre taken bent over puts the floor in the wrong
			// place: the body then hangs below the head.
			bool bothClicks = fs.hands[0].thumbstickClick && fs.hands[1].thumbstickClick;
			bool heldRecenter = false;
			if (bothClicks) {
				vrRig.stickChord = true;
				vrRig.recenterHold += dt;
				if (vrRig.recenterHold >= 1.0F && !vrRig.recenterHoldFired) {
					vrRig.recenterHoldFired = true;
					heldRecenter = true;
				}
			} else {
				vrRig.recenterHold = 0.0F;
				vrRig.recenterHoldFired = false;
			}
			if (heldRecenter || vrRig.heightResetRequested) {
				vrRig.heightResetRequested = false;
				RecenterVR(fs, true);
				VRPulse(-1, 0.6F, 0.12F);
			} else if (vr->ConsumeRecenterRequest() || (!vrRig.calibrated && fs.focused)) {
				RecenterVR(fs, false);
			}

			// Typing (chat, on the pointed-at keyboard) is a menu too: the
			// controllers are typing, not playing.
			const bool menu = NeedsAbsoluteMouseCoordinate() || AcceptsTextInput();
			// Playing, the front panel follows the head: its overlays are drawn
			// through it onto the world. A menu stands in the room instead.
			if (!menu)
				vr->SetHudFollowsHead();

			// Turning, on the tool hand's stick.
			vrRig.turnRate = 0.0F;
			if (!menu) {
				float x = T.thumbstick.x;
				float snap = (float)vr_snapTurn;
				if (snap > 0.0F) {
					if (!vrRig.snapLatched && std::fabs(x) > 0.7F) {
						vrRig.bodyYaw += std::copysign(DEG2RAD(snap), x);
						vrRig.snapLatched = true;
					} else if (std::fabs(x) < 0.3F) {
						vrRig.snapLatched = false;
					}
				} else if (std::fabs(x) > 0.15F) {
					vrRig.turnRate = x * DEG2RAD((float)vr_smoothTurnSpeed);
					vrRig.bodyYaw += vrRig.turnRate * dt;
				}
			}

			stmp::optional<Player&> maybePlayer;
			if (world)
				maybePlayer = world->GetLocalPlayer();
			ClientCameraMode mode = GetCameraMode();
			bool embodied = mode == ClientCameraMode::FirstPersonLocal && maybePlayer &&
			                maybePlayer->IsAlive();

			// The comfort vignette: the body's own artificial motion. Velocity is
			// blocks per 1/32s (Player::MovePlayer); +Z is down.
			{
				float target = 0.0F;
				if (embodied && (float)vr_vignette > 0.0F) {
					const Vector3 v = maybePlayer->GetVelocity() * 32.0F;
					target = VRVignetteTarget(std::sqrt(v.x * v.x + v.y * v.y),
					                          std::max(0.0F, v.z), vrRig.turnRate);
				}
				vrRig.vignette = VRVignetteStep(vrRig.vignette, target, dt);
			}

			if (embodied) {
				Player& p = maybePlayer.value();
				const float s = vrRig.scale;

				// Room-scale: the head's offset from where the body stands is walked
				// into the body, as far as the walls allow.
				Vector3 offset = MakeVector3(fs.head.position.x - vrRig.anchorX, 0.0F,
				                             fs.head.position.z - vrRig.anchorZ);
				// Not in aim training, which is shot from where you stand (a lean
				// still moves the view, not the body).
				if ((int)vr_roomScale && !menu && !gui::TrainingHost::Get().IsAim()) {
					Vector3 moved = p.VRPushBody(VRToWorld(offset));
					// Back into tracking space: undo the yaw, the axes and the scale.
					float c = std::cos(-vrRig.bodyYaw), sn = std::sin(-vrRig.bodyYaw);
					Vector3 g = MakeVector3(moved.x * c - moved.y * sn, moved.x * sn + moved.y * c,
					                        moved.z) /
					            s;
					vrRig.anchorX += g.y;  // game +y is tracking +X
					vrRig.anchorZ += -g.x; // game +x is tracking -Z
				}
				// Whatever is left is the head leaning past the body: where a wall
				// stopped the body. It is not kept. The view is where the body's eye
				// is, so a headset player sees nothing their hitbox is not out in:
				// no peeking round a corner the body is hidden behind, which no
				// monitor player can do (fair play, see VR.md). Against a wall the
				// view stops with the body.
				float rx = fs.head.position.x - vrRig.anchorX;
				float rz = fs.head.position.z - vrRig.anchorZ;
				float len = std::sqrt(rx * rx + rz * rz);
				const float maxLean = 0.0F;
				if (len > maxLean) {
					float k = 1.0F - maxLean / len;
					vrRig.anchorX += rx * k;
					vrRig.anchorZ += rz * k;
					rx *= maxLean / len;
					rz *= maxLean / len;
				}
				vrRig.residual = VRToWorld(MakeVector3(rx, 0.0F, rz));

				// Crouching for real crouches the character, and the eye is then
				// measured from a crouched height so the view does not drop twice.
				float depth = vrRig.standingHeight - fs.head.position.y;
				float crouchAt = std::max(0.1F, (float)vr_crouchAt);
				if ((int)vr_seated)
					vrRig.physicalCrouch = false;
				else if (!vrRig.physicalCrouch && depth > crouchAt)
					vrRig.physicalCrouch = true;
				else if (vrRig.physicalCrouch && depth < crouchAt - 0.1F)
					vrRig.physicalCrouch = false;
				float reference =
				  vrRig.standingHeight - (p.GetInput().crouch ? 0.9F / s : 0.0F);
				// Never above the game's eye: a head higher than the body's eye would
				// see over cover the hitbox is behind. Lower is fine (and crouches).
				vrRig.lift = Clamp(fs.head.position.y - reference, -0.5F, 0.0F) * s;

				// The head does not go into walls either. The body already stops at
				// them, but the head leans past it by up to 30cm, and a head in a
				// block sees through the map. Its offset from the body's eye stops
				// short of the first block instead, as a head against a wall does.
				{
					Vector3 want = vrRig.residual + MakeVector3(0.0F, 0.0F, -vrRig.lift);
					float t = VRHeadClearance(p.GetEye(), want);
					if (t < 1.0F) {
						vrRig.residual = vrRig.residual * t;
						vrRig.lift *= t;
					}
				}

				Vector3 camera = p.GetEye() + vrRig.residual + MakeVector3(0.0F, 0.0F, -vrRig.lift);
				vrRig.solidHands = true;
				vrRig.toolReach = VRToolReach(p);
				const bool wasBlocked = vrRig.toolBlocked;
				UpdateVRRigTransforms(fs, camera);
				if (menu) {
					if (vrRig.twoHanded) {
						vrRig.twoHanded = false;
						UpdateVRRigTransforms(fs, camera);
					}
				} else {
					UpdateVRTwoHandedGrip(p, fs);
					UpdateVRAimAssist(p, dt);
					ApplyVRToolCollision(); // again, for the aim just settled
				}
				// A light knock as the tool meets a wall.
				if (vrRig.toolBlocked && !wasBlocked)
					VRPulse(toolHand, 0.3F, 0.03F);
				UpdateVRWrist(fs, dt, menu);

				if (menu) {
					VRReleaseAllKeys();
				} else {
					UpdateVRPlayerInput(p, fs, dt);
				}

				// The facing the server sees: the head, or the tool hand while a
				// tool is in use (see the top of this file).
				bool usingTool = T.trigger > 0.25F || p.IsCookingGrenade() ||
				                 (T.squeeze > 0.7F && !p.IsToolWeapon());
				if (usingTool)
					vrRig.lastToolUseTime = time;
				// Without vr_presence, others see only this facing, and the head
				// looking about while the gun pointed elsewhere looked like a
				// cheat's snapping aim (a tester's report): there, while a gun is
				// held, it is always where the gun points. (Not the spade or
				// blocks: held low, they would slow walking and stop climbing.)
				// Walking, though, it is the head's, unless the gun is in use: the
				// server walks relative to the facing, and from the gun's, walking
				// where the head looks took a zigzag (UpdateVRPlayerInput).
				const bool presence = net && net->IsVRPresenceAvailable();
				const Vector2 walkStick = fs.hands[1 - toolHand].thumbstick;
				const bool walking = walkStick.x * walkStick.x + walkStick.y * walkStick.y >
				                     0.25F * 0.25F;
				bool aimFacing = vrRig.handValid[toolHand] &&
				                 ((!presence && p.IsToolWeapon() && !walking) ||
				                  time - vrRig.lastToolUseTime < 0.4F);
				Vector3 facing = aimFacing ? vrRig.handAxes[toolHand][0] : vrRig.headAxes[0];

				vrRig.gunLaser = false;
				if (vrRig.handValid[toolHand]) {
					Vector3 toolOffset = vrRig.aimPos[toolHand] - p.GetEye();
					Vector3 toolDir = vrRig.handAxes[toolHand][0];
					if (p.IsToolWeapon()) {
						// A gun fires from the eye, along the gun (VRGunShot), so
						// whatever stands in the eye's way takes the shot. Fired from
						// the hand, a gun held
						// out past a corner hit what the eye could not see, with the
						// body behind the wall; the eye is where a monitor player's
						// shot starts. Through the scope, the ray is the scope's, so the
						// shot lands on the reticle and not 0.23 blocks under it, where
						// the barrel is. See VR.md, "Fair play".
						Vector3 impact;
						bool hit = false;
						toolDir = VRGunShot(p, impact, hit);
						toolOffset = MakeVector3(0.0F, 0.0F, 0.0F);
						if (aimFacing)
							facing = toolDir;
						// The laser itself is worked out again where it is drawn
						// (ClientPlayer), from the hand the gun is drawn in.
						vrRig.gunLaser = true;
					} else {
						// Digs and blocks leave the tool in the hand. A hand can be out
						// round a corner the eye cannot see past, so unless the server
						// runs vr_presence (where everyone sees the VR player's arms),
						// a tool the eye has no line of sight to acts from the eye,
						// along the tool's direction, as a monitor player's does.
						// (Past a wall the far end can be clear: the line to it is not.)
						if (!presence && (VRHeadClearance(p.GetEye(), toolOffset) < 1.0F ||
						                  !VRLineOfSight(p.GetEye(), p.GetEye() + toolOffset)))
							toolOffset = MakeVector3(0.0F, 0.0F, 0.0F);
					}
					p.SetVRToolRay(toolOffset, toolDir);
				}
				else
					p.ClearVRToolRay();

				// Straight up or down has no heading, and walking is relative to
				// the heading: keep a sliver of one.
				if (facing.GetLength2D() < 0.02F)
					facing += MakeVector3(std::cos(vrRig.bodyYaw), std::sin(vrRig.bodyYaw), 0.0F) *
					          0.05F;
				// Not while a menu is up, the game's (the menu button) or the
				// headset's own (the game out of focus): the body turned to wherever
				// you looked in it (testers).
				if (!menu && fs.focused)
					p.SetOrientation(facing.Normalize());
			} else {
				if (maybePlayer)
					maybePlayer->ClearVRToolRay();
				vrRig.solidHands = false;
				vrRig.toolBlocked = false;
				vrRig.twoHanded = false;
				vrRig.shouldered = false;
				vrRig.stock = VRStockState();
				vrRig.aimFilter.Reset();
				vrRig.recoil.Reset();
				vrRig.recoilOn = false;
				vrRig.residual = MakeVector3(0, 0, 0);
				vrRig.lift = 0.0F;
				UpdateVRRigTransforms(fs, lastSceneDef.viewOrigin);
				UpdateVRWrist(fs, dt, menu);

				if (menu) {
					VRReleaseAllKeys();
				} else {
					// Dead or spectating: the triggers cycle whom to follow and A
					// switches the follow camera, as the mouse and space bar do.
					bool fire = TriggerState(T.trigger, vrRig.triggerHeld[toolHand]);
					vrRig.triggerHeld[toolHand] = fire;
					VRSetKey(cg_keyAttack,
					         fire && !vrRig.wristPointing && !vrRig.wristTriggerOwned);
					VRSetKey(cg_keyAltAttack, T.squeeze > 0.7F);
					VRSetKey(cg_keyJump, T.buttonPrimary);
					VRSetKey(cg_keyReloadWeapon, T.buttonSecondary);
					VRSetKey(cg_keyScoreboard, O.squeeze > 0.7F);

					if (mode == ClientCameraMode::Free) {
						// The free camera flies where the head looks.
						Vector3 f = vrRig.headAxes[0];
						followAndFreeCameraState.yaw = std::atan2(-f.y, -f.x);
						followAndFreeCameraState.pitch = std::asin(Clamp(f.z, -1.0F, 1.0F));
						Vector2 st = O.thumbstick;
						VRSetKey(cg_keyMoveForward, st.y > 0.4F);
						VRSetKey(cg_keyMoveBackward, st.y < -0.4F);
						VRSetKey(cg_keyMoveRight, st.x > 0.4F);
						VRSetKey(cg_keyMoveLeft, st.x < -0.4F);
						VRSetKey(cg_keySprint, O.thumbstickClick);
					}
				}
			}

			for (int h = 0; h < 2; h++) {
				vrRig.prevPrimary[h] = fs.hands[h].buttonPrimary;
				vrRig.prevSecondary[h] = fs.hands[h].buttonSecondary;
				vrRig.prevStickClick[h] = fs.hands[h].thumbstickClick;
			}
			if (!fs.hands[0].thumbstickClick && !fs.hands[1].thumbstickClick)
				vrRig.stickChord = false;
		}

		bool Client::VRToolHasReach(Player& p) {
			// As the laser's green dot: the block the tool's ray meets is in reach
			// by the game's measure for the tool, from the eye.
			GameMap* m = map.GetPointerOrNull();
			if (!m)
				return false;
			GameMap::RayCastResult r = m->CastRay2(p.GetToolOrigin(), p.GetToolDirection(), 32);
			if (!r.hit || r.startSolid)
				return false;
			if (p.IsToolBlock())
				return p.InBuildRange(r.hitBlock + r.normal);
			return (MakeVector3(r.hitBlock) + 0.5F - p.GetEye()).GetChebyshevLength() <
			       static_cast<float>(MAX_DIG_DISTANCE);
		}

		bool Client::VRLineOfSight(const Vector3& from, const Vector3& to) const {
			if (!world || !world->GetMap())
				return true;
			Vector3 d = to - from;
			const float len = d.GetLength();
			if (len < 1.0E-4F)
				return true;
			GameMap::RayCastResult r = world->GetMap()->CastRay2(from, d / len, (int)len + 3);
			// Already in a block at `from`: no better answer (as VRHeadClearance).
			if (!r.hit || r.startSolid)
				return true;
			return (r.hitPos - from).GetLength() >= len;
		}

		Vector3 Client::VRGunShot(Player& p, Vector3& impact, bool& hit) {
			const int toolHand = VRToolHand();
			const Vector3 eye = p.GetEye();
			// The ray that finds what the gun aims at leaves the eye, along the
			// gun: the shot leaves the eye too, so they are one line. From the
			// gun (and later 0.2m over it), it cleared a ledge or a corner the
			// eye's line met, and the dot jumped back onto the cover; and held at
			// the chest, a window or low cover in front of the feet stopped it
			// where the eye saw over (testers' reports). Up close the dot is the
			// eye-to-hand offset from the barrel's own line; it is always where
			// the shot lands. Through the scope, the scope's line.
			Vector3 from = eye;
			Vector3 dir = vrRig.handAxes[toolHand][0];
			if (vrRig.scopeAimValid) {
				from = vrRig.scopeAimOrigin;
				dir = vrRig.scopeAimDir;
			}
			// A gun pushed through a wall (tool collision is off) shoots the wall.
			// Its ray, from beyond, found whoever stood behind it, and the shot
			// and the laser swung to them: a tester's report, an ESP. Measured to
			// the muzzle, as the laser is: to the hand, a rifle held over a wall
			// two blocks high, its grip inside the top block and its barrel clear,
			// shot the wall (testers' report).
			Vector3 muzzle = vrRig.aimPos[toolHand];
			if (p.GetId() < clientPlayers.size() && clientPlayers[p.GetId()])
				muzzle = clientPlayers[p.GetId()]->VRGunLaserOrigin(vrRig.handPos[toolHand],
				                                                     vrRig.handAxes[toolHand]);
			if (GameMap* m = map.GetPointerOrNull()) {
				Vector3 toGun = muzzle - eye;
				const float len = toGun.GetLength();
				if (len > 1.0E-3F) {
					toGun = toGun / len;
					GameMap::RayCastResult r = m->CastRay2(eye, toGun, (int)len + 3);
					if (r.hit && !r.startSolid && (r.hitPos - eye).GetLength() < len) {
						impact = r.hitPos;
						hit = true;
						return toGun;
					}
				}
			}
			const Vector3 target = VRGunAimPoint(p, from, dir, hit);
			Vector3 shot = target - eye;
			const float toTarget = shot.GetLength();
			if (toTarget > 0.05F)
				shot = shot / toTarget;
			else
				shot = dir;
			// Short of the target when the eye cannot see it: what is in between.
			impact = target;
			if (GameMap* m = map.GetPointerOrNull()) {
				GameMap::RayCastResult r = m->CastRay2(eye, shot, 256);
				if (r.hit && !r.startSolid && (r.hitPos - eye).GetLength() < toTarget - 0.05F) {
					impact = r.hitPos;
					hit = true;
				}
			}
			return shot;
		}

		Vector3 Client::VRGunAimPoint(Player& p, const Vector3& from, const Vector3& dir,
		                              bool& hit) {
			// The first block or player hitbox on the ray, or 128 blocks out (the
			// fog) if none. Players count, or a shot at someone close, from an eye
			// an arm's length off the ray, would pass beside them.
			float best = 128.0F;
			hit = false;
			if (GameMap* m = map.GetPointerOrNull()) {
				GameMap::RayCastResult r = m->CastRay2(from, dir, 256);
				if (r.hit && !r.startSolid) {
					const float d = (r.hitPos - from).GetLength();
					if (d < best) {
						best = d;
						hit = true;
					}
				}
			}
			if (world) {
				for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
					auto other = world->GetPlayer(static_cast<unsigned int>(i));
					if (!other || &other.value() == &p || !other->IsAlive() || other->IsSpectator())
						continue;
					if (!other->RayCastApprox(from, dir))
						continue;
					Player::HitBoxes hb = other->GetHitBoxes(true);
					OBB3* boxes[5] = {&hb.head, &hb.torso, &hb.limbs[0], &hb.limbs[1],
					                        &hb.limbs[2]};
					for (OBB3* box : boxes) {
						Vector3 at;
						if (box->RayCast(from, dir, &at)) {
							const float d = (at - from).GetLength();
							if (d < best) {
								best = d;
								hit = true;
							}
						}
					}
				}
			}
			return from + dir * best;
		}

		void Client::UpdateVRPlayerInput(Player& p, const VRFrameState& fs, float dt) {
			const int toolHand = VRToolHand();
			const int offHand = 1 - toolHand;
			const VRControllerState& T = fs.hands[toolHand];
			const VRControllerState& O = fs.hands[offHand];

			// Walking, on the off hand's stick, relative to where the head faces.
			// The server only knows eight directions relative to the facing it was
			// sent. While walking that facing is the head's (UpdateVR), so the
			// stick is rounded to the nearest of the eight, as keys are, and
			// straight ahead is straight ahead. While a gun is in use the facing is
			// the gun's: rounded, the walk went up to 22.5 degrees off where the
			// head looked (off a tower's ledge, a tester's report), so there it
			// alternates between the two directions either side, held 1/20s at a
			// time, in the mix that averages to the wanted one (VRWalkDither).
			// That alternation, used for all walking until 0.3, zigzagged the view
			// (testers' report).
			Vector2 stick = O.thumbstick;
			float mag = std::sqrt(stick.x * stick.x + stick.y * stick.y);
			bool fwd = false, back = false, left = false, right = false;
			// A stick click counts when it is let go, and not at all if the other
			// stick went in meanwhile: that is the recentre chord.
			if ((int)vr_sprintHold)
				vrRig.sprintToggle = O.thumbstickClick && !vrRig.stickChord;
			else if (!O.thumbstickClick && vrRig.prevStickClick[offHand] && !vrRig.stickChord)
				vrRig.sprintToggle = !vrRig.sprintToggle;
			if (mag > 0.25F) {
				Vector3 hf = vrRig.headAxes[0];
				hf.z = 0.0F;
				Vector3 hr = vrRig.headAxes[1];
				hr.z = 0.0F;
				if (hf.GetLength() > 0.01F && hr.GetLength() > 0.01F) {
					Vector3 want = (hf.Normalize() * stick.y + hr.Normalize() * stick.x).Normalize();
					Vector3 f2 = p.GetFront2D();
					Vector3 r2 = p.GetRight();
					float a = Vector3::Dot(want, f2), b = Vector3::Dot(want, r2);
					const float angle = std::atan2(b, a);
					int dir;
					if (Vector3::Dot(f2, hf.Normalize()) > 0.999F) { // facing the head's
						dir = static_cast<int>(std::lround(angle / kQuarterPi));
						dir = (dir % 8 + 8) % 8;
						vrRig.walkLean = 0.0F;
						vrRig.walkHold = 0.0F;
					} else {
						dir = VRWalkDither(angle, dt, vrRig.walkLean, vrRig.walkHold,
						                   vrRig.walkSecond);
					}
					// 0 forward, then clockwise (to the right) in eighths.
					fwd = dir == 7 || dir == 0 || dir == 1;
					right = dir >= 1 && dir <= 3;
					back = dir >= 3 && dir <= 5;
					left = dir >= 5 && dir <= 7;
				}
			} else {
				if (!(int)vr_sprintHold)
					vrRig.sprintToggle = false;
				vrRig.walkLean = 0.0F;
				vrRig.walkHold = 0.0F;
			}
			VRSetKey(cg_keyMoveForward, fwd);
			VRSetKey(cg_keyMoveBackward, back);
			VRSetKey(cg_keyMoveLeft, left);
			VRSetKey(cg_keyMoveRight, right);
			VRSetKey(cg_keySprint, vrRig.sprintToggle && mag > 0.25F);

			// Tool hand: trigger uses the tool, grip is its second action (dig,
			// build a line). A gun has no second action in VR: aiming is holding
			// it up to the eye.
			bool fire = TriggerState(T.trigger, vrRig.triggerHeld[toolHand]);
			vrRig.triggerHeld[toolHand] = fire;
			// Not while the trigger is working the wrist menu.
			if (vrRig.wristPointing || vrRig.wristTriggerOwned)
				fire = false;

			// Swinging the spade hits, as the trigger does. One swing is one hit:
			// latched until the hand slows to under half the speed. The press is
			// held a moment, so a swing that comes as the spade is still recovering
			// from the last one lands as soon as it is ready.
			{
				const float swingAt = (float)vr_swingSpeed;
				vrRig.swingHold = std::max(0.0F, vrRig.swingHold - dt);
				if (p.IsToolSpade() && swingAt > 0.0F && vrRig.handValid[toolHand]) {
					float speed = (vrRig.handVelocity[toolHand] / vrRig.scale).GetLength();
					if (!vrRig.swingLatched && speed > swingAt) {
						vrRig.swingLatched = true;
						vrRig.swingHold = 0.15F;
					} else if (speed < swingAt * 0.5F) {
						vrRig.swingLatched = false;
					}
				} else {
					vrRig.swingLatched = false;
					vrRig.swingHold = 0.0F;
				}
			}

			// A grenade leaves with the hand's speed at release. Keep the fastest
			// recent sample: the hand is already slowing when the trigger opens.
			if (p.IsToolGrenade() && p.IsCookingGrenade() && vrRig.handValid[toolHand]) {
				Vector3 v = vrRig.handVelocity[toolHand] / vrRig.scale; // m/s
				float speed = v.GetLength();
				float full = std::max(1.0F, (float)vr_throwSpeed);
				float power = Clamp(speed / full, 0.15F, 1.0F);
				Vector3 dir = speed > 1.0F ? vrRig.handVelocity[toolHand].Normalize()
				                           : vrRig.handAxes[toolHand][0];
				vrRig.throwPeakAge += dt;
				if (power >= vrRig.throwPeakPower || vrRig.throwPeakAge > 0.12F) {
					vrRig.throwPeakPower = power;
					vrRig.throwPeakDir = dir;
					vrRig.throwPeakAge = 0.0F;
				}
				p.SetVRThrowVelocity(vrRig.throwPeakDir * vrRig.throwPeakPower);
			} else {
				vrRig.throwPeakPower = 0.0F;
			}
			VRSetKey(cg_keyAttack, fire || vrRig.swingHold > 0.0F);

			// Holsters: the gun hand's grip at a slot on the body takes that tool.
			// A light buzz says the hand is at one. The grip that took a tool is not
			// also the tool's second action (a dig, a line of blocks) until let go.
			//
			// A holster only counts for a grip that begins there. Testers found
			// the grip taking blocks when they meant to dig or build: a hand hanging
			// loose at the hip while a wall was drawn crossed the blocks' holster
			// and the line stopped, and the spade pointed at the ground beside the
			// feet put the hand in it. So a grip that began as the tool's action
			// stays that until let go, wherever the hand goes; no holster counts
			// while the gun is held in both hands, nor the one the held tool came
			// from, nor any while the spade or blocks have a block in reach.
			static const Player::ToolType holstered[4] = {
			  Player::ToolWeapon, Player::ToolSpade, Player::ToolBlock, Player::ToolGrenade};
			int slot = VRHolsterNone;
			// Only with the tool pointed down, at the ground by the feet where
			// the hand is at the hip: in a tunnel a block is always in reach, and
			// no holster ever counted there (a tester's report).
			const bool reach = (p.IsToolSpade() || p.IsToolBlock()) &&
			                   p.GetToolDirection().z > 0.5F && VRToolHasReach(p);
			if ((int)vr_holsters && VRGripMayHolster(vrRig.grip, vrRig.twoHanded, reach)) {
				slot = VRHolsterAt(fs, toolHand, vrRig.holsterHover);
				if (slot >= 0 && p.GetTool() == holstered[slot])
					slot = VRHolsterNone;
			}
			if (slot != vrRig.holsterHover) {
				if (slot >= 0)
					VRPulse(toolHand, 0.25F, 0.02F);
				vrRig.holsterHover = slot;
			}
			const int take = VRGripUpdate(vrRig.grip, T.squeeze > 0.7F, slot);
			if (take >= 0 && p.IsToolSelectable(holstered[take])) {
				SetSelectedTool(holstered[take]);
				MarkTrainingVRStep(training::VRStepHolster);
				VRPulse(toolHand, 0.6F, 0.06F);
			}
			VRSetKey(cg_keyAltAttack, vrRig.grip.action && !p.IsToolWeapon());

			VRSetKey(cg_keyJump, T.buttonPrimary);
			VRSetKey(cg_keyReloadWeapon, UpdateVRReload(p, fs));
			// A reload that finishes says so in the gun hand, as the laser turns
			// back from amber (ClientPlayer): the sound alone was not enough.
			{
				const bool reloading = p.IsToolWeapon() && p.GetWeapon().IsReloading();
				if (vrRig.wasReloading && !reloading && p.IsToolWeapon())
					MarkTrainingVRStep(training::VRStepReload);
				if (vrRig.wasReloading && !reloading && p.IsToolWeapon())
					VRPulse(toolHand, 0.5F, 0.06F);
				vrRig.wasReloading = reloading;
			}
			if (!T.thumbstickClick && vrRig.prevStickClick[toolHand] && !vrRig.stickChord)
				vrRig.crouchToggle = !vrRig.crouchToggle;
			VRSetKey(cg_keyCrouch, vrRig.crouchToggle || vrRig.physicalCrouch);

			// The palette is on the wrist menu. The turn stick's up/down walked it
			// too, and a stick pushed by accident changed the colour (testers).
			VRSetKey(cg_keyPaletteRight, false);
			VRSetKey(cg_keyPaletteLeft, false);

			// Off hand: X/Y (or the second hand's A/B) step through the tools with
			// vr_toolButtons. Without it (the default: the holsters take the tools)
			// X grabs a block's colour and Y opens and closes the big map (it was
			// sneak, which testers took for nothing: walking at half speed).
			// Not on a grip: it would be pressed by every hold.
			const bool toolButtons = (int)vr_toolButtons != 0;
			auto toggleMap = [&]() {
				std::string key = KeyEventName(cg_keyToggleMapZoom);
				KeyEvent(key, true);
				KeyEvent(key, false);
			};
			if (!toolButtons && p.IsToolBlock() && O.buttonPrimary && !vrRig.prevPrimary[offHand])
				CaptureColor();
			// Not with B: B and Y together are the menu
			if (!toolButtons && O.buttonSecondary && !vrRig.prevSecondary[offHand] &&
			    !T.buttonSecondary)
				toggleMap();
			auto cycleTool = [&](int step) {
				Player::ToolType t = p.GetTool();
				for (int i = 0; i < 4; i++) {
					int n = (static_cast<int>(t) + step + 4) % 4;
					t = static_cast<Player::ToolType>(n);
					if (p.IsToolSelectable(t))
						break;
				}
				SetSelectedTool(t);
				VRPulse(offHand, 0.3F, 0.03F);
			};
			if (toolButtons && O.buttonPrimary && !vrRig.prevPrimary[offHand])
				cycleTool(-1);
			if (toolButtons && O.buttonSecondary && !vrRig.prevSecondary[offHand] &&
			    !T.buttonSecondary)
				cycleTool(+1);

			// The headlamp's switch is at the head, where a real one is: the off
			// hand brought up beside the headset and squeezed. While it is there,
			// its grip is the switch and not the scores.
			{
				const bool squeezed = O.squeeze > 0.7F;
				bool atHead = false;
				if (O.grip.valid && fs.head.valid)
					atHead = (O.grip.position - fs.head.position).GetLength() < 0.18F;
				if (squeezed && !vrRig.prevLampSqueeze && atHead && !vrRig.twoHanded &&
				    vrRig.reloadStage == 0) {
					ToggleLocalFlashlight();
					VRPulse(offHand, 0.4F, 0.04F);
				}
				vrRig.prevLampSqueeze = squeezed;
				// The hand counts as at the head until the grip lets go.
				vrRig.lampHandAtHead = squeezed ? (vrRig.lampHandAtHead || atHead) : false;
			}

			// Off hand grip shows the scores (unless it is holding the gun or a
			// magazine, or switching the headlamp). Its trigger, held while it holds
			// the gun's barrel, braces the gun on the shoulder (UpdateVRAimAssist);
			// otherwise it opens and closes the big map, as Y does (Vive and WMR
			// controllers have no Y). (It used to pick a
			// block's colour while blocks were held, so a map opened before taking
			// blocks could not be closed: a tester could not find the way out of
			// it. The colour is X's now.)
			VRSetKey(cg_keyScoreboard, O.squeeze > 0.7F && !vrRig.twoHanded &&
			                             vrRig.reloadStage == 0 && !vrRig.lampHandAtHead);
			bool offTrigger = TriggerState(O.trigger, vrRig.triggerHeld[offHand]);
			const bool onBarrel = p.IsToolWeapon() && vrRig.twoHanded;
			vrRig.braceHeld = offTrigger && onBarrel;
			if (offTrigger && !vrRig.triggerHeld[offHand] && !onBarrel) {
				if (toolButtons && p.IsToolBlock() && !(largeMapView && largeMapView->IsZoomed()))
					CaptureColor(); // X steps tools here: the trigger keeps the colour
				else
					toggleMap();
			}
			vrRig.triggerHeld[offHand] = offTrigger;
		}

		void Client::ApplyVRToSceneDefinition(SceneDefinition& def, ClientCameraMode mode) {
			if (!vrRig.active)
				return;
			IVRSystem* vr = GetVRSystem();
			if (!vr)
				return;
			const VRFrameState& fs = vr->GetFrameState();
			if (!fs.head.valid)
				return;

			Vector3 origin = def.viewOrigin;
			vrRig.flatCameraOrigin = origin;
			vrRig.cameraMode = mode;
			if (mode == ClientCameraMode::FirstPersonLocal && world) {
				auto p = world->GetLocalPlayer();
				if (p && p->IsAlive())
					origin = p->GetEye() + vrRig.residual + MakeVector3(0.0F, 0.0F, -vrRig.lift);
			} else if ((int)vr_comfortCamera && IsVRComfortMode(mode) && vrRig.comfortAnchored) {
				origin = vrRig.comfortAnchor;
			}
			UpdateVRRigTransforms(fs, origin);

			// The single view is the head. Its field of view is the HUD panel's, so
			// anything the 2D layer pins to a point in the world (names, pings,
			// markers) lands on that point as seen through the panel.
			def.viewOrigin = origin;
			def.viewAxis[0] = vrRig.headAxes[1];
			def.viewAxis[1] = -vrRig.headAxes[2];
			def.viewAxis[2] = vrRig.headAxes[0];
			float aspect = renderer->ScreenHeight() / std::max(1.0F, renderer->ScreenWidth());
			def.fovX = 2.0F * std::atan(fs.hudWidth * 0.5F / fs.hudDistance);
			def.fovY = 2.0F * std::atan(fs.hudWidth * aspect * 0.5F / fs.hudDistance);

			for (int i = 0; i < 2; i++) {
				auto& e = def.vrEyes[i];
				const VRPose& eye = fs.eyes[i];
				e.origin = origin + VRToWorld(eye.position - fs.head.position);
				e.axis[0] = VRToWorld(eye.Axis(0)).Normalize();
				e.axis[1] = VRToWorld(eye.Axis(1)).Normalize();
				e.axis[2] = VRToWorld(-eye.Axis(2)).Normalize();
				e.fovX = 2.0F * std::atan(fs.eyeTanX[i]);
				e.fovY = 2.0F * std::atan(fs.eyeTanY[i]);
				e.asymmetric = fs.eyeAsymmetric;
				e.tangents = FovTangents{fs.eyeLeft[i], fs.eyeRight[i], fs.eyeDown[i], fs.eyeUp[i]};
			}
			def.numVREyes = 2;

			// Nothing that moves the picture without the head moving: that is
			// what makes people sick. Shake, roll and zoom are already gone with
			// the view axes and field of view above.
			def.denyCameraBlur = true;
			def.radialBlur = 0.0F;
			def.blurVignette = 0.0F;
			def.depthOfFieldFocalLength = 0.0F;
			def.globalBlur = 0.0F;

			if (mode == ClientCameraMode::FirstPersonLocal && vrRig.poseSendTimer <= 0.0F) {
				vrRig.poseSendTimer = vrPoseInterval;
				SendVRPose();
			}
		}

		void Client::SendVRPose() {
			if (!net || !net->IsVRPresenceAvailable() || !world)
				return;
			auto p = world->GetLocalPlayer();
			if (!p || !p->IsAlive())
				return;
			IVRSystem* vr = GetVRSystem();
			if (!vr)
				return;
			const VRFrameState& fs = vr->GetFrameState();

			Vector3 eye = p->GetEye();
			VRNetPose pose;
			pose.headOffset = vrRig.camera - eye;
			pose.headRotation =
			  VRQuat::FromAxes(vrRig.headAxes[0], vrRig.headAxes[1], vrRig.headAxes[2]);
			for (int h = 0; h < 2; h++) {
				if (!vrRig.handValid[h])
					continue;
				pose.flags |= h == VRHandLeft ? VRPoseLeftHand : VRPoseRightHand;
				pose.handOffset[h] = vrRig.handPos[h] - eye;
				pose.handRotation[h] = VRQuat::FromAxes(vrRig.handAxes[h][0], vrRig.handAxes[h][1],
				                                        vrRig.handAxes[h][2]);
			}
			if (VRToolHand() == VRHandLeft)
				pose.flags |= VRPoseToolInLeftHand;
			// The body, so a server that allows it can follow room-scale walking
			// faster than PositionData's once a second.
			pose.flags |= VRPoseBody;
			pose.body = p->GetPosition();
			if (fs.hands[VRHandRight].trigger > 0.5F)
				pose.buttons |= VRButtonRightTrigger;
			if (fs.hands[VRHandLeft].trigger > 0.5F)
				pose.buttons |= VRButtonLeftTrigger;
			if (fs.hands[VRHandRight].squeeze > 0.5F)
				pose.buttons |= VRButtonRightGrip;
			if (fs.hands[VRHandLeft].squeeze > 0.5F)
				pose.buttons |= VRButtonLeftGrip;
			net->SendVRPose(pose);
		}

		void Client::VRPulse(int hand, float amplitude, float seconds, VRHapticShape shape) {
			IVRSystem* vr = GetVRSystem();
			if (!vrRig.active || !vr)
				return;
			if (hand < 0) {
				vr->Vibrate(VRHandLeft, shape, amplitude, seconds);
				vr->Vibrate(VRHandRight, shape, amplitude, seconds);
			} else {
				vr->Vibrate(hand, shape, amplitude, seconds);
			}
		}

		void Client::VRPoseReceived(int playerId, const VRNetPose& pose) {
			RemoteVRPose& r = vrRemotePoses[playerId];
			r.pose = pose;
			r.receivedTime = time;
		}

		const VRNetPose* Client::GetRemoteVRPose(int playerId) const {
			auto it = vrRemotePoses.find(playerId);
			if (it == vrRemotePoses.end())
				return nullptr;
			// A player who took the headset off, or stopped sending, goes back to
			// the ordinary animation after a second.
			if (time - it->second.receivedTime > 1.0F || time < it->second.receivedTime)
				return nullptr;
			return &it->second.pose;
		}

		void Client::VRPresenceConfigured(uint8_t flags, uint8_t maxRate) {
			vrServerFlags = flags;
			float rate = Clamp(static_cast<float>(maxRate), 5.0F, 60.0F);
			vrPoseInterval = 1.0F / rate;
			SPLog("[VR] Server relays VR poses at up to %.0fHz%s", rate,
			      (flags & VRConfigBodySync) ? ", with room-scale body sync" : "");
		}
	} // namespace client
} // namespace spades
