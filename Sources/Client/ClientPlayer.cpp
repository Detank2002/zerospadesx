/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

#include "CTFGameMode.h"
#include "Client.h"
#include "ClientPlayer.h"
#include "GameConstants.h"
#include <Core/VoxelModel.h>
#include <Core/VoxelModelLoader.h>
#include "GameMap.h"
#include "GunCasing.h"
#include "IAudioChunk.h"
#include "IAudioDevice.h"
#include "IImage.h"
#include "IModel.h"
#include "IRenderer.h"
#include "Player.h"
#include "Weapon.h"
#include "World.h"
#include "NetClient.h"
#include <Core/Bitmap.h>
#include <Core/Settings.h>
#include <ScriptBindings/IBlockSkin.h>
#include <ScriptBindings/IGrenadeSkin.h>
#include <ScriptBindings/ISpadeSkin.h>
#include <ScriptBindings/IThirdPersonToolSkin.h>
#include <ScriptBindings/IToolSkin.h>
#include <ScriptBindings/IViewToolSkin.h>
#include <ScriptBindings/IWeaponSkin.h>
#include <ScriptBindings/ScriptFunction.h>

#undef interface

SPADES_SETTING(cg_ragdoll);
DEFINE_SPADES_SETTING(cg_animations, "1");
// A lit lamp flickers a little, as a torch does. 0 holds it steady.
DEFINE_SPADES_SETTING(cg_flashlightFlicker, "1");
SPADES_SETTING(cg_shake);
SPADES_SETTING(r_hdr);
DEFINE_SPADES_SETTING(cg_environmentalAudio, "1");
DEFINE_SPADES_SETTING(cg_classicPlayerModels, "0");
// Draw the model sets a server names (the Player Models extension); off, everyone
// is drawn with the usual model.
DEFINE_SPADES_SETTING(cg_customPlayerModels, "1");
DEFINE_SPADES_SETTING(cg_classicViewWeapon, "0");
// VR: 0 draws just the tool, 1 adds forearms, 2 whole arms from the shoulder.
DEFINE_SPADES_SETTING(vr_showArms, "1");
// A laser from the gun, spade or block in your hand, along the ray it acts on,
// with a dot where it meets the map. Only you see yours.
DEFINE_SPADES_SETTING(vr_laser, "1");
// Where a gun's laser leaves it, in the gun's frame from the grip (blocks):
// ahead along the barrel, and up (z is down) to its top.
static constexpr float kVRLaserAhead = 0.72F, kVRLaserUp = -0.14F;
// Your own torso and legs when you look down.
DEFINE_SPADES_SETTING(vr_showBody, "1");
SPADES_SETTING(vr_scope);
// For tests: the rifle's scope held up to the eye, straight ahead of the head,
// with no hands (a PC test headset has none), so its view is drawn every frame.
DEFINE_SPADES_SETTING(vr_debugScope, "0");
DEFINE_SPADES_SETTING(cg_viewWeaponX, "0");
DEFINE_SPADES_SETTING(cg_viewWeaponY, "0");
DEFINE_SPADES_SETTING(cg_viewWeaponZ, "0");
DEFINE_SPADES_SETTING(cg_viewWeaponSide, "1");
DEFINE_SPADES_SETTING(cg_hideBody, "0");
DEFINE_SPADES_SETTING(cg_hideArms, "0");
DEFINE_SPADES_SETTING(cg_debugToolSkinAnchors, "0");
DEFINE_SPADES_SETTING(cg_trueAimDownSight, "1");
DEFINE_SPADES_SETTING(cg_muzzleFire, "0");

SPADES_SETTING(cg_orientationSmoothing);

SPADES_SETTING(cg_pngScope);

namespace spades {
	namespace client {

		namespace {
			/** `torso` turned about its hips, and moved along itself, so that its
			 * top is at `neck`. The torso models hang down their +z from the neck;
			 * `length` is how far down the hips are. */
			Matrix4 LeanTorsoToNeck(const Matrix4& torso, Vector3 neck, float length) {
				const Vector3 top = torso.GetOrigin();
				const Vector3 hips = top + torso.GetAxis(2) * length;

				// A head this far off the eye is lag or a floor measured wrong, and a
				// torso lying flat looks worse than a head a little off its shoulders.
				Vector3 d = neck - top;
				float horiz = std::sqrt(d.x * d.x + d.y * d.y);
				if (horiz > 0.5F) {
					d.x *= 0.5F / horiz;
					d.y *= 0.5F / horiz;
				}
				d.z = Clamp(d.z, -0.3F, 0.6F); // z is down: up 0.3, down 0.6
				neck = top + d;

				Vector3 from = (top - hips).Normalize();
				Vector3 to = neck - hips;
				float len = to.GetLength();
				if (len < 0.1F)
					return torso;
				to = to / len;

				// The smallest turn taking `from` onto `to` (Rodrigues).
				Vector3 k = Vector3::Cross(from, to);
				float s = k.GetLength(), c = Vector3::Dot(from, to);
				auto turn = [&](const Vector3& v) {
					if (s < 1.0E-5F)
						return v;
					Vector3 u = k / s;
					return v * c + Vector3::Cross(u, v) * s + u * (Vector3::Dot(u, v) * (1.0F - c));
				};
				return Matrix4::FromAxis(turn(torso.GetAxis(0)), turn(torso.GetAxis(1)),
				                         turn(torso.GetAxis(2)), neck);
			}

			/**
			 * How far the glare sits ahead of the headlamp model's front face, in the
			 * model's voxels, so the voxels don't swallow it.
			 */
			const float kHeadlampLensClearance = 0.5F;

			/** How far above the lower body's origin the torso is posed, in blocks. */
			float TorsoHeight(bool crouch) { return crouch ? 0.5F : 1.0F; }

			/** How far below the torso's top the head is posed, in blocks. */
			float HeadDrop(bool crouch) { return crouch ? 0.05F : 0.0F; }

			/** Places the headlamp model on the third-person head, in the head's frame. */
			Matrix4 HeadlampModelMatrix() {
				return Matrix4::Scale(0.1F) * Matrix4::Scale(-1, -1, 1)
					* Matrix4::Translate(0.0F, 0.0F, -1.0F)
					* Matrix4::Scale(0.55F);
			}
		} // namespace

		class SandboxedRenderer : public IRenderer {
			Handle<IRenderer> base;
			AABB3 clipBox;
			bool allowDepthHack;
			bool allowPlayerXRay = false;
			Vector3 xrayColor = MakeVector3(1, 1, 1);
			std::map<IModel*, std::string> modelFiles;
			std::vector<std::pair<std::string, Matrix4>>* capture = nullptr;

			void OnProhibitedAction() {}

			bool CheckVisibility(const AABB3& box) {
				if (!clipBox.Contains(box) ||
					!std::isfinite(box.min.x) ||
					!std::isfinite(box.min.y) ||
					!std::isfinite(box.min.z) ||
					!std::isfinite(box.max.x) ||
					!std::isfinite(box.max.y) ||
					!std::isfinite(box.max.z)) {
					OnProhibitedAction();
					return false;
				}
				return true;
			}

		protected:
			~SandboxedRenderer() {}

		public:
			SandboxedRenderer(Handle<IRenderer> base) : base(std::move(base)) {}

			void SetClipBox(const AABB3& b) { clipBox = b; }
			void SetAllowDepthHack(bool h) { allowDepthHack = h; }

			/**
			 * Makes every model submitted from here on draw again, in `color`, where it
			 * is hidden behind the world.
			 *
			 * Set on the sandbox rather than on individual models because the weapon in
			 * a player's hands is submitted by their AngelScript skin, which this class
			 * is the only thing standing between and the renderer. Flagging it here is
			 * what reveals the player and what they are holding as one shape.
			 *
			 * It is also the permission: a skin that flags a model itself gets nothing,
			 * since only this client decides who is revealed.
			 */
			void SetPlayerXRay(bool enable, const Vector3& color) {
				allowPlayerXRay = enable;
				xrayColor = color;
			}

			void Init() { OnProhibitedAction(); }
			void Shutdown() { OnProhibitedAction(); }

			Handle<IImage> RegisterImage(const char* filename) {
				return base->RegisterImage(filename);
			}
			Handle<IModel> RegisterModel(const char* filename) {
				Handle<IModel> m = base->RegisterModel(filename);
				if (m)
					modelFiles[m.GetPointerOrNull()] = filename;
				return m;
			}

			/** Notes each model drawn, by its file, and where, into `into` (null:
			 * stop). How the sights of a skin that does not give them are found. */
			void SetModelCapture(std::vector<std::pair<std::string, Matrix4>>* into) {
				capture = into;
			}

			Handle<IImage> CreateImage(Bitmap& bmp) { return base->CreateImage(bmp); }
			Handle<IModel> CreateModel(VoxelModel& m) { return base->CreateModel(m); }

			void SetGameMap(stmp::optional<GameMap&>) { OnProhibitedAction(); }

			void SetFogDistance(float) { OnProhibitedAction(); }
			void SetFogColor(Vector3) { OnProhibitedAction(); }

			void StartScene(const SceneDefinition&) { OnProhibitedAction(); }

			void AddLight(const client::DynamicLightParam& light) {
				AABB3 aabb{light.origin, light.origin};
				if (light.type == DynamicLightTypeLinear)
					aabb += light.point2;

				if (CheckVisibility(aabb.Inflate(light.radius)))
					base->AddLight(light);
			}

			void RenderModel(IModel& model, const ModelRenderParam& _p) {
				if (capture) {
					auto it = modelFiles.find(&model);
					if (it != modelFiles.end())
						capture->emplace_back(it->second, _p.matrix);
				}
				ModelRenderParam p = _p;

				if (p.depthHack && !allowDepthHack) {
					OnProhibitedAction();
					p.depthHack = false;
				}

				if (allowPlayerXRay) {
					p.xray = true;
					p.xrayColor = xrayColor;
				} else if (p.xray) {
					OnProhibitedAction();
					p.xray = false;
				}

				// Overbright surfaces bypass the fog
				p.customColor.x = Clamp(p.customColor.x, 0.0F, 1.0F);
				p.customColor.y = Clamp(p.customColor.y, 0.0F, 1.0F);
				p.customColor.z = Clamp(p.customColor.z, 0.0F, 1.0F);

				// NaN values bypass the fog
				if (p.customColor.IsNaN()) {
					OnProhibitedAction();
					return;
				}

				auto bounds = (p.matrix * OBB3(model.GetBoundingBox())).GetBoundingAABB();
				if (CheckVisibility(bounds))
					base->RenderModel(model, p);
			}
			void AddDebugLine(Vector3 a, Vector3 b, Vector4 color) { OnProhibitedAction(); }

			void AddSprite(IImage& image, Vector3 center, float radius, float rotation) {
				Vector3 rad(radius * 1.5F, radius * 1.5F, radius * 1.5F);
				if (CheckVisibility(AABB3(center - rad, center + rad)))
					base->AddSprite(image, center, radius, rotation);
			}
			void AddLongSprite(IImage& image, Vector3 p1, Vector3 p2, float radius) {
				Vector3 rad(radius * 1.5F, radius * 1.5F, radius * 1.5F);
				AABB3 bounds1(p1 - rad, p1 + rad);
				AABB3 bounds2(p2 - rad, p2 + rad);
				bounds1 += bounds2;
				if (CheckVisibility(bounds1))
					base->AddLongSprite(image, p1, p2, radius);
			}
			void AddGlare(IImage& image, const GlareParam& param) {
				if (CheckVisibility(AABB3(param.origin, param.origin)))
					base->AddGlare(image, param);
			}

			void EndScene() { OnProhibitedAction(); }

			void MultiplyScreenColor(Vector3) { OnProhibitedAction(); }

			/** Sets color for image drawing. Deprecated because
			 * some methods treats this as an alpha premultiplied, while
			 * others treats this as an alpha non-premultiplied.
			 * @deprecated */
			void SetColor(Vector4 col) { base->SetColor(col); }

			/** Sets color for image drawing. Always alpha premultiplied. */
			void SetColorAlphaPremultiplied(Vector4 col) { base->SetColorAlphaPremultiplied(col); }

			void DrawFilledTriangle(const Vector2& v0, const Vector2& v1, const Vector2& v2) {
				if (allowDepthHack)
					base->DrawFilledTriangle(v0, v1, v2);
				else
					OnProhibitedAction();
			}

			void DrawFilledRectFade(float x0, float y0, float x1, float y1, Vector4 colorTop, Vector4 colorBottom, bool horizontal) {
				if (allowDepthHack)
					base->DrawFilledRectFade(x0, y0, x1, y1, colorTop, colorBottom, horizontal);
				else
					OnProhibitedAction();
			}

			void DrawImage(stmp::optional<IImage&> img, const Vector2& outTopLeft) {
				if (allowDepthHack)
					base->DrawImage(img, outTopLeft);
				else
					OnProhibitedAction();
			}
			void DrawImage(stmp::optional<IImage&> img, const AABB2& outRect) {
				if (allowDepthHack)
					base->DrawImage(img, outRect);
				else
					OnProhibitedAction();
			}
			void DrawImage(stmp::optional<IImage&> img,
				const Vector2& outTopLeft, const AABB2& inRect) {
				if (allowDepthHack)
					base->DrawImage(img, outTopLeft, inRect);
				else
					OnProhibitedAction();
			}
			void DrawImage(stmp::optional<IImage&> img,
				const AABB2& outRect, const AABB2& inRect) {
				if (allowDepthHack)
					base->DrawImage(img, outRect, inRect);
				else
					OnProhibitedAction();
			}
			void DrawImage(stmp::optional<IImage&> img, const Vector2& outTopLeft,
						   const Vector2& outTopRight, const Vector2& outBottomLeft,
						   const AABB2& inRect) {
				if (allowDepthHack)
					base->DrawImage(img, outTopLeft, outTopRight, outBottomLeft, inRect);
				else
					OnProhibitedAction();
			}

			void DrawFlatGameMap(const AABB2& outRect, const AABB2& inRect) {
				OnProhibitedAction();
			}
			void DrawFlatGameMap(const Vector2& outTopLeft, const Vector2& outTopRight,
			                     const Vector2& outBottomLeft, const AABB2& inRect) {
				OnProhibitedAction();
			}

			void FrameDone() { OnProhibitedAction(); }
			void Flip() { OnProhibitedAction(); }

			Handle<Bitmap> ReadBitmap() {
				OnProhibitedAction();
				return {};
			}

			float ScreenWidth() { return base->ScreenWidth(); }
			float ScreenHeight() { return base->ScreenHeight(); }
		};

		namespace {
			// Never 0, which leaves a sound untagged, and never shared, so a sound
			// still playing for a player who left is not stopped by whoever takes
			// their id next.
			int NextReloadSoundTag() {
				static int next = 0;
				if (++next <= 0)
					next = 1;
				return next;
			}
		} // namespace

		ClientPlayer::ClientPlayer(Player& p, Client& c)
			: client(c), player(p), reloadSoundTag(NextReloadSoundTag()),
			  hasValidOriginMatrix(false) {
			SPADES_MARK_FUNCTION();

			sprintState = 0.0F;
			aimDownState = 0.0F;
			toolRaiseState = 0.0F;
			currentTool = p.GetTool();
			time = 0.0F;
			wasAimingDownSight = false;
			viewWeaponOffset = MakeVector3(0, 0, 0);
			lastFront = MakeVector3(0, 0, 0);
			classicViewWeaponOrigin = MakeVector3(0, 0, 0);

			ScriptContextHandle ctx;
			IAudioDevice& audio = client.GetAudioDevice();
			skinScriptGeneration = ScriptManager::GetInstance()->GetGeneration();

			sandboxedRenderer = Handle<SandboxedRenderer>::New(client.GetRenderer());
			IRenderer& renderer = *sandboxedRenderer;

			static ScriptFunction spadeFactory(
			  "ISpadeSkin@ CreateThirdPersonSpadeSkin(Renderer@, AudioDevice@)");
			spadeSkin = initScriptFactory(spadeFactory, renderer, audio);

			static ScriptFunction spadeViewFactory(
			  "ISpadeSkin@ CreateViewSpadeSkin(Renderer@, AudioDevice@)");
			spadeViewSkin = initScriptFactory(spadeViewFactory, renderer, audio);

			static ScriptFunction blockFactory(
			  "IBlockSkin@ CreateThirdPersonBlockSkin(Renderer@, AudioDevice@)");
			blockSkin = initScriptFactory(blockFactory, renderer, audio);

			static ScriptFunction blockViewFactory(
			  "IBlockSkin@ CreateViewBlockSkin(Renderer@, AudioDevice@)");
			blockViewSkin = initScriptFactory(blockViewFactory, renderer, audio);

			static ScriptFunction grenadeFactory(
			  "IGrenadeSkin@ CreateThirdPersonGrenadeSkin(Renderer@, AudioDevice@)");
			grenadeSkin = initScriptFactory(grenadeFactory, renderer, audio);

			static ScriptFunction grenadeViewFactory(
			  "IGrenadeSkin@ CreateViewGrenadeSkin(Renderer@, AudioDevice@)");
			grenadeViewSkin = initScriptFactory(grenadeViewFactory, renderer, audio);

			static ScriptFunction rifleFactory(
			  "IWeaponSkin@ CreateThirdPersonRifleSkin(Renderer@, AudioDevice@)");
			static ScriptFunction smgFactory(
			  "IWeaponSkin@ CreateThirdPersonSMGSkin(Renderer@, AudioDevice@)");
			static ScriptFunction shotgunFactory(
			  "IWeaponSkin@ CreateThirdPersonShotgunSkin(Renderer@, AudioDevice@)");
			static ScriptFunction rifleViewFactory(
			  "IWeaponSkin@ CreateViewRifleSkin(Renderer@, AudioDevice@)");
			static ScriptFunction smgViewFactory(
			  "IWeaponSkin@ CreateViewSMGSkin(Renderer@, AudioDevice@)");
			static ScriptFunction shotgunViewFactory(
			  "IWeaponSkin@ CreateViewShotgunSkin(Renderer@, AudioDevice@)");
			switch (p.GetWeaponType()) {
				case RIFLE_WEAPON:
					weaponSkin = initScriptFactory(rifleFactory, renderer, audio);
					weaponViewSkin = initScriptFactory(rifleViewFactory, renderer, audio);
					break;
				case SMG_WEAPON:
					weaponSkin = initScriptFactory(smgFactory, renderer, audio);
					weaponViewSkin = initScriptFactory(smgViewFactory, renderer, audio);
					break;
				case SHOTGUN_WEAPON:
					weaponSkin = initScriptFactory(shotgunFactory, renderer, audio);
					weaponViewSkin = initScriptFactory(shotgunViewFactory, renderer, audio);
					break;
				default: SPAssert(false);
			}
		}
		ClientPlayer::~ClientPlayer() {
			spadeSkin->Release();
			blockSkin->Release();
			weaponSkin->Release();
			grenadeSkin->Release();

			spadeViewSkin->Release();
			blockViewSkin->Release();
			weaponViewSkin->Release();
			grenadeViewSkin->Release();
		}

		asIScriptObject* ClientPlayer::initScriptFactory(ScriptFunction& creator,
			IRenderer& renderer, IAudioDevice& audio) {
			ScriptContextHandle ctx = creator.Prepare();
			ctx->SetArgObject(0, reinterpret_cast<void*>(&renderer));
			ctx->SetArgObject(1, reinterpret_cast<void*>(&audio));
			ctx.ExecuteChecked();
			asIScriptObject* res = reinterpret_cast<asIScriptObject*>(ctx->GetReturnObject());
			res->AddRef();
			return res;
		}

		bool ClientPlayer::IsChangingTool() {
			return (currentTool != player.GetTool()) || (toolRaiseState < 0.999F);
		}

		void ClientPlayer::Update(float dt) {
			time += dt;

			{
				const FlashlightBeam& beam =
				  client.activeNet->GetFlashlightBeams().Resolve(player.GetId());
				flashlightFlicker.Update(dt,
				                         player.IsFlashlightOn() ? beam.GetFlickerDarkness() : 0.0F);
			}

			bool isLocalPlayer = player.IsLocalPlayer();
			bool isThirdPerson = ShouldRenderInThirdPersonView();

			PlayerInput actualInput = player.GetInput();
			WeaponInput actualWeapInput = player.GetWeaponInput();

			int zoomAnimation = cg_animations;
			float speed = (zoomAnimation >= 2) ? 2.0F : 1.0F;

			bool aimingDownSight = player.IsToolWeapon() && actualWeapInput.secondary;
			if (aimingDownSight) {
				// play zoom in sound
				if (!wasAimingDownSight && !isThirdPerson) {
					IAudioDevice& audioDevice = client.GetAudioDevice();
					AudioParam param;
					param.volume = 0.1F;
					Handle<IAudioChunk> c =
						audioDevice.RegisterSound("Sounds/Weapons/AimDownSightLocal.opus");
					audioDevice.PlayLocal(c.GetPointerOrNull(),
						MakeVector3(0.4F, -0.3F, 0.5F), param);
				}

				if (zoomAnimation && isLocalPlayer && !isThirdPerson) {
					aimDownState += dt * 8.0F * speed;
					if (aimDownState > 1.0F)
						aimDownState = 1.0F;
				} else {
					aimDownState = 1.0F;
				}
			} else {
				if (zoomAnimation && isLocalPlayer && !isThirdPerson) {
					aimDownState -= dt * 3.0F * speed;
					if (aimDownState < 0.0F)
						aimDownState = 0.0F;
				} else {
					aimDownState = 0.0F;
				}
			}
			wasAimingDownSight = aimingDownSight;

			if (actualInput.sprint) {
				sprintState += dt * 4.0F;
				if (sprintState > 1.0F)
					sprintState = 1.0F;
			} else {
				sprintState -= dt * 3.0F;
				if (sprintState < 0.0F)
					sprintState = 0.0F;
			}

			if (currentTool == player.GetTool()) {
				if (isLocalPlayer) {
					toolRaiseState += dt * 4.0F;
					if (toolRaiseState > 1.0F)
						toolRaiseState = 1.0F;
				} else {
					toolRaiseState = 1.0F;
				}
			} else {
				if (isLocalPlayer) {
					toolRaiseState -= dt * 4.0F;
					if (toolRaiseState < 0.0F) {
						toolRaiseState = 0.0F;
						currentTool = player.GetTool();
						client.activeNet->SendTool();

						// play tool change sound
						IAudioDevice& audioDevice = client.GetAudioDevice();
						Handle<IAudioChunk> c;
						switch (currentTool) {
							case Player::ToolSpade:
								c = audioDevice.RegisterSound("Sounds/Weapons/Spade/RaiseLocal.opus");
								break;
							case Player::ToolBlock:
								c = audioDevice.RegisterSound("Sounds/Weapons/Block/RaiseLocal.opus");
								break;
							case Player::ToolWeapon:
								switch (player.GetWeaponType()) {
									case RIFLE_WEAPON:
										c = audioDevice.RegisterSound(
										  "Sounds/Weapons/Rifle/RaiseLocal.opus");
										break;
									case SMG_WEAPON:
										c = audioDevice.RegisterSound(
										  "Sounds/Weapons/SMG/RaiseLocal.opus");
										break;
									case SHOTGUN_WEAPON:
										c = audioDevice.RegisterSound(
										  "Sounds/Weapons/Shotgun/RaiseLocal.opus");
										break;
								}
								break;
							case Player::ToolGrenade:
								c = audioDevice.RegisterSound(
								  "Sounds/Weapons/Grenade/RaiseLocal.opus");
								break;
						}
						audioDevice.PlayLocal(c.GetPointerOrNull(),
							MakeVector3(0.4F, -0.3F, 0.5F), AudioParam());
					}
				} else {
					toolRaiseState = 0.0F;
					currentTool = player.GetTool();
				}
			}

			if (!isThirdPerson) {
				Vector3 front = player.GetFront();
				Vector3 right = player.GetRight();
				Vector3 up = player.GetUp();

				if (cg_classicViewWeapon) {
					// Offset the view weapon according to the camera movement
					Vector3 diff = front - lastFront;
					viewWeaponOffset.x += Vector3::Dot(diff, right);
					viewWeaponOffset.z += Vector3::Dot(diff, up);
					lastFront = front;

					if (dt > 0.0F)
						viewWeaponOffset *= powf(1.0E-6F, dt);
				} else {
					float scale = dt;

					// Offset the view weapon according to the player movement
					Vector3 v = player.GetVelocity();
					viewWeaponOffset.x += Vector3::Dot(v, right) * scale;
					viewWeaponOffset.y -= Vector3::Dot(v, front) * scale;
					viewWeaponOffset.z += Vector3::Dot(v, up) * scale;

					// Offset the view weapon according to the camera movement
					Vector3 diff = front - lastFront;
					viewWeaponOffset.x += Vector3::Dot(diff, right) * 0.05F;
					viewWeaponOffset.z += Vector3::Dot(diff, up) * 0.05F;
					lastFront = front;

					if (dt > 0.0F)
						viewWeaponOffset *= powf(0.02F, dt);

					// Limit the movement
					auto softLimitFunc = [&](float& v, float minLimit, float maxLimit) {
						float transition = (maxLimit - minLimit) * 0.5F;
						if (v < minLimit)
							v = Mix(v, minLimit, std::min(1.0F, (minLimit - v) / transition));
						if (v > maxLimit)
							v = Mix(v, maxLimit, std::min(1.0F, (v - maxLimit) / transition));
					};

					const float limitX = 0.006F;
					const float limitY = 0.006F;
					softLimitFunc(viewWeaponOffset.x, -limitX, limitY);
					softLimitFunc(viewWeaponOffset.y, -limitX, limitY);
					softLimitFunc(viewWeaponOffset.z, -limitX, limitY);

					// When the player is aiming down the sight, the weapon's movement
					// must be restricted so that other parts of the weapon don't
					// cover the ironsight.
					if (currentTool == Player::ToolWeapon && actualWeapInput.secondary) {
						if (dt > 0.0F)
							viewWeaponOffset *= powf(0.01F, dt);

						const float limitX = 0.003F;
						const float limitY = 0.003F;
						softLimitFunc(viewWeaponOffset.x, -limitX, limitX);
						softLimitFunc(viewWeaponOffset.z, 0, limitY);
					}
				}
			}

			// FIXME: should do for non-active skins?
			asIScriptObject* curSkin = GetCurrentSkin(!isThirdPerson);
			{
				ScriptIToolSkin interface(curSkin);
				interface.Update(dt);
			}
			// The third-person skin a headset's hand holds animates too, muted
			// (the first-person one above makes the sounds), see FiredWeapon
			if (!isThirdPerson && IsVRHandsView()) {
				asIScriptObject* handSkin = GetCurrentSkin(false);
				if (handSkin && handSkin != curSkin) {
					ScriptIToolSkin hand(handSkin);
					hand.SetMuted(true);
					hand.Update(dt);
					hand.SetMuted(client.IsMuted());
				}
			}
		}

		std::string ClientPlayer::ModelPart(const std::string& modelPath, const std::string& part) {
			// The usual file, unless the server has this player in an outfit whose
			// model set has its own (PlayerOutfit.h).
			std::string usual = modelPath + part + ".kv6";
			if (!cg_customPlayerModels)
				return usual;
			const PlayerOutfits& outfits = client.GetPlayerOutfits();
			return outfits.ResolvePart(outfits.GetModelName(player.GetId(), player.GetTeamId()), part,
			                          usual);
		}

		Matrix4 ClientPlayer::GetEyeMatrix() {
			Vector3 eye = player.GetEye();

			if ((int)cg_shake >= 2 && !player.GetWade()) {
				float sp = SmoothStep(sprintState);
				float vel2D = player.GetVelocity().GetSquaredLength2D();
				sp *= std::min(1.0F, (vel2D * 5.0F) / 0.1F);

				float p = cosf(player.GetWalkAnimationProgress() * kTwoPi - 0.8F);
				p = p * p;
				p *= p;
				p *= p;
				p *= p;
				eye.z -= p * 0.06F * sp;
			}

			return Matrix4::FromAxis(-player.GetRight(), player.GetFront(), -player.GetUp(), eye);
		}

		void ClientPlayer::SetSkinParameterForTool(Player::ToolType type, asIScriptObject* skin) {
			Player& p = player;
			Weapon& w = p.GetWeapon();

			WeaponInput actualWeapInput = p.GetWeaponInput();

			const float primaryDelay = p.GetToolPrimaryDelay(type);
			const float secondaryDelay = p.GetToolSecondaryDelay(type);

			switch (type) {
				case Player::ToolSpade: {
					ScriptISpadeSkin interface(skin);

					const float nextSpadeTime = p.GetTimeToNextSpade();
					if (nextSpadeTime > 0.0F) {
						interface.SetActionType(SpadeActionTypeBash);
						interface.SetActionProgress(1.0F - (nextSpadeTime / primaryDelay));
					} else if (actualWeapInput.secondary) {
						interface.SetActionType(p.IsFirstDig()
							? SpadeActionTypeDigStart : SpadeActionTypeDig);
						interface.SetActionProgress(1.0F - (p.GetTimeToNextDig() / secondaryDelay));
					} else {
						interface.SetActionType(SpadeActionTypeIdle);
						interface.SetActionProgress(0.0F);
					}
				} break;
				case Player::ToolBlock: {
					ScriptIBlockSkin interface(skin);
					interface.SetReadyState(1.0F - (p.GetTimeToNextBlock() / primaryDelay));
					interface.SetBlockColor(ConvertColorRGB(p.GetBlockColor()));
				} break;
				case Player::ToolGrenade: {
					ScriptIGrenadeSkin interface(skin);
					interface.SetReadyState(1.0F - (p.GetTimeToNextGrenade() / primaryDelay));
					interface.SetCookTime(p.IsCookingGrenade() ? p.GetGrenadeCookTime() : 0.0F);
				} break;
				case Player::ToolWeapon: {
					ScriptIWeaponSkin interface(skin);
					interface.SetReadyState(1.0F - (w.GetTimeToNextFire() / primaryDelay));
					interface.SetAimDownSightState(cg_trueAimDownSight ? aimDownState : aimDownState * 0.5F);
					interface.SetAmmo(w.GetAmmo());
					interface.SetClipSize(w.GetClipSize());
					interface.SetReloading(w.IsReloading());
					interface.SetReloadProgress(w.GetReloadProgress());
				} break;
				default: SPInvalidEnum("currentTool", type);
			}
		}

		asIScriptObject* ClientPlayer::GetCurrentSkin(bool viewSkin) {
			switch (currentTool) {
				case Player::ToolSpade: return viewSkin ? spadeViewSkin : spadeSkin; break;
				case Player::ToolBlock: return viewSkin ? blockViewSkin : blockSkin; break;
				case Player::ToolWeapon: return viewSkin ? weaponViewSkin : weaponSkin; break;
				case Player::ToolGrenade: return viewSkin ? grenadeViewSkin : grenadeSkin; break;
				default: SPInvalidEnum("currentTool", currentTool);
			}
		}

		void ClientPlayer::SetCommonSkinParameter(asIScriptObject* skin) {
			asIScriptObject* curSkin = GetCurrentSkin(!ShouldRenderInThirdPersonView());

			float sprint = SmoothStep(sprintState);
			float putdown = 1.0F - toolRaiseState;
			putdown *= putdown;
			putdown = std::min(1.0F, putdown * 1.5F);
			float raiseState = (skin == curSkin) ? (1.0F - putdown) : 0.0F;

			{
				ScriptIToolSkin interface(skin);
				interface.SetTeamColor(ConvertColorRGB(player.GetColor()));
				interface.SetRaiseState(player.IsLocalPlayer() ? raiseState : 1.0F);
				interface.SetSprintState(sprint);
				interface.SetMuted(client.IsMuted());
			}
		}

		Vector3 ClientPlayer::GetFlashlightDirection() {
			// The orientation the player's model is rendered with, and nothing more, so
			// that every client points the beam alike.
			return player.GetFront(cg_orientationSmoothing);
		}

		std::array<Vector3, 3> ClientPlayer::GetFlashlightAxes() {
			// Roll the cone around its own direction: the player's up vector snaps with
			// network updates and is undefined when looking straight up or down.
			static const Vector3 worldUp = MakeVector3(0, 0, -1);

			std::array<Vector3, 3> axes;
			axes[2] = GetFlashlightDirection();
			axes[0] = Vector3::Cross(axes[2], worldUp);
			if (axes[0].GetSquaredLength() < 1.0E-6F) // pointing straight up or down
				axes[0] = Vector3::Cross(axes[2], MakeVector3(0, 1, 0));
			axes[0] = axes[0].Normalize();
			axes[1] = Vector3::Cross(axes[0], axes[2]);
			return axes;
		}

		std::string ClientPlayer::GetModelPath() {
			std::string modelPath = "Models/Player/";
			if (player.IsZombie())
				modelPath += "Zombie/";
			else if (!cg_classicPlayerModels)
				modelPath += player.GetWeapon().GetName() + "/";
			return modelPath;
		}

		bool ClientPlayer::IsLampBuried(const Vector3& lightOrigin) {
			World* world = client.GetWorld();
			if (!world)
				return false;

			GameMap* map = world->GetMap().GetPointerOrNull();
			if (!map)
				return false;

			IntVector3 block = lightOrigin.Floor();
			return map->IsSolidWrapped(block.x, block.y, block.z);
		}

		Vector3 ClientPlayer::GetHeadlampLens(const Matrix4& head) {
			// The model's front is +Y, which the head's frame turns to face forward
			AABB3 const bounds = GetHeadlampBounds();
			Vector3 const lens = MakeVector3((bounds.min.x + bounds.max.x) * 0.5F,
				bounds.max.y + kHeadlampLensClearance,
				(bounds.min.z + bounds.max.z) * 0.5F);
			return (head * HeadlampModelMatrix() * lens).GetXYZ();
		}

		AABB3 ClientPlayer::GetHeadlampBounds() {
			Handle<IModel> model = client.GetRenderer().RegisterModel("Models/Player/Headlamp.kv6");
			return model->GetBoundingBox();
		}

		bool ClientPlayer::IsFlashlightLit() {
			if (!player.IsFlashlightOn() || flashlightFlicker.IsDark())
				return false;
			return client.activeNet->GetFlashlightBeams().Resolve(player.GetId()).Emits();
		}

		void ClientPlayer::AddFlashlightToScene(const Vector3& lightOrigin, bool viewed) {
			Player& p = player;
			IRenderer& renderer = client.GetRenderer();

			if (!IsFlashlightLit())
				return;

			const FlashlightBeam& beam = client.activeNet->GetFlashlightBeams().Resolve(p.GetId());
			const float fadeIn = GetFlashlightFadeIn();

			FlashlightBudget::Beam offer;
			offer.glowColor = beam.GetColor() * fadeIn;
			offer.viewed = viewed;

			DynamicLightParam& light = offer.light;
			light.type = DynamicLightTypeSpotlight;
			light.origin = lightOrigin;
			light.radius = beam.GetReach();
			light.color = beam.GetColor() * (fadeIn * (r_hdr ? 3.0F : 1.5F));
			light.spotAngle = beam.GetConeAngle();
			light.spotAxis = GetFlashlightAxes();
			Handle<IImage> img = renderer.RegisterImage("Gfx/Spotlight.jpg");
			light.image = img.GetPointerOrNull();

			// Lit as a spotlight or as a glow where it lands, once every lamp is known
			client.flashlightBudget.Offer(offer);
		}

		float ClientPlayer::GetFlashlightFadeIn() {
			Player& p = player;
			const float t = p.GetWorld().GetTime();
			float sinceOn = t - p.GetFlashlightOnTime();
			float fade = 1.0F - expf(-sinceOn * 5.0F);
			if (!(int)cg_flashlightFlicker)
				return fade;

			// A slight flicker, as a torch's: a few percent of unsteadiness from
			// sines that never line up, and now and then a short dip, as from a
			// loose contact. Each player's has its own phase, so a crowd's lamps
			// do not pulse together. The beam and its glare both use this.
			const float phase = static_cast<float>(p.GetId()) * 1.618F;
			float wobble = 0.5F * sinf(t * 11.3F + phase) + 0.3F * sinf(t * 23.7F + phase * 2.1F) +
			               0.2F * sinf(t * 41.9F + phase * 3.7F);
			float flicker = 1.0F - 0.035F * (wobble * 0.5F + 0.5F);
			// The dips: a tenth of a second at a time, in about one in fifty.
			const float slot = floorf(t * 10.0F);
			const float h = sinf(slot * 12.9898F + phase * 78.233F) * 43758.5453F;
			const float noise = h - floorf(h);
			if (noise > 0.98F)
				flicker *= 0.82F;
			return fade * flicker;
		}

		void ClientPlayer::UpdateFlashlightGlare(const Vector3& lampPosition) {
			Player& p = player;

			// The glare drops out with the beam.
			if (!IsFlashlightLit())
				return;

			const FlashlightBeam& beam = client.activeNet->GetFlashlightBeams().Resolve(p.GetId());

			World* world = client.GetWorld();
			if (!world)
				return;

			GameMap* map = world->GetMap().GetPointerOrNull();
			if (!map)
				return;

			FlashlightGlare::Lamp lamp;
			lamp.position = lampPosition;
			lamp.direction = GetFlashlightDirection();
			lamp.coneAngle = beam.GetConeAngle();
			lamp.reach = beam.GetReach();
			lamp.color = beam.GetColor();
			lamp.brightness = GetFlashlightFadeIn();
			flashlightGlare.Update(lamp, client.GetLastSceneDef().viewOrigin, *map, time);
		}

		void ClientPlayer::AddFlashlightGlareToScene(float ambient, float share) {
			flashlightGlare.AddToScene(client.GetRenderer(), ambient, share);
		}

		void ClientPlayer::AddToSceneFirstPersonView() {
			Player& p = player;
			Weapon& w = p.GetWeapon();
			IRenderer& renderer = client.GetRenderer();
			Matrix4 eyeMatrix = GetEyeMatrix();
			Vector3 vel = p.GetVelocity();

			std::string modelPath = GetModelPath();

			const Vector3 origin = eyeMatrix.GetOrigin();

			// set clipping box to prevent drawing models that are too large
			AABB3 clip = AABB3(
				origin - Vector3(20.0F, 20.0F, 20.0F),
				origin + Vector3(20.0F, 20.0F, 20.0F)
			);
			sandboxedRenderer->SetClipBox(clip);
			sandboxedRenderer->SetAllowDepthHack(true); // allow depthhack

			// Nothing hides a player from their own eyes, and the flag persists on the
			// sandbox between frames, so it has to be cleared and not merely not set.
			sandboxedRenderer->SetPlayerXRay(false, MakeVector3(1, 1, 1));

			// This path also runs for a remote player the camera is following.
			AddFlashlightToScene(eyeMatrix.GetOrigin(), true);

			Vector3 leftHand, rightHand;
			leftHand = MakeVector3(0, 0, 0);
			rightHand = MakeVector3(0, 0, 0);

			Handle<IModel> model;
			ModelRenderParam param;
			param.depthHack = true;
			param.customColor = ConvertColorRGB(p.GetColor());

			// disable depth hack when `cg_debugToolSkinAnchors` is set
			// so that the drawn debug lines intersect with the weapon model
			if (cg_debugToolSkinAnchors && currentTool == Player::ToolWeapon) {
				param.depthHack = false;
				sandboxedRenderer->SetAllowDepthHack(false);
			}

			asIScriptObject* curSkin = GetCurrentSkin(true);
			SetSkinParameterForTool(currentTool, curSkin);
			SetCommonSkinParameter(curSkin);

			float weapSide = Clamp((float)cg_viewWeaponSide, -1.0F, 1.0F);
			bool leftHanded = weapSide < 0.0F;

			// view weapon
			Vector3 viewWeaponSway = viewWeaponOffset;
			viewWeaponSway.x *= leftHanded ? -1.0F : 1.0F;

			// Moving this to the scripting environment means
			// breaking compatibility with existing scripts.
			if (cg_classicViewWeapon) {
				Matrix4 mat = Matrix4::Scale(0.033F);
				Vector3 trans = MakeVector3(0, 0, 0);

				float bob = std::max(fabsf(vel.x), fabsf(vel.y)) / 1000;
				int timer = (int)(time * 1000);
				bob *= (timer % 1024 < 512)
					? (timer % 512) - 255.5F
					: 255.5F - (timer % 512);
				trans.y += bob;

				if (!p.IsOnGroundOrWade())
					trans.z -= vel.z * 0.2F;

				float raiseState = p.IsLocalPlayer() ? toolRaiseState : 1.0F;
				if (sprintState > 0.0F || raiseState < 1.0F) {
					float per = std::max(sprintState, 1.0F - raiseState) * 5;
					trans.x -= per;
					trans.z += per;
				}

				WeaponInput actualWeapInput = p.GetWeaponInput();

				const float primaryDelay = p.GetToolPrimaryDelay(currentTool);
				const float secondaryDelay = p.GetToolSecondaryDelay(currentTool);

				switch (currentTool) {
					case Player::ToolSpade: {
						model = renderer.RegisterModel("Models/Weapons/Spade/Spade.kv6");

						const float nextSpadeTime = p.GetTimeToNextSpade();
						const float nextDigTime = p.GetTimeToNextDig();

						if (nextSpadeTime > 0.0F) {
							float f = nextSpadeTime / primaryDelay;
							mat = Matrix4::Rotate(MakeVector3(1, 0, 0), f * 1.25F) * mat;
							mat = Matrix4::Translate(0.0F, f * 0.5F, f * 0.25F) * mat;
						} else if (actualWeapInput.secondary && nextDigTime > 0.0F) {
							float f = nextDigTime / secondaryDelay;
							float f2;
							if (f >= 0.6F) {
								f2 = 0.0F;
								f = 1.0F - f;
							} else if (f >= 0.3F) {
								f2 = 0.6F - f;
								f = 0.4F;
							} else if (f >= 0.1F) {
								f2 = 0.3F;
								f = 0.4F;
							} else {
								f2 = f * 3;
								f *= 4;
							}

							mat = Matrix4::Translate(MakeVector3(f2 * 0.5F, f * 0.25F, -f2 * 1.25F)) * mat;
							mat = Matrix4::Rotate(MakeVector3(1, 0, 0), f / 0.32F) * mat;
							mat = Matrix4::Rotate(MakeVector3(0, 1, 0), -f) * mat;
						}
					} break;
					case Player::ToolBlock: {
						model = renderer.RegisterModel("Models/Weapons/Block/Block.kv6");
						param.customColor = ConvertColorRGB(p.GetBlockColor());

						const float nextBlockTime = p.GetTimeToNextBlock();

						if (nextBlockTime > 0.0F) {
							float f = nextBlockTime * 5;
							trans.x -= f;
							trans.z += f;
						}
					} break;
					case Player::ToolGrenade: {
						model = renderer.RegisterModel("Models/Weapons/Grenade/Grenade.kv6");

						const float nextGrenadeTime = p.GetTimeToNextGrenade();
						const float cookGrenadeTime = p.GetGrenadeCookTime();

						if (p.IsCookingGrenade() && cookGrenadeTime > 0.0F) {
							trans.x -= cookGrenadeTime;
							trans.z -= cookGrenadeTime;
						}
						if (nextGrenadeTime > 0.0F) {
							float f = nextGrenadeTime * 5;
							trans.x -= f;
							trans.z += f;
						}
					} break;
					case Player::ToolWeapon: {
						// don't draw model when aiming
						if (aimDownState > 0.99F)
							return;

						switch (w.GetWeaponType()) {
							case RIFLE_WEAPON:
								model = renderer.RegisterModel("Models/Weapons/Rifle/Weapon.kv6");
								break;
							case SMG_WEAPON:
								model = renderer.RegisterModel("Models/Weapons/SMG/Weapon.kv6");
								break;
							case SHOTGUN_WEAPON:
								model = renderer.RegisterModel("Models/Weapons/Shotgun/Weapon.kv6");
								break;
						}

						const float nextFireTime = w.GetTimeToNextFire();
						const float reloadProgress = 1.0F - w.GetReloadProgress();

						if (reloadProgress > 0.0F && !w.IsReloadSlow()) {
							float f = reloadProgress * 10;
							trans.x -= f;
							trans.z += f;
						}

						if (nextFireTime > 0.0F) {
							float f = nextFireTime / 32;
							float f2 = f * 10;
							trans.x -= f2;
							trans.z += f2;
							mat = Matrix4::Rotate(MakeVector3(-1, 0, 0), DEG2RAD(f * 280)) * mat;
						}
					} break;
				}

				trans += Vector3(-0.33F, 0.66F, 0.4F);
				trans += 0.015F; // adjust to match voxlap
				trans.x *= fabsf(weapSide);

				// add weapon sway
				trans += viewWeaponSway;

				mat = Matrix4::Translate(trans) * mat;

				// mirror weapon geometry when left-handed
				if (leftHanded)
					mat = Matrix4::Scale(-1.0F, 1.0F, 1.0F) * mat;

				classicViewWeaponOrigin = mat.GetOrigin();
				param.matrix = eyeMatrix * mat;
				renderer.RenderModel(*model, param);

				return;
			}

			// bobbing
			{
				float sp = 1.0F - aimDownState;
				sp *= 0.3F;

				float vel2D = vel.GetSquaredLength2D();
				sp *= std::min(1.0F, (vel2D * 5.0F) / 0.1F);

				float walkAng = p.GetWalkAnimationProgress() * kTwoPi;
				float vl = cosf(walkAng);
				vl *= vl;

				viewWeaponSway.x += sinf(walkAng) * 0.013F * sp;
				viewWeaponSway.z += vl * 0.018F * sp;
			}

			// slow pulse
			{
				float sp = 1.0F - aimDownState;
				float vl = sinf(time * 1.0F);

				viewWeaponSway.x += vl * 0.001F * sp;
				viewWeaponSway.y += vl * 0.0007F * sp;
				viewWeaponSway.z += vl * 0.003F * sp;
			}

			// common process
			{
				ScriptIViewToolSkin interface(curSkin);
				interface.SetEyeMatrix(eyeMatrix);
				interface.SetSwing(viewWeaponSway);
			}
			{
				ScriptIToolSkin interface(curSkin);
				interface.AddToScene();
			}
			{
				ScriptIViewToolSkin interface(curSkin);
				leftHand = interface.GetLeftHandPosition();
				rightHand = interface.GetRightHandPosition();
			}

			Vector3 o = p.GetFront();

			float yaw = atan2f(o.y, o.x) + kHalfPi;

			// lower axis
			Matrix4 const lower = Matrix4::Translate(p.GetOrigin())
				* Matrix4::Rotate(MakeVector3(0, 0, 1), yaw);

			Matrix4 const scaler = Matrix4::Scale(0.1F)
				* Matrix4::Scale(-1, -1, 1);

			PlayerInput const inp = p.GetInput();

			float const legsPosX = 0.25F;
			float const legsPosY = inp.crouch ? 1.25F : 1.0F;
			float const legsPosZ = inp.crouch ? 0.05F : 0.1F;
			float const torsoPosZ = TorsoHeight(inp.crouch);

			Vector2 legsRot;
			legsRot.x = Vector3::Dot(vel, p.GetFront2D());
			legsRot.y = Vector3::Dot(vel, p.GetRight());
			legsRot *= sinf(p.GetWalkAnimationProgress() * kTwoPi) * 3.0F;

			Matrix4 const legLeft = lower
				* Matrix4::Translate(-legsPosX, legsPosY, -legsPosZ)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), -legsRot.x)
				* Matrix4::Rotate(MakeVector3(0, 1, 0), -legsRot.y);

			Matrix4 const legRight = lower
				* Matrix4::Translate(legsPosX, legsPosY, -legsPosZ)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), legsRot.x)
				* Matrix4::Rotate(MakeVector3(0, 1, 0), legsRot.y);

			Matrix4 const torso = lower
				* Matrix4::Translate(0.0F, 1.0F, -torsoPosZ);

			// Legs and Torso
			if (!cg_hideBody) {
				model = inp.crouch
					? renderer.RegisterModel(ModelPart(modelPath, "LegCrouch").c_str())
					: renderer.RegisterModel(ModelPart(modelPath, "Leg").c_str());
				param.matrix = legLeft * scaler;
				renderer.RenderModel(*model, param);

				model = inp.crouch
					? renderer.RegisterModel(ModelPart(modelPath, "LegRightCrouch").c_str())
					: renderer.RegisterModel(ModelPart(modelPath, "LegRight").c_str());
				param.matrix = legRight * scaler;
				renderer.RenderModel(*model, param);

				model = inp.crouch
					? renderer.RegisterModel(ModelPart(modelPath, "TorsoCrouch").c_str())
					: renderer.RegisterModel(ModelPart(modelPath, "Torso").c_str());
				param.matrix = torso * scaler;
				renderer.RenderModel(*model, param);
			}

			// Arms
			if (!cg_hideArms) {
				Vector3 hands[] = {leftHand, rightHand};
				Vector3 shoulders[] = {{0.4F, 0.0F, 0.25F}, {-0.4F, 0.0F, 0.25F}};
				Vector3 benddirs[] = {{0.5F, 0.2F, 0.0F}, {-0.5F, 0.2F, 0.0F}};

				if (leftHanded) {
					std::swap(hands[0], hands[1]);
					hands[0].x = -hands[0].x;
					hands[1].x = -hands[1].x;
					shoulders[0].x = -shoulders[0].x;
					shoulders[1].x = -shoulders[1].x;
					benddirs[0].x = -benddirs[0].x;
					benddirs[1].x = -benddirs[1].x;
				}

				const float leftHandSqr = hands[0].GetSquaredLength();
				const float rightHandSqr = hands[1].GetSquaredLength();
				if (leftHandSqr > 0.01F || rightHandSqr > 0.01F) {
					Handle<IModel> armModel = renderer.RegisterModel(ModelPart(modelPath, "Arm").c_str());
					Handle<IModel> upperModel = renderer.RegisterModel(ModelPart(modelPath, "UpperArm").c_str());
					Handle<IModel> armRightModel = renderer.RegisterModel(ModelPart(modelPath, "ArmRight").c_str());
					Handle<IModel> upperRightModel = renderer.RegisterModel(ModelPart(modelPath, "UpperArmRight").c_str());

					// manual adjustment
					Vector3 trans = MakeVector3(0, 0, 0);
					float sp = 1.0F - aimDownState;
					trans.x += (float)cg_viewWeaponX * sp;
					trans.y += (float)cg_viewWeaponY * sp;
					trans.z += (float)cg_viewWeaponZ * sp;
					trans.x *= fabsf(weapSide);

					// add weapon sway
					trans += viewWeaponSway * sp;

					const float armlen = 0.5F;
					const float armlenSqr = armlen * armlen;
					const Matrix4 armsScale = Matrix4::Scale(0.05F);

					auto getArmMatrix = [&](const Vector3& v1, const Vector3& v2) {
						Vector3 axes[3];
						axes[2] = (v1 - v2).Normalize();
						axes[1] = Vector3::Cross(axes[2], MakeVector3(0, 0, 1)).Normalize();
						axes[0] = Vector3::Cross(axes[1], axes[2]).Normalize();
						Matrix4 mat = Matrix4::FromAxis(axes[0], axes[1], axes[2], v2);
						if (leftHanded)
							mat = Matrix4::Scale(-1, 1, 1) * mat;
						return eyeMatrix * mat * armsScale;
					};

					auto renderArm = [&](int i) {
						const Vector3 shoulder = shoulders[i] + trans;
						const Vector3& hand = hands[i];
						const Vector3& benddir = benddirs[i];

						const Vector3 shoulderToHand = hand - shoulder;
						Vector3 bend = Vector3::Cross(benddir, shoulderToHand).Normalize();
						bend.z = fabsf(bend.z);

						const float distSqr = shoulderToHand.GetSquaredLength();
						const float bendlen = sqrtf(std::max(armlenSqr - distSqr * 0.25F, 0.0F));
						const Vector3 elbow = ((hand + shoulder) * 0.5F) + (bend * bendlen);

						const bool isLeftArm = leftHanded ? (i != 0) : (i == 0);

						// draw arm
						param.matrix = getArmMatrix(hand, elbow);
						renderer.RenderModel(isLeftArm ? *armModel : *armRightModel, param);

						// draw upper arm
						param.matrix = getArmMatrix(elbow, shoulder);
						renderer.RenderModel(isLeftArm ? *upperModel : *upperRightModel, param);
					};

					if (leftHandSqr > 0.01F)
						renderArm(0);
					if (rightHandSqr > 0.01F)
						renderArm(1);
				}
			}

			// --- local view ends
		}

		void ClientPlayer::AddToSceneThirdPersonView() {
			Player& p = player;
			Weapon& w = p.GetWeapon();
			IRenderer& renderer = client.GetRenderer();
			World* world = client.GetWorld();

			std::string modelPath = GetModelPath();

			Vector3 o = p.GetFront(cg_orientationSmoothing); // interpolated

			// A VR player: their tracked head and hands replace the animated head,
			// arms and tool below, and the body turns with the head rather than
			// with the facing, which jumps to the gun hand while they shoot.
			const VRNetPose* vrPose = p.IsLocalPlayer() ? nullptr : client.GetRemoteVRPose(p.GetId());
			if (vrPose && p.IsAlive()) {
				Vector3 headFwd = vrPose->headRotation.Rotate(MakeVector3(1, 0, 0));
				if (headFwd.GetLength2D() > 0.1F)
					o = MakeVector3(headFwd.x, headFwd.y, 0.0F).Normalize();
			}

			Vector3 front2D = MakeVector3(o.x, o.y, 0).Normalize();
			Vector3 up = MakeVector3(0, 0, -1);
			Vector3 right = -Vector3::Cross(up, front2D).Normalize();

			Handle<IModel> model;
			ModelRenderParam param;
			param.customColor = ConvertColorRGB(p.GetColor());

			if (!p.IsAlive()) {
				if (!cg_ragdoll) {
					model = renderer.RegisterModel(ModelPart(modelPath, "Dead").c_str());
					param.matrix = Matrix4::FromAxis(-right, front2D, -up, p.GetEye());
					param.matrix = param.matrix * Matrix4::Translate(0.0F, 0.0F, -0.1F);
					param.matrix = param.matrix * Matrix4::Scale(0.1F);
					renderer.RenderModel(*model, param);
				}

				return;
			}

			const Vector3 origin = p.GetOrigin();

			// set clipping box to prevent drawing models that are too large
			AABB3 clip = AABB3(
				origin - Vector3(2.0F, 2.0F, 4.0F),
				origin + Vector3(2.0F, 2.0F, 2.0F)
			);
			sandboxedRenderer->SetClipBox(clip);
			sandboxedRenderer->SetAllowDepthHack(false); // disable depthhack

			// Reveal this player through walls when the client is showing them. The body
			// parts below go straight to the renderer and carry the flag on `param`,
			// while the tool the skin submits goes through the sandbox, so both have to
			// be told for the player and what they are holding to come out as one shape.
			Vector3 xrayColor;
			bool reveal = client.ShouldRevealPlayer(player, xrayColor);
			param.xray = reveal;
			param.xrayColor = xrayColor;
			sandboxedRenderer->SetPlayerXRay(reveal, xrayColor);

			// ready for tool rendering
			asIScriptObject* curSkin = GetCurrentSkin(false);
			SetSkinParameterForTool(currentTool, curSkin);
			SetCommonSkinParameter(curSkin);

			float yaw = atan2f(o.y, o.x) + kHalfPi;
			float pitch = -atan2f(o.z, o.GetLength2D());

			// lower axis
			Matrix4 const lower = Matrix4::Translate(origin)
				* Matrix4::Rotate(MakeVector3(0, 0, 1), yaw);

			Matrix4 const scaler = Matrix4::Scale(0.1F)
				* Matrix4::Scale(-1, -1, 1);

			PlayerInput const inp = p.GetInput();

			float const legsPosX = 0.25F;
			float const legsPosY = inp.crouch ? 0.25F : 0.0F;
			float const legsPosZ = inp.crouch ? 0.05F : 0.1F;
			float const headPosZ = HeadDrop(inp.crouch);
			float const torsoPosZ = TorsoHeight(inp.crouch);
			float const armsPosZ = inp.crouch ? 0.0F : 0.1F;

			float armPitch = pitch;

			// slow pulse
			{
				float sp = 1.0F - aimDownState;
				float vl = sinf(time * 1.0F);

				pitch -= vl * 0.005F * sp;
				armPitch += vl * 0.0075F * sp;
			}

			if (inp.sprint)
				armPitch -= 0.9F * sprintState;

			WeaponInput actualWeapInput = p.GetWeaponInput();

			const float primaryDelay = p.GetToolPrimaryDelay(currentTool);
			const float secondaryDelay = p.GetToolSecondaryDelay(currentTool);

			if (currentTool == Player::ToolSpade) {
				const float nextSpadeTime = p.GetTimeToNextSpade();
				const float nextDigTime = p.GetTimeToNextDig();
				if (nextSpadeTime > 0.0F)
					armPitch -= (nextSpadeTime / primaryDelay);
				else if (actualWeapInput.secondary && nextDigTime > 0.0F)
					armPitch -= 1.0F - (nextDigTime / secondaryDelay);
			} else if (currentTool == Player::ToolBlock) {
				const float nextBlockTime = p.GetTimeToNextBlock();
				if (nextBlockTime > 0.0F)
					armPitch -= (nextBlockTime / primaryDelay);
			} else if (currentTool == Player::ToolWeapon) {
				const float nextFireTime = w.GetTimeToNextFire();
				if (nextFireTime > 0.0F)
					armPitch += nextFireTime;
			} else if (currentTool == Player::ToolGrenade) {
				const float fuse = p.GetGrenadeCookTime();
				if (p.IsCookingGrenade() && fuse > 0.0F)
					armPitch += fuse * DEG2RAD(30);
			}

			if (armPitch < 0.0F)
				armPitch = std::max(armPitch, -kHalfPi) * 0.9F;

			Vector3 v = p.GetVelocity();
			Vector2 legsRot;
			legsRot.x = Vector3::Dot(v, p.GetFront2D());
			legsRot.y = Vector3::Dot(v, p.GetRight());
			legsRot *= sinf(p.GetWalkAnimationProgress() * kTwoPi) * 3.0F;

			Matrix4 const legLeft = lower
				* Matrix4::Translate(-legsPosX, legsPosY, -legsPosZ)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), -legsRot.x)
				* Matrix4::Rotate(MakeVector3(0, 1, 0), -legsRot.y);

			Matrix4 const legRight = lower
				* Matrix4::Translate(legsPosX, legsPosY, -legsPosZ)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), legsRot.x)
				* Matrix4::Rotate(MakeVector3(0, 1, 0), legsRot.y);

			Matrix4 torso = lower
				* Matrix4::Translate(0.0F, 0.0F, -torsoPosZ);

			// A VR player's torso leans from the hips to under their tracked head,
			// so that leaning, ducking and a head held above the game's eye keep the
			// head on the shoulders. The legs stay where the body stands.
			if (vrPose) {
				Vector3 headDown = vrPose->headRotation.Rotate(MakeVector3(0, 0, 1));
				// Where AddVRBody hangs the head model from.
				Vector3 neck = p.GetEye() + vrPose->headOffset + headDown * 0.2F;
				torso = LeanTorsoToNeck(torso, neck, inp.crouch ? 0.5F : 0.7F);
			}

			Matrix4 const head = torso
				* Matrix4::Translate(0.0F, 0.0F, -headPosZ)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), pitch);

			Matrix4 const arms = torso
				* Matrix4::Translate(0.0F, 0.0F, armsPosZ)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), armPitch);

			// Legs
			{
				model = inp.crouch
					? renderer.RegisterModel(ModelPart(modelPath, "LegCrouch").c_str())
					: renderer.RegisterModel(ModelPart(modelPath, "Leg").c_str());

				param.matrix = legLeft * scaler;
				renderer.RenderModel(*model, param);

				model = inp.crouch
					? renderer.RegisterModel(ModelPart(modelPath, "LegRightCrouch").c_str())
					: renderer.RegisterModel(ModelPart(modelPath, "LegRight").c_str());

				param.matrix = legRight * scaler;
				renderer.RenderModel(*model, param);
			}

			// Torso
			{
				model = inp.crouch
					? renderer.RegisterModel(ModelPart(modelPath, "TorsoCrouch").c_str())
					: renderer.RegisterModel(ModelPart(modelPath, "Torso").c_str());

				param.matrix = torso * scaler;
				renderer.RenderModel(*model, param);
			}

			if (vrPose) {
				// Head, arms and tool from the pose, placed relative to the eye the
				// way the sender measured them.
				Vector3 eye = p.GetEye();
				auto axesOf = [](const VRQuat& q, Vector3 out[3]) {
					out[0] = q.Rotate(MakeVector3(1, 0, 0));
					out[1] = q.Rotate(MakeVector3(0, 1, 0));
					out[2] = q.Rotate(MakeVector3(0, 0, 1));
				};
				Vector3 headAxes[3];
				axesOf(vrPose->headRotation, headAxes);
				bool handValid[2];
				Vector3 handPos[2];
				Vector3 handAxes[2][3];
				for (int h = 0; h < 2; h++) {
					handValid[h] = vrPose->HasHand(h);
					handPos[h] = eye + vrPose->handOffset[h];
					axesOf(vrPose->handRotation[h], handAxes[h]);
				}
				int toolHand = (vrPose->flags & VRPoseToolInLeftHand) ? VRHandLeft : VRHandRight;
				AddVRBody(eye + vrPose->headOffset, headAxes, handValid, handPos, handAxes, toolHand,
				          true, true, param);
			} else {
				// Arms
				{
					model = renderer.RegisterModel(ModelPart(modelPath, "Arms").c_str());

					param.matrix = arms * scaler;
					renderer.RenderModel(*model, param);
				}

				// Head
				{
					model = client.isChristmasOn
						? renderer.RegisterModel("Models/Player/XmasHat.kv6")
						: renderer.RegisterModel(ModelPart(modelPath, "Head").c_str());

					param.matrix = head * scaler;
					renderer.RenderModel(*model, param);
				}

				// Headlamp, shown while the flashlight is lit
				if (p.IsFlashlightOn()) {
					model = renderer.RegisterModel("Models/Player/Headlamp.kv6");

					param.matrix = head * HeadlampModelMatrix();
					renderer.RenderModel(*model, param);
				}

				// Tool
				{
					ScriptIThirdPersonToolSkin interface(curSkin);
					interface.SetOriginMatrix(arms);
				}
				{
					ScriptIToolSkin interface(curSkin);
					interface.AddToScene();
				}

				hasValidOriginMatrix = true;
			}

			// draw intel in ctf
			stmp::optional<IGameMode&> mode = world->GetMode();
			if (mode && mode->ModeType() == IGameMode::m_CTF) {
				auto& ctf = static_cast<CTFGameMode&>(mode.value());
				if (ctf.PlayerHasIntel(p)) {
					model = client.isChristmasOn
						? renderer.RegisterModel("Models/MapObjects/XmasGift.kv6")
						: renderer.RegisterModel("Models/MapObjects/Intel.kv6");
					param.customColor = ConvertColorRGB(world->GetTeamColor(1 - p.GetTeamId()));
					Matrix4 const briefcase = torso
						* (inp.crouch ? Matrix4::Translate(0, 0.8F, 0.4F)
							* Matrix4::Rotate(MakeVector3(1, 0, 0), -0.5F)
						: Matrix4::Translate(0, 0.35F, 0.7F));
					param.matrix = briefcase * scaler;
					renderer.RenderModel(*model, param);
				}
			}

			// Emit at the eye, as the first-person view does, so the beam doesn't
			// shift when the observer changes camera mode.
			Vector3 const lightOrigin = p.GetEye();
			if (!IsLampBuried(lightOrigin))
				AddFlashlightToScene(lightOrigin, false);

			// The lamp itself is seen on the front of the headlamp as drawn, though.
			UpdateFlashlightGlare(GetHeadlampLens(head));

			// third person player rendering, done
		}

		bool ClientPlayer::IsVRHandsView() {
			return player.IsLocalPlayer() && player.IsAlive() && client.IsVRActive() &&
			       !ShouldRenderInThirdPersonView();
		}

		Matrix4 ClientPlayer::VRToolOriginMatrix(const Vector3& handPos, const Vector3 axes[3]) {
			// The third-person skins draw in the frame of the arms: +x right, -y
			// forward, +z down, with the tool sat in front of the right shoulder at
			// (0.4, -0.9, 0). `grip` is where a hand closes on each tool in that
			// frame, read off the voxel models: behind the magazine on the guns, at
			// the end of the handle on the spade, under the block and the grenade.
			Vector3 grip = MakeVector3(0.4F, -0.9F, 0.0F);
			Matrix4 turn = Matrix4::Identity();
			switch (currentTool) {
				case Player::ToolWeapon:
					switch (player.GetWeaponType()) {
						case RIFLE_WEAPON: grip = MakeVector3(0.4F, -0.49F, 0.03F); break;
						case SMG_WEAPON: grip = MakeVector3(0.4F, -0.42F, 0.03F); break;
						case SHOTGUN_WEAPON: grip = MakeVector3(0.4F, -0.42F, -0.03F); break;
					}
					break;
				case Player::ToolSpade:
					// The skin holds the spade upright with the blade up the frame's -z.
					// In a hand it points forward, so -z is turned onto -y.
					grip = MakeVector3(0.4F, -0.9F, 0.6F);
					turn = Matrix4::FromAxis(MakeVector3(1, 0, 0), MakeVector3(0, 0, -1),
					                         MakeVector3(0, 1, 0), MakeVector3(0, 0, 0));
					break;
				case Player::ToolBlock: grip = MakeVector3(0.4F, -0.7F, 0.0F); break;
				case Player::ToolGrenade: grip = MakeVector3(0.4F, -0.9F, 0.05F); break;
			}
			Matrix4 hand = Matrix4::FromAxis(axes[1], -axes[0], axes[2], handPos);

			// A spade in a hand does not animate by itself: the hand is the
			// animation. A little swing on top, about the hand's right (the
			// frame's x), shows a hit or a dig landing. A negative angle tips
			// the blade (forward, the frame's -y) down.
			if (currentTool == Player::ToolSpade) {
				Player& p = player;
				float swing = 0.0F;
				const float hitDelay = p.GetToolPrimaryDelay(Player::ToolSpade);
				const float nextHit = p.GetTimeToNextSpade();
				if (hitDelay > 0.0F && nextHit > 0.0F) {
					// A chop: down fast in the first quarter, back up in the rest.
					float t = 1.0F - Clamp(nextHit / hitDelay, 0.0F, 1.0F);
					float k = t < 0.25F ? t / 0.25F : 1.0F - (t - 0.25F) / 0.75F;
					k = k * k * (3.0F - 2.0F * k);
					swing = DEG2RAD(35.0F) * k;
				} else if (p.GetWeaponInput().secondary) {
					// Digging: the blade leans in as the dig builds, and comes
					// back as it lands.
					const float digDelay = p.GetToolSecondaryDelay(Player::ToolSpade);
					const float nextDig = p.GetTimeToNextDig();
					if (digDelay > 0.0F && nextDig > 0.0F)
						swing = DEG2RAD(25.0F) * (1.0F - Clamp(nextDig / digDelay, 0.0F, 1.0F));
				}
				if (swing > 0.0F)
					hand = hand * Matrix4::Rotate(MakeVector3(1, 0, 0), -swing);
			} else if (currentTool == Player::ToolWeapon) {
				// The kick in the hand: the gun jolts back and its muzzle flicks up,
				// and it settles in the first half of the time to the next shot. The
				// aim's own recoil (VRRecoil, which the shots follow) is already in
				// the hand's axes; this is the jolt on top, so it is smaller than
				// it was when it was all the recoil there was.
				Weapon& w = player.GetWeapon();
				const float delay = w.GetDelay();
				const float next = w.GetTimeToNextFire();
				if (delay > 0.0F && next > 0.0F) {
					float k = Clamp((next / delay - 0.5F) * 2.0F, 0.0F, 1.0F);
					k *= k;
					float kick = 4.0F; // degrees, the rifle
					switch (w.GetWeaponType()) {
						case SMG_WEAPON: kick = 1.5F; break;
						case SHOTGUN_WEAPON: kick = 6.0F; break;
						default: break;
					}
					// +y is back, and a positive turn about +x lifts the muzzle (-y)
					// towards up (-z).
					hand = hand * Matrix4::Translate(0.0F, 0.06F * k, 0.0F) *
					       Matrix4::Rotate(MakeVector3(1, 0, 0), DEG2RAD(kick) * k);
				}
			}

			return hand * turn * Matrix4::Translate(-grip);
		}

		void ClientPlayer::AddVRBody(const Vector3& headPos, const Vector3 headAxes[3],
		                             const bool handValid[2], const Vector3 handPos[2],
		                             const Vector3 handAxes[2][3], int toolHand, bool drawHead,
		                             bool drawUpperArms, const ModelRenderParam& baseParam) {
			IRenderer& renderer = client.GetRenderer();
			std::string modelPath = "Models/Player/";
			if (!cg_classicPlayerModels)
				modelPath += player.GetWeapon().GetName() + "/";
			ModelRenderParam param = baseParam;

			// The body's heading is the head's, levelled.
			Vector3 fwd = headAxes[0];
			fwd.z = 0.0F;
			fwd = fwd.GetLength() > 0.01F ? fwd.Normalize() : player.GetFront2D();
			const Vector3 right = MakeVector3(-fwd.y, fwd.x, 0.0F);
			const Vector3 down = MakeVector3(0.0F, 0.0F, 1.0F);

			if (drawHead) {
				Handle<IModel> head = client.isChristmasOn
				                        ? renderer.RegisterModel("Models/Player/XmasHat.kv6")
				                        : renderer.RegisterModel(ModelPart(modelPath, "Head").c_str());
				// The head model hangs off the neck, 0.2 below the eyes, the same as
				// the ordinary animation puts it.
				Vector3 neck = headPos + headAxes[2] * 0.2F;
				const Matrix4 headFrame =
				  Matrix4::FromAxis(-headAxes[1], headAxes[0], headAxes[2], neck);
				param.matrix = headFrame * Matrix4::Scale(0.1F);
				renderer.RenderModel(*head, param);

				// The headlamp sits on the tracked head as it does on an animated one.
				if (player.IsFlashlightOn()) {
					Handle<IModel> lamp = renderer.RegisterModel("Models/Player/Headlamp.kv6");
					param.matrix = headFrame * HeadlampModelMatrix();
					renderer.RenderModel(*lamp, param);
				}
			}

			int armMode = drawUpperArms ? 2 : (int)vr_showArms;
			if (armMode > 0) {
				Handle<IModel> foreModel = renderer.RegisterModel(ModelPart(modelPath, "Arm").c_str());
				Handle<IModel> upperModel =
				  renderer.RegisterModel(ModelPart(modelPath, "UpperArm").c_str());
				const float upperLen = 0.55F, foreLen = 0.55F;

				// The arm models run up +z from their origin, as in the first-person
				// view; the left one is the right one mirrored.
				auto armMatrix = [](const Vector3& from, const Vector3& to, bool leftArm) {
					Vector3 a2 = to - from;
					float len = a2.GetLength();
					a2 = len > 1.0E-4F ? a2 / len : MakeVector3(0, 0, 1);
					Vector3 a1 = Vector3::Cross(a2, MakeVector3(0, 0, 1));
					if (a1.GetLength() < 1.0E-3F)
						a1 = Vector3::Cross(a2, MakeVector3(1, 0, 0));
					a1 = a1.Normalize();
					Vector3 a0 = Vector3::Cross(a1, a2).Normalize();
					Matrix4 m = Matrix4::FromAxis(a0, a1, a2, from) * Matrix4::Scale(0.05F);
					if (leftArm)
						m = m * Matrix4::Translate(0, -1, 0) * Matrix4::Scale(1, -1, 1);
					return m;
				};

				for (int h = 0; h < 2; h++) {
					if (!handValid[h])
						continue;
					bool left = h == VRHandLeft;
					Vector3 wrist = handPos[h] - handAxes[h][0] * 0.08F;
					Vector3 elbow;
					Vector3 shoulder =
					  headPos + down * 0.55F + right * (left ? -0.33F : 0.33F) - fwd * 0.08F;

					if (armMode >= 2) {
						// Two bones from a shoulder beside the head, the elbow bent
						// down and out.
						Vector3 sw = wrist - shoulder;
						float d = sw.GetLength();
						Vector3 dir = d > 1.0E-4F ? sw / d : fwd;
						Vector3 bend = down + right * (left ? -0.6F : 0.6F) - fwd * 0.2F;
						bend = bend - dir * Vector3::Dot(bend, dir);
						bend = bend.GetLength() > 1.0E-4F ? bend.Normalize() : down;
						float reach = std::max(0.05F, std::min(d, upperLen + foreLen - 1.0E-3F));
						float x = (upperLen * upperLen - foreLen * foreLen + reach * reach) /
						          (2.0F * reach);
						float y = std::sqrt(std::max(upperLen * upperLen - x * x, 0.0F));
						elbow = shoulder + dir * x + bend * y;
					} else {
						// Forearm only: it runs back from the controller the way a real
						// forearm does, which reads better than a guessed shoulder.
						Vector3 back = (handAxes[h][0] * -0.9F + handAxes[h][2] * 0.35F).Normalize();
						elbow = wrist + back * foreLen;
					}

					param.matrix = armMatrix(elbow, wrist, left);
					renderer.RenderModel(*foreModel, param);
					if (armMode >= 2) {
						param.matrix = armMatrix(shoulder, elbow, left);
						renderer.RenderModel(*upperModel, param);
					}
				}
			}

			if (toolHand >= 0 && toolHand < 2 && handValid[toolHand]) {
				asIScriptObject* skin = GetCurrentSkin(false);
				SetSkinParameterForTool(currentTool, skin);
				SetCommonSkinParameter(skin);
				{
					ScriptIToolSkin interface(skin);
					interface.SetRaiseState(1.0F);
				}
				// A physical reload's magazine is out: the rifle and SMG skins draw
				// the gun without it for a reload progress below zero, which the
				// game itself never gives.
				if (player.IsLocalPlayer() && currentTool == Player::ToolWeapon &&
				    client.vrRig.reloadStage != 0) {
					ScriptIWeaponSkin w(skin);
					if (w.ImplementsInterface()) {
						w.SetReloading(true);
						w.SetReloadProgress(-1.0F);
					}
				}
				{
					ScriptIThirdPersonToolSkin interface(skin);
					interface.SetOriginMatrix(VRToolOriginMatrix(handPos[toolHand], handAxes[toolHand]));
				}
				// Your own gun is the first-person one, sights and all, where its skin
				// can draw it (IVRWeaponSkin); everybody else's is the usual one.
				ScriptIVRWeaponSkin vrSkin(skin);
				const bool ownGun = player.IsLocalPlayer() && currentTool == Player::ToolWeapon &&
				                    vrSkin.ImplementsInterface();
				if (ownGun)
					vrSkin.SetVRHand(true);
				// A skin of your gun that does not give its sights (a mod's, made
				// before VR): the models it draws, to find them from
				const bool fitSights = player.IsLocalPlayer() &&
				                       currentTool == Player::ToolWeapon &&
				                       !vrSkin.ImplementsInterface();
				std::vector<std::pair<std::string, Matrix4>> drawn;
				if (fitSights)
					sandboxedRenderer->SetModelCapture(&drawn);
				{
					ScriptIToolSkin interface(skin);
					interface.AddToScene();
				}
				if (fitSights) {
					sandboxedRenderer->SetModelCapture(nullptr);
					FitVRSights(drawn, VRToolOriginMatrix(handPos[toolHand], handAxes[toolHand]));
				}
				if (ownGun)
					vrSkin.SetVRHand(false);
				hasValidOriginMatrix = true;
			}

			// The new magazine (a shell for the shotgun), in the off hand.
			const int offHand = 1 - toolHand;
			if (player.IsLocalPlayer() && client.vrRig.reloadStage == 2 && offHand >= 0 &&
			    offHand < 2 && handValid[offHand]) {
				const char* path = "Models/Weapons/Rifle/Magazine.kv6";
				switch (player.GetWeaponType()) {
					case SMG_WEAPON: path = "Models/Weapons/SMG/Magazine.kv6"; break;
					case SHOTGUN_WEAPON: path = "Models/Weapons/Shotgun/Casing.kv6"; break;
					default: break;
				}
				Handle<IModel> mag = renderer.RegisterModel(path);
				const Vector3* ax = handAxes[offHand];
				param.matrix = Matrix4::FromAxis(ax[1], -ax[0], ax[2], handPos[offHand]) *
				               Matrix4::Scale(0.05F) * Matrix4::Scale(-1, -1, 1);
				renderer.RenderModel(*mag, param);
			}
		}

		void ClientPlayer::AddToSceneVRHands() {
			Player& p = player;
			IRenderer& renderer = client.GetRenderer();
			const Vector3 eye = p.GetEye();

			// Real depth, not the flat view's depth hack: in stereo a gun drawn on
			// top of the wall it is poking into reads as being in two places.
			sandboxedRenderer->SetClipBox(AABB3(eye - Vector3(6.0F, 6.0F, 6.0F),
			                                    eye + Vector3(6.0F, 6.0F, 6.0F)));
			sandboxedRenderer->SetAllowDepthHack(false);
			sandboxedRenderer->SetPlayerXRay(false, MakeVector3(1, 1, 1));

			bool valid[2];
			Vector3 pos[2];
			Vector3 axes[2][3];
			for (int h = 0; h < 2; h++)
				valid[h] = client.GetLocalVRHand(h, pos[h], axes[h]);
			const int toolHand = VRToolHand();

			// The rifle's scope: a virtual one, seen while an eye looks down the
			// rifle's iron sights (the line its skin gives, IVRWeaponSkin). Its view
			// is rendered from the front sight, along that line, and drawn into each
			// eye at the rear sight (Client::DrawVRScopeLens). The view has the
			// head's up for its up, so the world in the lens stands level with the
			// world around it however the gun is rolled. (Until 0.3 a voxel tube
			// stood on the receiver, placed for the stock rifle: on any other skin
			// it sat wrong, and it stayed when the skin changed. A skin without the
			// sights' line gets the tube's old line, with nothing drawn.)
			// Set again below while an eye is behind the scope; read by the laser
			// below and by the next shot. (Worked out after the laser, it drew last
			// frame's line, and through the scope trailed the crosshair: testers.)
			client.vrRig.scopeAimValid = false;
			client.vrRig.reflexActive = false;
			const VRGunSights sights = valid[toolHand] && currentTool == Player::ToolWeapon
			                             ? GetVRGunSights(pos[toolHand], axes[toolHand])
			                             : VRGunSights();
			// A reflex sight (cg_reflexScope) in place of the iron sights: its dot,
			// where each eye's line along the barrel crosses its window, so it sits
			// at infinity (Client::DrawVRReflexDot). The rifle's scope gives way to it.
			if (sights.valid && sights.reflexRadius > 0.0F) {
				const Vector3 along = sights.front - sights.rear;
				if (along.GetLength() > 1.0E-3F) {
					client.vrRig.reflexActive = true;
					client.vrRig.reflexCenter = sights.reflexCenter;
					client.vrRig.reflexDir = along.Normalize();
					client.vrRig.reflexRadius = sights.reflexRadius;
				}
			}
			// The tool in the hand (currentTool), not the one chosen: the hand
			// changes its tool only once the old one is put away, and the scope
			// went with the choice, a moment before the rifle came or after it went.
			const bool debugScope = (int)vr_debugScope != 0;
			if ((int)vr_scope &&
			    (debugScope || (valid[toolHand] && currentTool == Player::ToolWeapon &&
			                    p.GetWeaponType() == RIFLE_WEAPON)) &&
			    !client.vrRig.reflexActive) {
				// The lens's size: the old tube's 4.2 voxels of 0.025.
				const float voxel = 0.025F;
				Vector3 eyepiece, objective;
				if (debugScope) {
					static bool logged = false;
					if (!logged)
						SPLog("[VR] vr_debugScope: the scope held up to the eye");
					logged = true;
					eyepiece = client.vrRig.camera + client.vrRig.headAxes[0] * 0.15F;
					objective = eyepiece + client.vrRig.headAxes[0] * 0.3F;
				} else if (sights.valid) {
					eyepiece = sights.rear;
					objective = sights.front;
				} else {
					// The old tube's line, in the gun's own frame (recoil included):
					// 0.6 ahead of the grip to 0.05 behind it, 0.33 over it.
					const Matrix4 hand = VRToolOriginMatrix(pos[toolHand], axes[toolHand]) *
					                     Matrix4::Translate(0.4F, -0.49F, 0.03F);
					const float receiverTop = -0.03F - 4.0F * 0.051F + 0.004F;
					const Matrix4 tube = hand *
					                     Matrix4::Translate(-4.0F * voxel, -0.6F, receiverTop - 9 * voxel) *
					                     Matrix4::Scale(voxel);
					eyepiece = (tube * MakeVector3(4.0F, 22.0F, 4.0F)).GetXYZ();
					objective = (tube * MakeVector3(4.0F, 0.0F, 4.0F)).GetXYZ();
				}
				Vector3 f = objective - eyepiece;
				const Vector3 toLens = eyepiece - client.vrRig.camera;
				if (f.GetLength() > 1.0E-3F) {
					f = f.Normalize();
					const float along = Vector3::Dot(toLens, f);
					const float off = (toLens - f * along).GetLength();
					if (along > 0.0F && along < 0.5F && off < 0.1F) {
						Vector3 d = client.vrRig.headAxes[2];
						d = d - f * Vector3::Dot(d, f);
						if (d.GetLength() > 1.0E-3F) {
							d = d.Normalize();
							const Vector3 right = Vector3::Cross(d, f).Normalize();
							// Magnified as a monitor player's scoped rifle is, and never
							// more: GetAimDownZoomScale's 2.4 at full aim, over a field at
							// most a monitor's zoomed one (cg_fov's 68 / 2.4: 15.6 degrees
							// each side). A fixed 6 degree picture in the lens magnified by
							// however close the eye was, 4x at 0.3 blocks and over 10x at
							// 0.1. The lens is drawn wider than the tube now (testers asked
							// for a bigger scope), and its rim is a fisheye: the centre at
							// 2.4x, the edge squeezed to show up to 60% more round the
							// target, within that 15.6 (DrawVRScopeLens). (This is
							// 1d5eff8's scope, put back on 2026-10-12 after a stronger
							// rim and a cap for the eye close to it were tried and
							// liked less; see VR.md.)
							const float lensRadius = 4.2F * voxel;
							const float apparent =
							  std::atan(lensRadius / std::max(0.02F, toLens.GetLength()));
							const float centreHalf = apparent / 2.4F;
							const float edgeHalf =
							  std::max(centreHalf, std::min(DEG2RAD(15.6F), centreHalf * 1.6F));
							// A lens pushed through a wall shows black: its view was the
							// far side's (a tester's report), and its aim is not used.
							const bool lensSeen =
							  client.VRLineOfSight(client.vrRig.camera, objective + f * 0.02F);
							if (lensSeen)
								renderer.SetVRScopeView(objective + f * 0.02F, right, -d, f,
								                        2.0F * edgeHalf);
							client.vrRig.scopeCentreHalf = centreHalf;
							client.vrRig.scopeEdgeHalf = edgeHalf;
							client.vrRig.scopeActive = true;
							client.vrRig.scopeBlocked = !lensSeen;
							client.vrRig.scopeAimValid = lensSeen;
							client.vrRig.scopeAimOrigin = objective;
							client.vrRig.scopeAimDir = f;
							client.vrRig.scopeLensPos = eyepiece + f * 0.005F;
							client.vrRig.scopeLensRight = right;
							client.vrRig.scopeLensRadius = lensRadius; // past the bell
							const Matrix4 gunFrame = VRToolOriginMatrix(pos[toolHand], axes[toolHand]);
							client.vrRig.scopeGunRight = gunFrame.GetAxis(0).Normalize();
							client.vrRig.scopeGunUp = (gunFrame.GetAxis(2) * -1.0F).Normalize();
						}
					}
				}
			}

			// The laser: along the ray the tool acts on (not the hand's pose, and
			// not the gun's barrel: the shot goes where the ray goes), to the first
			// thing in the map it meets. Not for grenades, which leave with the
			// hand's swing rather than along a ray.
			// Not while the hand is changing tools: the laser is the chosen tool's,
			// the hand still holds the old one.
			// No laser from a barrel through a wall (Client::VRGunShot).
			const bool gunThroughWall =
			  p.IsToolWeapon() && valid[toolHand] &&
			  !client.VRLineOfSight(p.GetEye(), VRGunLaserOrigin(pos[toolHand], axes[toolHand]));
			if ((int)vr_laser && valid[toolHand] && currentTool == p.GetTool() && !gunThroughWall &&
			    (p.IsToolWeapon() || p.IsToolSpade() || p.IsToolBlock())) {
				// A gun's shot leaves the eye (Client::VRGunAimPoint): its laser
				// is drawn from the gun to where that shot lands.
				// Worked out here, from the hand the gun is drawn in this frame:
				// kept from the game's update, it trailed the gun by however far
				// the player moved in between.
				const bool gunShot = p.IsToolWeapon() && client.vrRig.gunLaser;
				Vector3 from = p.GetToolOrigin();
				Vector3 dir = p.GetToolDirection();
				Vector3 gunImpact;
				bool gunHit = false;
				if (gunShot) {
					client.VRGunShot(p, gunImpact, gunHit);
					// From the top of the barrel, in the gun's own frame (the one the
					// scope is mounted in: x right, y back, z down, from the grip),
					// so it comes out of the gun and kicks with it. It started at the
					// controller's aim point, under the gun.
					from = VRGunLaserOrigin(pos[toolHand], axes[toolHand]);
					Vector3 d = gunImpact - from;
					if (d.GetLength() > 1.0E-3F)
						dir = d.Normalize();
				}
				// Guns reach as far as can be seen. The spade and blocks draw a short
				// laser, and their dot turns green when the block it is on is in
				// reach, by the measure the game uses for that tool.
				const bool gun = p.IsToolWeapon();
				const float reach = gun ? 128.0F : 6.0F;
				float length = reach;
				bool hit = false, inReach = gun;
				World* world = client.GetWorld();
				if (gunShot) {
					length = std::min(reach, (gunImpact - from).GetLength());
					hit = gunHit;
				} else if (world && world->GetMap()) {
					GameMap::RayCastResult r = world->GetMap()->CastRay2(from, dir, 256);
					if (r.hit && !r.startSolid) {
						float d = (r.hitPos - from).GetLength();
						if (d < reach) {
							length = d;
							hit = true;
							// By the game's own measures, from the eye: building's
							// InBuildRange for blocks, UseSpade's for the spade. From
							// the hand, the dot went green where nothing could be built.
							if (p.IsToolBlock())
								inReach = p.InBuildRange(r.hitBlock + r.normal);
							else if (!gun)
								inReach = (MakeVector3(r.hitBlock) + 0.5F - p.GetEye())
								            .GetChebyshevLength() <
								          static_cast<float>(MAX_DIG_DISTANCE);
						}
					}
				}
				// Pointing at the wrist menu: it ends there.
				if (client.vrRig.wristPointing) {
					length = std::min(length, client.vrRig.wristPointDistance * client.vrRig.scale);
					hit = false;
				}
				// It starts clear of the hand, where the gun's body is (a gun's at
				// its barrel already).
				const float start = gunShot ? 0.0F : std::min(0.3F, length);
				const Vector3 a = from + dir * start, b = from + dir * length;
				Handle<IImage> glow = renderer.RegisterImage("Gfx/Ball.png");
				Handle<IImage> solid = renderer.RegisterImage("Gfx/White.tga");
				// A solid core, so it shows against a bright sky (a glow alone is
				// additive, and there is nothing to add to white), inside a wider
				// additive glow. Premultiplied colours: alpha 0 is additive.
				// Amber while the gun reloads, so it shows when it is ready (a buzz
				// in the hand says so too).
				const bool reloading = gun && p.GetWeapon().IsReloading();
				const Vector3 beam = reloading ? MakeVector3(0.9F, 0.45F, 0.02F)
				                               : MakeVector3(0.85F, 0.05F, 0.03F);
				renderer.SetColorAlphaPremultiplied(MakeVector4(beam.x, beam.y, beam.z, 0.85F));
				renderer.AddLongSprite(*solid, a, b, 0.004F);
				renderer.SetColorAlphaPremultiplied(
				  MakeVector4(beam.x * 0.95F, beam.y * 1.2F, beam.z, 0.0F));
				renderer.AddLongSprite(*glow, a, b, 0.018F);
				if (hit) {
					// Sized with distance, so the dot stays a dot far away.
					const Vector3 at = from + dir * (length - 0.02F);
					const float seen = (at - client.vrRig.camera).GetLength();
					const float radius = 0.03F + 0.004F * seen;
					const Vector4 tint = inReach && !gun ? MakeVector4(0.2F, 1.0F, 0.2F, 1.0F)
					                     : reloading     ? MakeVector4(1.0F, 0.55F, 0.05F, 1.0F)
					                                     : MakeVector4(1.0F, 0.1F, 0.06F, 1.0F);
					// Solid centre, glow round it.
					renderer.SetColorAlphaPremultiplied(tint * 0.9F);
					renderer.AddSprite(*glow, at, radius * 0.5F, 0.0F);
					renderer.SetColorAlphaPremultiplied(MakeVector4(tint.x, tint.y, tint.z, 0.0F));
					renderer.AddSprite(*glow, at, radius, 0.0F);
				}
			}

			// The headlamp, as everybody else sees it: on the forehead, a little
			// above the eyes (the head's axis 2 points down), along the facing the
			// server has, which in a headset is the head's.
			AddFlashlightToScene(client.vrRig.camera - client.vrRig.headAxes[2] * 0.08F, true);

			ModelRenderParam param;
			param.customColor = ConvertColorRGB(p.GetColor());
			const bool body = (int)vr_showBody != 0;
			if (body)
				AddVRLocalBody(param);
			// With a body, the arms run from its shoulders.
			AddVRBody(client.vrRig.camera, client.vrRig.headAxes, valid, pos, axes, toolHand, false,
			          body, param);
		}

		void ClientPlayer::AddVRLocalBody(const ModelRenderParam& baseParam) {
			// The body other players see you with, from your own eyes: legs where
			// the body stands, the torso leaning to under the head. It sits a little
			// behind the head, as a real one does, so looking down shows your chest
			// and not the inside of your neck; the head itself is not drawn.
			Player& p = player;
			IRenderer& renderer = client.GetRenderer();
			std::string modelPath = "Models/Player/";
			if (!cg_classicPlayerModels)
				modelPath += p.GetWeapon().GetName() + "/";
			ModelRenderParam param = baseParam;

			// Looking down from a height the body hid what was below (a tester's
			// report): it fades as the head tips past 40 degrees down, and is gone
			// by 65. (The head's forward has z down.)
			{
				const float down = std::asin(Clamp(client.vrRig.headAxes[0].z, -1.0F, 1.0F));
				const float fade = Clamp((down - DEG2RAD(40.0F)) / DEG2RAD(25.0F), 0.0F, 1.0F);
				if (fade > 0.95F)
					return;
				if (fade > 0.0F) {
					param.ghost = true;
					param.opacity = 1.0F - fade;
				}
			}

			// Facing where the head faces, levelled.
			Vector3 fwd = client.vrRig.headAxes[0];
			fwd.z = 0.0F;
			fwd = fwd.GetLength() > 0.01F ? fwd.Normalize() : p.GetFront2D();
			const Vector3 behind = fwd * -0.15F;
			const float yaw = atan2f(fwd.y, fwd.x) + kHalfPi;

			const PlayerInput inp = p.GetInput();
			const Matrix4 lower = Matrix4::Translate(p.GetOrigin() + behind) *
			                      Matrix4::Rotate(MakeVector3(0, 0, 1), yaw);
			const Matrix4 scaler = Matrix4::Scale(0.1F) * Matrix4::Scale(-1, -1, 1);

			// Legs, walking as the third-person ones do.
			{
				const float legsPosX = 0.25F;
				const float legsPosY = inp.crouch ? 0.25F : 0.0F;
				const float legsPosZ = inp.crouch ? 0.05F : 0.1F;
				Vector3 v = p.GetVelocity();
				Vector2 legsRot;
				legsRot.x = Vector3::Dot(v, fwd);
				legsRot.y = Vector3::Dot(v, MakeVector3(-fwd.y, fwd.x, 0.0F));
				legsRot *= sinf(p.GetWalkAnimationProgress() * kTwoPi) * 3.0F;

				const Matrix4 legLeft = lower * Matrix4::Translate(-legsPosX, legsPosY, -legsPosZ) *
				                        Matrix4::Rotate(MakeVector3(1, 0, 0), -legsRot.x) *
				                        Matrix4::Rotate(MakeVector3(0, 1, 0), -legsRot.y);
				const Matrix4 legRight = lower * Matrix4::Translate(legsPosX, legsPosY, -legsPosZ) *
				                         Matrix4::Rotate(MakeVector3(1, 0, 0), legsRot.x) *
				                         Matrix4::Rotate(MakeVector3(0, 1, 0), legsRot.y);

				Handle<IModel> model =
				  renderer.RegisterModel(ModelPart(modelPath, inp.crouch ? "LegCrouch" : "Leg").c_str());
				param.matrix = legLeft * scaler;
				renderer.RenderModel(*model, param);
				model = renderer.RegisterModel(
				  (modelPath + (inp.crouch ? "LegRightCrouch.kv6" : "LegRight.kv6")).c_str());
				param.matrix = legRight * scaler;
				renderer.RenderModel(*model, param);
			}

			// Torso, leaning to under the head (as remote VR players' do).
			{
				const float torsoPosZ = inp.crouch ? 0.5F : 1.0F;
				Matrix4 torso = lower * Matrix4::Translate(0.0F, 0.0F, -torsoPosZ);
				const Vector3 neck = client.vrRig.camera + behind + client.vrRig.headAxes[2] * 0.2F;
				torso = LeanTorsoToNeck(torso, neck, inp.crouch ? 0.5F : 0.7F);
				Handle<IModel> model = renderer.RegisterModel(
				  (modelPath + (inp.crouch ? "TorsoCrouch.kv6" : "Torso.kv6")).c_str());
				param.matrix = torso * scaler;
				renderer.RenderModel(*model, param);
			}
		}

		void ClientPlayer::AddToScene() {
			SPADES_MARK_FUNCTION();

			Player& p = player;

			hasValidOriginMatrix = false;

			// Only the third-person path, which draws the lamp, gives it a glare.
			flashlightGlare.Clear();

			if (p.IsSpectator())
				return; // spectator

			// Do not draw a player with an invalid state
			if (p.GetFront().GetSquaredLength() < 0.01F)
				return;

			// distance cull
			const auto& viewOrigin = client.GetLastSceneDef().viewOrigin;
			float distSqr = (p.GetOrigin() - viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			bool isThirdPerson = ShouldRenderInThirdPersonView();
			if (IsVRHandsView())
				AddToSceneVRHands();
			else if (!isThirdPerson)
				AddToSceneFirstPersonView();
			else
				AddToSceneThirdPersonView();

			if (cg_debugToolSkinAnchors
				&& p.IsLocalPlayer() && p.IsAlive()
				&& currentTool == Player::ToolWeapon) {
				IRenderer& renderer = client.GetRenderer();

				auto drawAxes = [&renderer](Vector3 p) {
					renderer.AddDebugLine(Vector3{p.x - 0.2F, p.y, p.z},
										  Vector3{p.x + 0.2F, p.y, p.z},
										  Vector4{1.0F, 0.0F, 0.0F, 1.0F});
					renderer.AddDebugLine(Vector3{p.x, p.y - 0.2F, p.z},
										  Vector3{p.x, p.y + 0.2F, p.z},
										  Vector4{0.0F, 0.6F, 0.0F, 1.0F});
					renderer.AddDebugLine(Vector3{p.x, p.y, p.z - 0.2F},
										  Vector3{p.x, p.y, p.z + 0.2F},
										  Vector4{0.0F, 0.0F, 1.0F, 1.0F});
				};

				drawAxes(isThirdPerson ? GetMuzzlePosition() : GetMuzzlePositionInFirstPersonView());
				drawAxes(isThirdPerson ? GetCaseEjectPosition() : GetCaseEjectPositionInFirstPersonView());
			}
		}

		void ClientPlayer::Draw2D() {
			// A crosshair or scope on the HUD panel points where the head looks, not
			// where the gun does; in VR the sights are on the gun.
			if (IsVRHandsView())
				return;
			if (!ShouldRenderInThirdPersonView() && player.IsAlive()) {
				asIScriptObject* curSkin = GetCurrentSkin(true);

				// common process
				{
					ScriptIViewToolSkin interface(curSkin);
					interface.Draw2D();
				}
			}
		}

		bool ClientPlayer::ShouldRenderInThirdPersonView() {
			// The player from whom's perspective the game is
			return !client.IsPlayerInFirstPerson(player.GetId());
		}

		ClientPlayer::VRGunSights ClientPlayer::GetVRGunSights(const Vector3& handPos,
		                                                       const Vector3 axes[3]) {
			VRGunSights sights;
			ScriptIVRWeaponSkin vr(weaponSkin);
			if (currentTool != Player::ToolWeapon)
				return sights;
			if (!vr.ImplementsInterface()) {
				// Found from its models (FitVRSights), in the gun's frame
				if (fittedSights.valid && fittedSights.weapon == player.GetWeaponType()) {
					const Matrix4 gun = VRToolOriginMatrix(handPos, axes) * VRGunFrameOffset();
					sights.valid = true;
					sights.rear = (gun * fittedSights.rear).GetXYZ();
					sights.front = (gun * fittedSights.front).GetXYZ();
				}
				return sights;
			}
			// Where the gun is drawn this frame (AddVRBody gives it the same)
			ScriptIThirdPersonToolSkin(weaponSkin).SetOriginMatrix(VRToolOriginMatrix(handPos, axes));
			sights.valid = true;
			sights.rear = vr.GetVRSightRear();
			sights.front = vr.GetVRSightFront();
			sights.reflexCenter = vr.GetVRReflexCenter();
			sights.reflexRadius = vr.GetVRReflexRadius();
			return sights;
		}

		Matrix4 ClientPlayer::VRGunFrameOffset() { return Matrix4::Translate(0.4F, -0.49F, 0.03F); }

		void ClientPlayer::FitVRSights(const std::vector<std::pair<std::string, Matrix4>>& drawn,
		                               const Matrix4& toolOrigin) {
			// Once a skin, by the files it draws: where it draws them in the gun's
			// frame (x right, y back, z down, from the grip) is taken from the
			// frame it is first seen in. Not again as it moves (a skin's reload),
			// which would read its models every frame.
			std::string key = std::to_string(static_cast<int>(player.GetWeaponType()));
			for (const auto& d : drawn)
				key += "|" + d.first;
			if (key == fittedSights.key)
				return;
			const Matrix4 toGun = (toolOrigin * VRGunFrameOffset()).Inversed();
			std::vector<Matrix4> inGun;
			for (const auto& d : drawn)
				inGun.push_back(toGun * d.second);
			fittedSights = FittedSights();
			fittedSights.key = key;
			fittedSights.weapon = player.GetWeaponType();

			// Every voxel's centre, in the gun's frame
			std::vector<Vector3> points;
			for (size_t i = 0; i < drawn.size(); i++) {
				Handle<VoxelModel> vm;
				try {
					vm = VoxelModelLoader::Load(drawn[i].first.c_str());
				} catch (const std::exception& ex) {
					SPLog("[VR] Sights of a skin: %s not read: %s", drawn[i].first.c_str(),
					      ex.what());
					continue;
				}
				const Vector3 origin = vm->GetOrigin();
				for (int x = 0; x < vm->GetWidth(); x++)
					for (int y = 0; y < vm->GetHeight(); y++)
						for (int z = 0; z < vm->GetDepth(); z++)
							if (vm->IsSolid(x, y, z))
								points.push_back(
								  (inGun[i] * (MakeVector3(static_cast<float>(x),
								                           static_cast<float>(y),
								                           static_cast<float>(z)) +
								               origin))
								    .GetXYZ());
			}
			if (points.empty())
				return;

			// The sights are the gun's top, along the barrel: the line runs at
			// the top's height and, across, through the middle of what is up
			// there; from the back of it to the front (a scope's tube, or the
			// rear sight to the front one). The 6 cm band takes in a rear sight
			// lower than the front post.
			float top = points[0].z;
			for (const Vector3& p : points)
				top = std::min(top, p.z);
			std::vector<float> across;
			float back = -1.0E9F, ahead = 1.0E9F;
			for (const Vector3& p : points) {
				if (p.z > top + 0.06F)
					continue;
				across.push_back(p.x);
				back = std::max(back, p.y);
				ahead = std::min(ahead, p.y);
			}
			if (back - ahead < 0.05F)
				return;
			std::nth_element(across.begin(), across.begin() + across.size() / 2, across.end());
			const float x = across[across.size() / 2];
			const float z = top + 0.01F;
			fittedSights.valid = true;
			fittedSights.rear = MakeVector3(x, back, z);
			fittedSights.front = MakeVector3(x, ahead, z);
			SPLog("[VR] Sights of a skin with none of its own: found from %d model(s), "
			      "%.2f m long, %.2f m over the grip, %.2f m across",
			      static_cast<int>(drawn.size()), back - ahead, -z, x);
		}

		Vector3 ClientPlayer::VRGunLaserOrigin(const Vector3& handPos, const Vector3 axes[3]) {
			// In the gun's own frame (the one the scope is mounted in: x right, y
			// back, z down, from the grip), so it comes out of the gun and kicks
			// with it.
			const Matrix4 gunFrame =
			  VRToolOriginMatrix(handPos, axes) * Matrix4::Translate(0.4F, -0.49F, 0.03F);
			return (gunFrame * MakeVector3(0.0F, -kVRLaserAhead, kVRLaserUp)).GetXYZ();
		}

		Vector3 ClientPlayer::GetMuzzlePosition() {
			// In a headset the flash and the tracer leave where the laser does:
			// from the skin's muzzle they came out under it (a tester's report).
			if (IsVRHandsView() && player.IsToolWeapon()) {
				Vector3 handPos, handAxes[3];
				if (client.GetLocalVRHand(VRToolHand(), handPos, handAxes))
					return VRGunLaserOrigin(handPos, handAxes);
			}
			ScriptIWeaponSkin3 interface(weaponSkin);
			if (interface.ImplementsInterface()) {
				const Vector3 muzzle = interface.GetMuzzlePosition();

				// The skin should return a legit position. Return the default position if it didn't.
				const Vector3 origin = player.GetOrigin();
				AABB3 clip = AABB3(
					origin - Vector3(2.0F, 2.0F, 4.0F),
					origin + Vector3(2.0F, 2.0F, 2.0F)
				);
				if (clip.Contains(muzzle))
					return muzzle;
			}

			return (GetEyeMatrix() * MakeVector3(-0.13F, 1.5F, 0.2F)).GetXYZ();
		}

		Vector3 ClientPlayer::GetMuzzlePositionInFirstPersonView() {
			// In VR the gun in the hand is the third-person model.
			if (IsVRHandsView())
				return GetMuzzlePosition();
			if (cg_trueAimDownSight && cg_pngScope && aimDownState > 0.99F)
				return (GetEyeMatrix() * MakeVector3(0.0F, 0.5F, 0.0F)).GetXYZ();

			if (cg_classicViewWeapon) {
				const Vector3 origin = classicViewWeaponOrigin;

				Vector3 muzzle = MakeVector3(0.0F, 0.0F, 0.0F);
				switch (player.GetWeaponType()) {
					case RIFLE_WEAPON: muzzle = MakeVector3(0.0F, 0.55F, -0.075F); break;
					case SMG_WEAPON: muzzle = MakeVector3(0.0F, 0.25F, -0.075F); break;
					case SHOTGUN_WEAPON: muzzle = MakeVector3(0.0F, 0.4F, -0.075F); break;
				}

				return (GetEyeMatrix() * (origin + muzzle)).GetXYZ();
			}

			ScriptIWeaponSkin3 interface(weaponViewSkin);
			if (interface.ImplementsInterface())
				return (GetEyeMatrix() * interface.GetMuzzlePosition()).GetXYZ();

			return (GetEyeMatrix() * MakeVector3(-0.13F, 1.5F, 0.2F)).GetXYZ();
		}

		Vector3 ClientPlayer::GetCaseEjectPosition() {
			ScriptIWeaponSkin3 interface(weaponSkin);
			if (interface.ImplementsInterface()) {
				const Vector3 caseEject = interface.GetCaseEjectPosition();

				// The skin should return a legit position. Return the default position if it didn't.
				const Vector3 origin = player.GetOrigin();
				AABB3 clip = AABB3(
					origin - Vector3(2.0F, 2.0F, 4.0F),
					origin + Vector3(2.0F, 2.0F, 2.0F)
				);
				if (clip.Contains(caseEject))
					return caseEject;
			}

			return (GetEyeMatrix() * MakeVector3(-0.13F, 0.5F, 0.2F)).GetXYZ();
		}

		Vector3 ClientPlayer::GetCaseEjectPositionInFirstPersonView() {
			if (IsVRHandsView())
				return GetCaseEjectPosition();
			if (cg_classicViewWeapon) {
				const Vector3 origin = classicViewWeaponOrigin;

				Vector3 caseEject = MakeVector3(0.0F, 0.0F, 0.0F);
				switch (player.GetWeaponType()) {
					case RIFLE_WEAPON: caseEject = MakeVector3(0.0F, 0.125F, -0.1F); break;
					case SMG_WEAPON: caseEject = MakeVector3(0.0F, 0.2F, -0.1F); break;
					case SHOTGUN_WEAPON: caseEject = MakeVector3(0.0F, 0.0F, -0.1F); break;
				}

				return (GetEyeMatrix() * (origin + caseEject)).GetXYZ();
			}

			ScriptIWeaponSkin3 interface(weaponViewSkin);
			if (interface.ImplementsInterface())
				return (GetEyeMatrix() * interface.GetCaseEjectPosition()).GetXYZ();

			return (GetEyeMatrix() * MakeVector3(-0.13F, 0.5F, 0.2F)).GetXYZ();
		}

		struct ClientPlayer::AmbienceInfo {
			float room, size, distance;
		};

		ClientPlayer::AmbienceInfo ClientPlayer::ComputeAmbience() {
			const auto& viewOrigin = client.GetLastSceneDef().viewOrigin;
			const auto& rayFrom = player.GetEye();

			if (!cg_environmentalAudio) {
				AmbienceInfo result;
				result.room = 0.0F;
				result.distance = (viewOrigin - rayFrom).GetLength();
				result.size = 0.0F;
				return result;
			}

			float maxDistance = 40.0F;
			GameMap& map = *client.map;

			// uniformly distributed random unit vectors
			const Vector3 directions[24] = {
			  {-0.4806003057749437F, -0.42909622618705534F, 0.7647874049440525F},
			  {-0.32231294555647927F, 0.6282069816346844F, 0.7081457147735524F},
			  {0.048740582496498826F, -0.6670915238644523F, 0.7433796166200044F},
			  {0.4507022412112344F, 0.2196054264547812F, 0.8652403980621708F},
			  {-0.42721511627413183F, -0.587164590982542F, -0.6875499891085622F},
			  {-0.5570464880797501F, 0.3832470400156089F, -0.7367638131974799F},
			  {0.4379032819319448F, -0.5217172826725083F, -0.732155579528044F},
			  {0.5505793235065188F, 0.5884516130938041F, -0.5921039668625805F},
			  {0.681714179159347F, -0.6289005125058891F, -0.3738314102679548F},
			  {0.882424317058847F, 0.4680895178240496F, -0.047111866514457174F},
			  {0.8175844570742612F, -0.5123280060684333F, 0.26282250616819125F},
			  {0.7326555076593512F, 0.16938649523355995F, 0.6591844372623717F},
			  {-0.8833847855718798F, -0.46859333747646814F, -0.007183640636104698F},
			  {-0.6478926243769724F, 0.5325399055055595F, -0.5446433661783178F},
			  {-0.7011236289377749F, -0.4179353735633245F, 0.5777159167528706F},
			  {-0.8834742898471629F, 0.3226030059694268F, 0.3397064611080296F},
			  {-0.701272268659947F, 0.7126868112640804F, -0.017167243773185584F},
			  {-0.4048459451282839F, 0.8049148135357349F, 0.4338339586338529F},
			  {0.10511344475950758F, 0.7400485819463978F, -0.664288536774432F},
			  {0.4228172536676786F, 0.7759558485735245F, 0.46810051384874957F},
			  {-0.641642302739998F, -0.7293326298605313F, -0.23742171416118207F},
			  {-0.269582155924164F, -0.957885171758109F, 0.09890125850168793F},
			  {0.09274966874325204F, -0.9126579244190587F, -0.39806156803076687F},
			  {0.49359438685568013F, -0.721891173178783F, 0.48501310843226225F}
			};

			std::array<float, 24> distances;
			std::array<float, 24> feedbacknesses;

			std::fill(feedbacknesses.begin(), feedbacknesses.end(), 0.0F);

			for (std::size_t i = 0; i < distances.size(); ++i) {
				float& distance = distances[i];
				float& feedbackness = feedbacknesses[i];

				const Vector3& rayTo = directions[i];

				IntVector3 hitPos;
				if (map.CastRay(rayFrom, rayTo, maxDistance, hitPos)) {
					distance = (MakeVector3(hitPos) - rayFrom).GetLength();
					feedbackness = map.CastRay(rayFrom, -rayTo, maxDistance, hitPos) ? 1.0F : 0.0F;
				} else {
					distance = maxDistance * 2.0F;
				}
			}

			// monte-carlo integration
			unsigned int rayHitCount = 0;
			float roomSize = 0.0F;
			float feedbackness = 0.0F;

			for (float dist : distances) {
				if (dist < maxDistance) {
					rayHitCount++;
					roomSize += dist;
				}
			}

			for (float fb : feedbacknesses)
				feedbackness += fb;

			float reflections;
			if (rayHitCount > distances.size() / 4) {
				roomSize /= (float)rayHitCount;
				reflections = (float)rayHitCount / (float)distances.size();
			} else {
				reflections = 0.1F;
				roomSize = 100.0F;
			}

			feedbackness /= (float)distances.size();
			feedbackness = std::min(std::max(0.0F, feedbackness - 0.35F) / 0.5F, 1.0F);

			AmbienceInfo result;
			result.room = reflections * feedbackness;
			result.distance = (viewOrigin - rayFrom).GetLength();
			result.size = std::max(std::min(roomSize / 15.0F, 1.0F), 0.0F);
			result.room *= std::max(0.0F, std::min((result.size - 0.1F) * 4.0F, 1.0F));
			result.room *= 1.0F - result.size * 0.3F;

			return result;
		}

		void ClientPlayer::FiredWeapon() {
			Player& p = player;

			bool isThirdPerson = ShouldRenderInThirdPersonView();

			// make dlight
			int muzzleMode = cg_muzzleFire;
			if (muzzleMode == 1 || (muzzleMode == 2 && isThirdPerson)) {
				Vector3 muzzle = isThirdPerson
					? GetMuzzlePosition()
					: GetMuzzlePositionInFirstPersonView();

				bool showMuzzleSmoke = true;
				if (cg_trueAimDownSight && cg_pngScope && aimDownState > 0.99F)
					showMuzzleSmoke = false;
				client.MuzzleFire(muzzle, isThirdPerson || showMuzzleSmoke);
			}

			// FIXME: what if current tool isn't weapon?
			asIScriptObject* skin = isThirdPerson ? weaponSkin : weaponViewSkin;

			{
				ScriptIWeaponSkin2 interface(skin);
				if (interface.ImplementsInterface()) {
					// sound ambience estimation
					auto ambience = ComputeAmbience();
					interface.SetSoundEnvironment(ambience.room, ambience.size, ambience.distance);
					interface.SetSoundOrigin(p.GetEye());
				} else if (isThirdPerson && !hasValidOriginMatrix) {
					// Legacy skin scripts rely on OriginMatrix which is only updated when
					// the player's location is within the fog range.
					return;
				}
			}

			{
				ScriptIWeaponSkin interface(skin);
				interface.WeaponFired();
			}

			// In a headset the hand holds the third-person skin (AddToSceneVRHands),
			// whose animations run on shots too (a minigun's barrels spinning up):
			// told as well, muted, as the first-person skin has played the sounds
			if (!isThirdPerson && IsVRHandsView() && weaponSkin != skin) {
				ScriptIToolSkin hand(weaponSkin);
				hand.SetMuted(true);
				ScriptIWeaponSkin(weaponSkin).WeaponFired();
				hand.SetMuted(client.IsMuted());
			}
		}

		void ClientPlayer::EjectedBrass() {
			IRenderer& renderer = client.GetRenderer();
			IAudioDevice& audioDevice = client.GetAudioDevice();
			Player& p = player;
			WeaponType weaponType = p.GetWeaponType();

			// distance cull
			const auto& viewOrigin = client.GetLastSceneDef().viewOrigin;
			float distSqr = (p.GetOrigin() - viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			Handle<IModel> model;
			Handle<IAudioChunk> snd = NULL;
			Handle<IAudioChunk> snd2 = NULL;
			switch (weaponType) {
				case RIFLE_WEAPON:
					model = renderer.RegisterModel("Models/Weapons/Rifle/Casing.kv6");
					snd = SampleRandomBool()
							? audioDevice.RegisterSound("Sounds/Weapons/Rifle/ShellDrop1.opus")
							: audioDevice.RegisterSound("Sounds/Weapons/Rifle/ShellDrop2.opus");
					snd2 = audioDevice.RegisterSound("Sounds/Weapons/Rifle/ShellWater.opus");
					break;
				case SHOTGUN_WEAPON:
					model = renderer.RegisterModel("Models/Weapons/Shotgun/Casing.kv6");
					break;
				case SMG_WEAPON:
					model = renderer.RegisterModel("Models/Weapons/SMG/Casing.kv6");
					snd = SampleRandomBool()
							? audioDevice.RegisterSound("Sounds/Weapons/SMG/ShellDrop1.opus")
							: audioDevice.RegisterSound("Sounds/Weapons/SMG/ShellDrop2.opus");
					snd2 = audioDevice.RegisterSound("Sounds/Weapons/SMG/ShellWater.opus");
					break;
			}

			if (model) {
				float weapSide = Clamp((float)cg_viewWeaponSide, -1.0F, 1.0F);
				bool leftHanded = weapSide < 0.0F;

				Vector3 origin = ShouldRenderInThirdPersonView()
					? GetCaseEjectPosition()
					: GetCaseEjectPositionInFirstPersonView();

				float casingSide = leftHanded ? -1.0F : 1.0F;
				Vector3 o = p.GetFront();
				Vector3 vel = o * 0.5F + p.GetRight() * casingSide + p.GetUp() * 0.2F;

				if (weaponType == SHOTGUN_WEAPON)
					vel *= 0.5F;

				auto ent = stmp::make_unique<GunCasing>(&client,
					model.GetPointerOrNull(), snd.GetPointerOrNull(),
					snd2.GetPointerOrNull(), origin, o, vel);

				client.AddLocalEntity(std::move(ent));
			}
		}

		void ClientPlayer::ReloadingWeapon() {
			bool isThirdPerson = ShouldRenderInThirdPersonView();

			// FIXME: what if current tool isn't weapon?
			asIScriptObject* skin = isThirdPerson ? weaponSkin : weaponViewSkin;

			{
				ScriptIWeaponSkin2 interface(skin);
				if (interface.ImplementsInterface()) {
					// sound ambience estimation
					auto ambience = ComputeAmbience();
					interface.SetSoundEnvironment(ambience.room, ambience.size, ambience.distance);
					interface.SetSoundOrigin(player.GetEye());
				} else if (isThirdPerson && !hasValidOriginMatrix) {
					// Legacy skin scripts rely on OriginMatrix which is only updated when
					// the player's location is within the fog range.
					return;
				}
			}

			// Whatever the skin plays for the reload is tagged as this player's, so an
			// aborted reload can stop it.
			{
				IAudioDevice& audioDevice = client.GetAudioDevice();
				struct TagScope {
					IAudioDevice& dev;
					~TagScope() { dev.SetPlayTag(0); }
				} scope{audioDevice};
				audioDevice.SetPlayTag(reloadSoundTag);

				ScriptIWeaponSkin interface(skin);
				interface.ReloadingWeapon();
			}
		}

		void ClientPlayer::ReloadAborted(float progress) {
			// FIXME: what if current tool isn't weapon?
			asIScriptObject* skin =
			  ShouldRenderInThirdPersonView() ? weaponSkin : weaponViewSkin;

			// The skin is told first and may keep its reload sounds playing; a skin
			// that does not know about aborts has them faded out.
			bool stopSounds = true;
			ScriptIWeaponSkin4 interface(skin);
			if (interface.ImplementsInterface())
				stopSounds = interface.ReloadAborted(progress);

			if (stopSounds)
				client.GetAudioDevice().StopTagged(reloadSoundTag);
		}

		void ClientPlayer::ReloadedWeapon() {
			// FIXME: what if current tool isn't weapon?
			asIScriptObject* skin = ShouldRenderInThirdPersonView() ? weaponSkin : weaponViewSkin;

			{
				ScriptIWeaponSkin interface(skin);
				interface.ReloadedWeapon();
			}
		}
	} // namespace client
} // namespace spades