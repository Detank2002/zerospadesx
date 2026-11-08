/*
 Copyright (c) 2013 yvt
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

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

#pragma once

#include <list>
#include <array>
#include <memory>
#include <string>
#include <tuple>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "ClientCameraMode.h"
#include "DemoNetClient.h"
#include "FlashlightBudget.h"
#include "INetClient.h"
#include "NetClient.h"
#include "ILocalEntity.h"
#include "IRenderer.h"
#include "IWorldListener.h"
#include "MumbleLink.h"
#include "NoiseSampler.h"
#include "PieMenu.h"
#include "Player.h"
#include "PieMenuView.h"
#include "PlayerOutfit.h"
#include "VRNetProtocol.h"
#include "VRState.h"
#include <Core/Math.h>
#include <Core/ServerAddress.h>
#include <Core/Stopwatch.h>
#include <Core/VersionInfo.h>
#include <Gui/View.h>
#include <Gui/UI/Components/SoftwareCursor.h>
#include <Gui/UI/Components/LimboMenu.h>

#include "SoundIndicatorEntity.h"

namespace spades {
	class IStream;
	class Stopwatch;
	namespace client {
		class IRenderer;
		struct SceneDefinition;
		class GameMap;
		class GameMapWrapper;
		class World;
		struct PlayerInput;
		struct WeaponInput;
		class IAudioDevice;
		class IAudioChunk;
		class NetClient;
		class IFont;
		class FontManager;
		class ChatWindow;
		class CenterMessageView;
		class Corpse;
		class HurtRingView;
		class MapView;
		class ScoreboardView;
		class LimboView;
		class Player;
		class PaletteView;
		class TCProgressView;
		class ClientPlayer;
		class BloodMarks;
		class ClientUI;
		class PieMenuView;
		class Teamplay;

		class Client : public IWorldListener, public gui::View {
			friend class ScoreboardView;
			friend class MapView;
			friend class FallingBlock;
			friend class PaletteView;
			friend class TCProgressView;
			friend class ClientPlayer;
			friend class ClientUI;

			/** used to keep the input state of keypad so that
			 * after user pressed left and right, and then
			 * released right, left is internally pressed. */
			struct KeypadInput {
				bool left, right, forward, backward;
				KeypadInput() : left(false), right(false), forward(false), backward(false) {}
			};

			class FPSCounter {
				Stopwatch sw;
				int numFrames;
				double lastFps;

			public:
				FPSCounter();
				void MarkFrame();
				double GetFps() { return lastFps; }
			};

			FPSCounter fpsCounter;
			FPSCounter upsCounter;

			std::unique_ptr<NetClient> net;
			std::unique_ptr<DemoNetClient> demoNet;
			INetClient* activeNet; // points to net.get() or demoNet.get(), never null after DoInit
			std::string demoFilePath;

			// Seek-key hold state.
			// While a seek key is held, SeekPreview() advances demoSeekPendingTime so
			// the HUD stays responsive.  The full world-replay Seek() fires only on
			// key release, avoiding one expensive reset/replay per repeat tick.
			bool demoSeekForwardHeld = false;
			bool demoSeekBackwardHeld = false;
			float demoSeekRepeatTimer = 0.0F;	// accumulates dt between preview steps
			float demoSeekPendingTime = 0.0F;	// target time to commit on key release
			bool demoHudVisible = true;

			// Timeline cursor: while the demo is paused the mouse becomes an
			// absolute cursor that can click and drag the progress bar. The pointer
			// itself is the shared SoftwareCursor (`cursor`), same as the menus use.
			AABB2 demoTimelineRect;               // bar rect from the last HUD draw
			bool demoTimelineVisible = false;     // true only if the bar was drawn this frame
			bool demoCursorHasPos = false;        // false until the first absolute motion event
			bool demoTimelineDragging = false;

			// Automated one-shot follow for menuless demo replay (--replay-demo):
			// once the world has loaded the requested player is followed. Unused
			// unless EnableDemoReplayFollow() has been called.
			bool demoReplayFollowPending = false;
			std::string demoReplayFollowSpec; // empty = first player; else id or name

			std::string playerName;
			std::unique_ptr<IStream> logStream;

			Handle<ClientUI> scriptedUI;

			ServerAddress hostname;
			std::string serverName;
			std::string gameMode;

			std::unique_ptr<World> world;
			Handle<GameMap> map;
			std::unique_ptr<GameMapWrapper> mapWrapper;
			Handle<IRenderer> renderer;
			Handle<IAudioDevice> audioDevice;
			float time;
			bool readyToClose;
			float worldSubFrame;

			int frameToRendererInit;
			float timeSinceInit;

			MumbleLink mumbleLink;

			// view/drawing state for some world objects
			std::vector<Handle<ClientPlayer>> clientPlayers;
			// Players found in the world without a view and made one by
			// `EnsureClientPlayers`, until the world sets them again
			std::unordered_set<int> missingClientPlayers;
			/** Makes the view of the world's player `id`, if any; logs a failure. */
			void MakeClientPlayer(int id);
			/** Makes the view of any player in the world that has none. */
			void EnsureClientPlayers();

			// other windows
			std::unique_ptr<CenterMessageView> centerMessageView;
			std::unique_ptr<HurtRingView> hurtRingView;
			std::unique_ptr<MapView> mapView;
			std::unique_ptr<MapView> largeMapView;
			std::unique_ptr<ScoreboardView> scoreboard;
			std::unique_ptr<LimboView> limbo;
			std::unique_ptr<gui::LimboMenu> limboMenu;
			std::unique_ptr<PaletteView> paletteView;
			std::unique_ptr<TCProgressView> tcView;
			Handle<IImage> debugHitTestImage;

			// chat
			std::unique_ptr<ChatWindow> chatWindow;
			std::unique_ptr<ChatWindow> killfeedWindow;

			// pie menu
			std::unique_ptr<PieMenuView> pieMenuView;
			/** The menu the server sent last, which replaces the client's own
			 * entirely. It takes effect on the next open: the view keeps its own copy
			 * of the rings on show, so a ring never changes under a held thumb. */
			stmp::optional<PieMenu> serverPieMenu;

			// unified cursor used by all UI overlays
			std::unique_ptr<gui::SoftwareCursor> cursor;

			// Teamplay protocol extension (team ESP, pings, ESP marks).
			// Always allocated; it simply stays empty when the server does not
			// negotiate the extension.
			std::unique_ptr<Teamplay> teamplay;

			// Player Models protocol extension: which model set a team or a player is
			// drawn with. Empty when the server does not negotiate it.
			PlayerOutfits playerOutfits;

			// player state
			PlayerInput playerInput;
			WeaponInput weapInput;
			KeypadInput keypadInput;
			Player::ToolType lastTool;
			bool aimingDownSight;
			bool hasLastTool;
			Vector3 lastPosSent;
			Vector3 lastFrontSent;
			float lastPosSentTime;
			float lastOriSentTime;
			float lastHurtTime;
			float lastHealTime;
			float lastAliveTime;
			int lastRespawnCount;
			float lastHitTime;
			bool lastHitWasScoped;
			bool lastHitWasAirborne;
			int damageTaken;
			int lastHealth;
			int lastScore;
			int curKills;
			int curDeaths;
			int curStreak;
			int bestStreak;
			int meleeKills;
			int grenadeKills;
			int placedBlocks;

			std::unordered_map<int, std::unordered_map<int, int>> killStreaks;

			struct WeaponStats {
				std::array<int, 3> hits, shots;
				WeaponStats() : hits({0, 0, 0}), shots({0, 0, 0}) {}
			};
			WeaponStats weaponStats;

			struct HitStats {
				int numHeadHits, numBodyHits, numLimbHits;
				HitStats() : numHeadHits(0), numBodyHits(0), numLimbHits(0) {}
			};
			HitStats hitStats;

			// Net-graph history ring buffer.
			// Sampled once per frame; stores the last kNetGraphSamples measurements.
			static constexpr int kNetGraphSamples = 128;
			struct NetGraphSample {
				float downKbps;
				float upKbps;
				float pingMs;
			};
			struct NetGraphSampler {
				NetGraphSample samples[kNetGraphSamples]{};
				int head = 0; // next write index
				float accumTime = 0.0F;

				void Push(const NetGraphSample& s) {
					samples[head] = s;
					head = (head + 1) % kNetGraphSamples;
				}
				// Index 0 = oldest, index (kNetGraphSamples-1) = newest
				const NetGraphSample& At(int i) const {
					return samples[(head + i) % kNetGraphSamples];
				}
			};
			NetGraphSampler netGraph;

			float worldSetTime;

			bool reloadKeyPressed;

			struct HurtSprite {
				float angle;
				float horzShift;
				float scale;
				float strength;
			};
			std::vector<HurtSprite> hurtSprites;

			struct DamageIndicator {
				/** From this much damage a number is a crit: it rises straight up,
				 * bigger and pulsing. */
				static constexpr int kCritDamage = 100;
				/** The largest amount a number shows either way. Both a number and
				 * every amount added to it are bounded by this before they are summed,
				 * so the sum, and its negation drawn for a heal, always fit in an int. */
				static constexpr int kMaxDamage = 999999;

				int damage;
				int playerId;
				float fade;
				float lastHitTime;
				// Not drawn before this: a hit the server refused (it had us dead
				// first) is dropped by then (DamageIndicatorDelay).
				float showAt = 0.0F;
				Vector3 position;
				Vector3 velocity;
				bool crit;
				DamageIndicator() : damage(0), playerId(-1),
					fade(0.0F), lastHitTime(0.0F), crit(false) {}

				/** A number rising from `position`, drifting sideways as it goes when
				 * `scatter` is set, unless it is a crit. A negative `damage` is a heal. */
				static DamageIndicator Make(int playerId, int damage, const Vector3& position,
				                            float time, bool scatter);
				/** Adds another hit to this number and restarts its fade. */
				void Accumulate(int damage, float time);

			private:
				void Refresh(float time);
			};
			std::list<DamageIndicator> damageIndicators;

			struct SoundFeedbackIndicator {
				float fade = 0.0F;
				float radius = 0.0F;
				bool farSound = false;
			};
			std::vector<SoundFeedbackIndicator> soundFeedbackIndicators;

			float GetSprintState();
			float GetAimDownState();

			/**
			 * Queries whether the local player is allowed to use a tool in this state.
			 *
			 * The following factors are considered by this function:
			 *
			 *	- The player cannot use a tool while / soon after sprinting.
			 *	- The player cannot use a tool while switching tools.
			 *	- The player must exist and be alive to use a tool.
			 *
			 * The following factors also affect whether a tool can actually be used, but they
			 * do not affect the result of this function:
			 *
			 *	- Tool-specific status — e.g., out of ammo, out of block, "cannot build there"
			 *	- Firing rate limit imposed by the tool
			 */
			bool CanLocalPlayerUseTool();
			bool CanLocalPlayerUseWeapon();
			bool CanLocalPlayerReloadWeapon();

			/** Retrieves `ClientPlayer` for the local player, or `{}` if it does not exist. */
			stmp::optional<ClientPlayer&> GetLocalClientPlayer();

			void SetSelectedTool(Player::ToolType, bool quiet = false);

			// view
			SceneDefinition lastSceneDef;
			/** Derived from `lastSceneDef`. */
			Matrix4 lastViewProjectionScreenMatrix;
			float localFireVibrationTime;
			float grenadeVibration;
			float grenadeVibrationSlow;
			bool scoreboardVisible;
			bool netgraphVisible;
			bool hudVisible;

			/** The debug screen (F3). While it is open it replaces the HUD. */
			bool debugScreenVisible = false;
			/** CPU time of the last frame's `DrawScene` and `Draw2D`, in milliseconds. */
			float debugDrawSceneMs = 0.0F;
			float debugDraw2DMs = 0.0F;

			// History of the last frames, for the frame time graph of the debug screen.
			static constexpr int kDebugFrameSamples = 240;
			struct DebugFrameSample {
				/** The time the whole frame took, vsync wait included. */
				float frameMs;
				/** The CPU time spent drawing it, part of `frameMs`. */
				float cpuMs;
				DebugFrameSample(float frame = 0.0F, float cpu = 0.0F) : frameMs(frame), cpuMs(cpu) {}
			};
			struct DebugFrameSampler {
				DebugFrameSample samples[kDebugFrameSamples];
				int head = 0; // next write index

				void Push(const DebugFrameSample& s) {
					samples[head] = s;
					head = (head + 1) % kDebugFrameSamples;
				}
				// Index 0 = oldest, index (kDebugFrameSamples-1) = newest
				const DebugFrameSample& At(int i) const {
					return samples[(head + i) % kDebugFrameSamples];
				}
			};
			DebugFrameSampler debugFrames;

			struct GrenadeTracer {
				std::vector<Vector3> positions;
				float fadeTime = -1.0F;
				float fadeDuration = 0.3F;
				Vector3 color = MakeVector3(1.0F, 0.0F, 0.0F);
			};
			std::unordered_map<Grenade*, GrenadeTracer> grenadeTracers;

			CoherentNoiseSampler1D coherentNoiseSamplers[3];
			void KickCamera(float strength);

			bool isChristmasOn;
			float lastSnowDropTime;

			float hotBarIconState;

			float hitFeedbackIconState;
			bool hitFeedbackFriendly;

			float debugHitTestZoomState;
			bool debugHitTestZoom;

			float spectatorZoomState;
			bool spectatorZoom;

			// manual focus
			float focalLength;
			float targetFocalLength;
			bool autoFocusEnabled;

			// Spectator camera control
			/** The state of the following camera used for spectating. */
			struct {
				/**
				 * Indicates whether the current camera mode is first-person or not.
				 * Ignored and locked to third-person when the target player
				 * (`followedPlayerId`) is dead.
				 */
				bool firstPerson = true;

				/** Controls whether the follow camera is enabled. */
				bool enabled = false;
			} followCameraState;

			/** The state of the free floating camera used for spectating. */
			struct {
				/** The temporally smoothed position (I guess). */
				Vector3 position{0.0F, 0.0F, 0.0F};

				/** The temporally smoothed velocity (I guess). */
				Vector3 velocity{0.0F, 0.0F, 0.0F};
			} freeCameraState;

			/**
			 * The state shared by both of the third-person and free-floating cameras.
			 *
			 * Note: These values are not used in the `cg_thirdperson` mode.
			 */
			struct {
				/** The yaw angle. */
				float yaw = 0.0F;

				/** The pitch angle. */
				float pitch = 0.0F;
			} followAndFreeCameraState;

			/**
			 * The ID of the player being followed (in a spectator mode, or when the local player is
			 * dead). Must be valid as long as the follow cam is enabled.
			 *
			 * Must *not* specify a local player.
			 */
			int followedPlayerId;

			/**
			 * Chooses the next player to follow and assigns it to `this->followingPlayerId`.
			 * Enables the follow cam by assigning `true` to `followCameraState.enabled`.
			 * If the next player is the local player, disables the follow cam.
			 */
			void FollowNextPlayer(bool reverse);

			/**
			 * Retrieves the current camera mode.
			 */
			ClientCameraMode GetCameraMode();

			/**
			 * Retrieves the target player ID of the current camera mode (as returned by
			 * `GetCameraMode`).
			 *
			 * Throws an exception if the current camera mode does not have a player in concern.
			 */
			int GetCameraTargetPlayerId();

			/**
			 * Retrieves the target player of the current camera mode (as returned by
			 * `GetCameraMode`).
			 *
			 * Throws an exception if the current camera mode does not have a player in concern.
			 */
			Player& GetCameraTargetPlayer();

			bool IsPlayerBeingFollowed(int playerId);
			bool IsPlayerInFirstPerson(int playerId);

			/**
			 * Calculate the zoom value incorporating the effect of ADS for a first-person view.
			 *
			 * The camera mode must be first-person.
			 */
			float GetAimDownZoomScale();
			float GetLocalFireVibration();

			void SetBlockColor(IntVector3 color);
			void CaptureColor();

			bool inGameLimbo;
			/** A team picked in the limbo menu, or joined, on this map. Until then
			 * the menu offers, and Spawn joins, the team with fewer players: a new
			 * player who spawned at once landed in spectator mode and did not know
			 * how to get out of it (testers). */
			bool limboTeamChosen = false;
			int SmallerTeam();
			/** The team the limbo menu shows and Spawn joins. */
			int LimboTeam();
			bool IsLimboViewActive();
			void CloseLimboView();
			void ToggleLimboView();
			void SpawnPressed();

			// Enemies are excluded by default: naming whoever is under the crosshair
			// would double as an aim aid. Opt in only where that is intended.
			stmp::optional<std::tuple<Player&, hitTag_t>> HotTrackedPlayer(bool includeEnemies = false);

			/** This frame's lit flashlights, offered by the players as the scene is
			 * built and lit once every one is known. */
			FlashlightBudget flashlightBudget;

			// effects (local entity, etc)
			std::vector<DynamicLightParam> flashDlights;
			std::vector<DynamicLightParam> flashDlightsOld;
			void Bleed(Vector3, bool bounce = false);
			void EmitBlockFragments(Vector3, IntVector3 color);
			void EmitBlockDestroyFragments(IntVector3 pos);
			void GrenadeExplosion(Vector3);
			void GrenadeExplosionUnderwater(Vector3);
			void MuzzleFire(Vector3, bool smoke = true);
			void BulletHitWaterSurface(Vector3);
			void EmitSnowflakes(Vector3);
			void EmitSoundIndicator(Vector3, SoundType type);

			// drawings
			Handle<FontManager> fontManager;

			enum class AlertType { Notice, Warning, Error };
			AlertType alertType;
			std::string alertContents;
			float alertDisappearTime;
			float alertAppearTime;

			// Loading screen
			float mapReceivingProgressSmoothed = 0.0F;

			std::list<std::unique_ptr<ILocalEntity>> localEntities;
			std::list<std::unique_ptr<Corpse>> corpses;
			Corpse* lastLocalCorpse;
			void RemoveCorpses();
			void RemoveInvisibleCorpses();
			void RemoveCorpseForPlayer(int playerId);
			void RemoveAllLocalEntities();

			std::unique_ptr<BloodMarks> bloodMarks;
			Handle<IModel> blockCursorModel;

			int nextMapShotIndex;

			/** Project the specified world-space position to a screen space. */
			bool Project(const Vector3&, Vector2&);

			/** Recalculate `lastViewProjectionScreenMatrix` based on the current value of
			 * `lastSceneDef`. */
			void UpdateMatrices();

			void DrawSplash();
			void DrawStartupScreen();
			void DoInit();

			void ShowAlert(const std::string& contents, AlertType type);
			void ShowAlert(const std::string& contents, AlertType type,
				float timeout, bool quiet = false);
			void PlayAlertSound();
			void PlayScreenshotSound();
			void PlayHoverSound();
			void PlaySelectSound();
			void OnTeamSelected(int team);
			void OnWeaponSelected(WeaponType weapon);
			void OnSpawnPressed();
			void OnClosePressed();

			// Killsteak sounds
			std::vector<Handle<IAudioChunk>> killSounds;
			void LoadKillSounds();

			void UpdateWorld(float dt, float gameplayDt);
			void UpdateLocalSpectator(float dt);
			void UpdateLocalPlayer(float dt);
			void UpdateAutoFocus(float dt);
			float RayCastForAutoFocus(const Vector3& origin, const Vector3& direction);

			void Draw2D();
			void Draw2DWithoutWorld();
			void Draw2DWithWorld();

			Vector4 GetHUDColor(Player&);

			/** Called when the local player is alive. */
			void DrawJoinedAlivePlayerHUD();

			/** Called when the local player is dead. */
			void DrawDeadPlayerHUD();

			/**
			 * Called when `IsFirstPerson(GetCameraMode()).` Renders the follwing element:
			 *	- The center reticule
			 */
			void DrawFirstPersonHUD();

			/**
			 * Called when the local player is dead or a spectator.
			 */
			void DrawSpectateHUD();

			bool staffSpectating;
			bool spectatorPlayerNames;
			Vector4 GetPlayerColor(Player&);
			void DrawPlayerBox(Player&, const Vector4&);
			void DrawPlayerName(Player&, const Vector4&);
			void DrawHottrackedPlayerName();
			void DrawPubOVL();
			void DrawDeadPlayers();

			void DrawBlockPaletteHUD(float y);
			void DrawAlivePlayersCount();
			void DrawRecordingIndicator();
			void DrawDemoPlaybackHUD();
			void DrawDemoCursor();
			bool IsOverDemoTimeline(float x, float y);
			float DemoTimelineTimeAt(float x);
			void DrawHurtSprites();
			void DrawScreenEffect(bool hurt, float fadeTime = 0.35F);
			void DrawAlert();
			void DrawDebugAim(Player&);
			void DrawCompassBar(Player&);
			void DrawReloadIndicator(const Vector4&);
			void DrawStats();
			void DrawNetGraph();

			struct DebugScreenLine;
			/** Draws the debug screen: two columns of text over the world. */
			void DrawDebugScreen();
			/** `subject` is the player the camera looks out of, or `nullptr` for a free camera. */
			void BuildDebugScreenLeft(std::vector<DebugScreenLine>& lines, Player* subject);
			void BuildDebugScreenRight(std::vector<DebugScreenLine>& lines, Player* subject);
			void DrawHitTestDebugger();
			void DrawPlayerStats();

			void UpdateDamageIndicators(float dt);

			// Opens the pie menu on the ring set the crosshair calls for. The context
			// is resolved once, here, and then stays put for as long as the menu is up.
			/** Returns whether the menu opened. */
			bool OpenPieMenu();
			/** Sends what a chosen slice sends. `targetPlayerId` is the player aimed
			 * at when the menu opened, or `-1`. */
			void CommitPieSlice(const PieSlice&, int targetPlayerId);
			/** Whether the menu would open on the spectator context. */
			bool IsSpectatingForPieMenu();
			/** The player the drawn camera looks out of, whom a ray from it must not
			 * meet. */
			stmp::optional<int> ViewExcludedPlayerId();
			/** Whether the menu on show opened on the spectator context. */
			bool pieMenuSpectating = false;
			void DrawDamageIndicators();

			// ── Teamplay ───────────────────────────────────────────
			/** True while the team overlay key is held. */
			bool teamOverlayHeld;
			/** Eased `teamOverlayHeld`, so the overlay fades instead of snapping. */
			float teamOverlayAlpha;

			/** The team colour the extension mandates for a `TEAM_ESP` highlight: the
			 * one the server sent in State Data, not the player's block colour. */
			Vector4 GetTeamplayTeamColor(Player&, float alpha);

			/** A chevron above a player's head, with their class weapon, their name when
			 * `showName`, and `note` above it when there is one. Used for the team
			 * overlay, which the extension leaves to the client. */
			void DrawPlayerChevron(Player&, const Vector4& color, bool showWeapon,
								   bool showName = true,
								   const std::string& note = std::string());

			/** Whether a player's name is already drawn by a teamplay chevron, so the
			 * ordinary name label must not draw it a second time lower down. */
			bool HasTeamplayChevronName(Player&);

			/**
			 * Whether this player is currently revealed through walls, and in what
			 * colour. Called by `ClientPlayer` as it submits the player's models, so the
			 * renderer draws their body and weapon where the world hides them. The
			 * extension requires the body shape itself for an ESP Mark.
			 */
			bool ShouldRevealPlayer(Player&, Vector3& outColor);

			/** Whether anything at all could be revealed this frame, asked of the same
			 * two sources as `ShouldRevealPlayer` before any player is examined. */
			bool HasRevealedPlayers();

			/** Which half of the blink a marked teammate is in, so every surface
			 * alternates together. */
			bool IsMarkBlinkPhase() const;

			/** Whether `TEAM_ESP` is revealing this player to the local player right
			 * now, which is the permission the overlay key spends. */
			bool IsTeamESPRevealing(Player&);

			/**
			 * The colour a mark on this player is drawn in this frame, on every surface
			 * it named: the colour the server chose, alternating with the team colour
			 * while `TEAM_ESP` is revealing the same player.
			 *
			 * Both colours are true at once on a marked teammate — one says they are
			 * yours, the other says what the server is telling you about them — so the
			 * mark blinks between them instead of throwing either away.
			 */
			Vector3 ResolveMarkColor(Player&, const IntVector3& markColor);

			/** Teammates revealed through `TEAM_ESP`, while the overlay key is held. */
			void DrawTeamOverlay();
			/** Players the server marked, which are drawn whatever `TEAM_ESP` says. */
			void DrawEspMarks();
			/** A notice when the server marked the local player to somebody. */
			void DrawLocalPlayerMarkedIndicator();
			/** Relayed pings, as markers anchored to their world position. */
			void DrawTeamplayPings();

			/** The world position the local player's crosshair points at. Returns false
			 * when the ray hits nothing usable. */
			bool ResolveCrosshairWorldPos(Vector3& out);

			/** Sends a neutral ping at whatever the crosshair points at. Bound to a key;
			 * reports why nothing happened when the server does not allow pings. */
			void SendTeamplayPingAtCrosshair();

			/** The camera ray when the menu opened, for a slice's `%b`, `%h` and `%r`,
			 * frozen then. The menu takes the mouse for itself, but the player can
			 * still move, so resolving it again on release would drift away from what
			 * they were pointing at. */
			PieAim pieMenuAim;

			bool trainingJoined = false; // RunFrame: the course is joined at once

			// ── VR (Client_VR.cpp) ─────────────────────────────────
			/** The headset's play space as the game sees it. Tracking space is
			 * turned by `bodyYaw` and scaled by vr_worldScale into the world; the
			 * body stands on `anchor`, and the head's offset from it is walked into
			 * the body (room-scale) as far as the map allows. */
			struct VRRig {
				bool active = false;
				bool calibrated = false;
				float bodyYaw = 0.0F;
				float standingHeight = 1.6F;
				float anchorX = 0.0F, anchorZ = 0.0F;
				float scale = 1.4F;

				// This frame, world space.
				Vector3 residual{0.0F, 0.0F, 0.0F}; // head beside the body
				float lift = 0.0F;                  // head above/below the game eye
				Vector3 camera{0.0F, 0.0F, 0.0F};
				Vector3 headAxes[3];                // forward, right, down
				bool handValid[2] = {false, false};
				Vector3 handPos[2];
				Vector3 handAxes[2][3];             // forward, right, down
				Vector3 aimPos[2];
				Vector3 handVelocity[2];            // blocks per second

				// Input state carried between frames.
				bool crouchToggle = false;
				bool physicalCrouch = false;
				bool sprintToggle = false;
				bool snapLatched = false;
				bool paletteLatched = false;
				bool prevPrimary[2] = {false, false};
				bool prevSecondary[2] = {false, false};
				bool prevStickClick[2] = {false, false};
				// A gun held in both hands: the off hand on the barrel steers it.
				bool twoHanded = false;
				// The shoulder stock's hold and how far it is eased in.
				VRStockState stock;
				// ...and with the gun hand at the shoulder, from the shoulder
				// (vr_shoulderStock), like a stock.
				bool shouldered = false;
				// The off hand's trigger held with a gun: any gun braced on the
				// shoulder as the rifle's stock is (UpdateVRAimAssist).
				bool braceHeld = false;
				// Aim smoothing (vr_aimSmooth): the filtered forward of the gun,
				// and the angular speed it adapts to.
				VRAimFilter aimFilter;
				// The gun's recoil, turned in after the smoothing.
				VRRecoil recoil;
				bool recoilOn = false;
				// Each hand's own forward, before any of that.
				Vector3 ownForward[2];
				bool offGripHeld = false;
				// Swinging the spade hits: latched until the hand slows, and the
				// press held a moment so it lands when the spade is ready.
				bool swingLatched = false;
				float swingHold = 0.0F;
				// Holsters: the one the gun hand is at (VRHolster), and whether its
				// grip is held since it took a tool from one.
				int holsterHover = -1;
				VRGripState grip;
				bool wasReloading = false;
				// Guns fire from the eye at what the gun points at (VRGunAimPoint);
				// the laser shows that shot, from the gun to where it lands.
				bool gunLaser = false;
				// The scope's line in the last frame it was looked through: shots
				// aim along it (the barrel is 0.23 blocks under it).
				bool scopeAimValid = false;
				Vector3 scopeAimOrigin{0.0F, 0.0F, 0.0F};
				Vector3 scopeAimDir{1.0F, 0.0F, 0.0F};
				// The game's frame rate, smoothed, for the wrist.
				float fps = 0.0F;
				// Solid tools (vr_toolCollision): whether the hands are stopped by the
				// map this frame (alive, in the body), how far the held tool reaches
				// ahead of its hand (blocks), and whether it is touching.
				bool solidHands = false;
				float toolReach = 0.0F;
				bool toolBlocked = false;
				// The wrist menu: the other hand brought to the wrist panel. Whether
				// it is up, and the tool hand's pointer on it (canvas pixels).
				bool wristMenu = false;
				bool wristPointing = false;
				Vector2 wristCursor{0.0F, 0.0F};
				float wristPointDistance = 0.0F; // metres, controller to panel
				int wristHover = -1;
				bool wristPokeArmed = true;
				bool wristTriggerHeld = false;
				// A trigger pull that began on the panel: not a shot, until let go.
				bool wristTriggerOwned = false;
				bool wristToTeam = true; // where quick chat goes
				float wristSendCooldown = 0.0F;
				// Physical reloading (vr_reloadGesture): 0 nothing, 1 the magazine
				// is out, 2 a new one is in the off hand.
				int reloadStage = 0;
				bool prevOffSqueeze = false;
				/** The off hand's grip, for the headlamp switch at the head. */
				bool prevLampSqueeze = false;
				bool lampHandAtHead = false;
				// The rifle scope (vr_scope), while an eye is behind its lens this
				// frame: where the lens is, for drawing it into each eye.
				bool scopeActive = false;
				// The lens is through a wall: drawn black, never the far side.
				bool scopeBlocked = false;
				Vector3 scopeLensPos{0.0F, 0.0F, 0.0F};
				Vector3 scopeLensRight{1.0F, 0.0F, 0.0F};
				float scopeLensRadius = 0.0F;
				// A reflex sight on the held gun (ClientPlayer, from its skin): its
				// window, the barrel's direction and the window's radius, for its dot
				// (DrawVRReflexDot).
				bool reflexActive = false;
				Vector3 reflexCenter, reflexDir;
				float reflexRadius = 0.0F;
				// The picture's half-angles (radians): what the centre of the lens
				// shows at the monitor's magnification, and what its rim reaches.
				float scopeCentreHalf = 0.05F;
				float scopeEdgeHalf = 0.05F;
				// The gun's own right and up at the lens: the reticle turns with the
				// gun, the picture stays level with the world.
				Vector3 scopeGunRight{1.0F, 0.0F, 0.0F};
				Vector3 scopeGunUp{0.0F, 0.0F, -1.0F};
				// The comfort camera (vr_comfortCamera). When the view is not your
				// own body's (dead, following someone), it holds still and moves only
				// in cuts behind a blink. `flatCameraOrigin` is where the flat
				// camera is (ApplyVRToSceneDefinition), `fade` 0 clear to 1 black.
				ClientCameraMode cameraMode = ClientCameraMode::None;
				ClientCameraMode lastCameraMode = ClientCameraMode::None;
				Vector3 flatCameraOrigin{0.0F, 0.0F, 0.0F};
				bool comfortAnchored = false;
				Vector3 comfortAnchor{0.0F, 0.0F, 0.0F};
				float comfortAwayTime = 0.0F;
				float fade = 0.0F;
				bool fadingOut = false;
				// The comfort vignette (vr_vignette), 0 none to 1 full, and the
				// smooth turn's rate (radians a second) that feeds it.
				float vignette = 0.0F;
				float turnRate = 0.0F;
				// Both sticks in: held long enough, a recentre. `stickChord` stays
				// set until both are out, so neither click toggles anything.
				bool stickChord = false;
				float recenterHold = 0.0F;
				bool recenterHoldFired = false;
				// The menu's Reset Height (RequestVRHeightReset), for the next frame.
				bool heightResetRequested = false;
				bool prevTrigger[2] = {false, false};
				bool triggerHeld[2] = {false, false};
				float lastToolUseTime = -100.0F;
				float poseSendTimer = 0.0F;
				// Walking between two of the server's eight directions
				// (UpdateVRPlayerInput): how far the next step leans to the second,
				// and how long the current choice holds.
				float walkLean = 0.0F;
				float walkHold = 0.0F;
				bool walkSecond = false;
				// The strongest throw of the last few frames (see UpdateVRPlayerInput).
				float throwPeakPower = 0.0F;
				float throwPeakAge = 0.0F;
				Vector3 throwPeakDir{1.0F, 0.0F, 0.0F};
			};
			VRRig vrRig;
			/** Synthetic keys the controllers are holding down, by key name. */
			std::unordered_map<std::string, bool> vrHeldKeys;

			struct RemoteVRPose {
				VRNetPose pose;
				float receivedTime = 0.0F;
			};
			std::unordered_map<int, RemoteVRPose> vrRemotePoses;
			/** What the server's vr_presence script allows (VRConfig flags). */
			uint8_t vrServerFlags = 0;
			float vrPoseInterval = 1.0F / 20.0F;

			/** Reads the controllers into this frame's input, turning and room-scale
			 * movement. Before UpdateWorld, so the physics sees it. */
			void UpdateVR(float dt);
			void UpdateVRPlayerInput(Player&, const VRFrameState&, float dt);
			/** Where a gun at `from` pointing `dir` aims: the first block or player
			 * hitbox on the ray, or 128 blocks out. `hit`: it met something. */
			Vector3 VRGunAimPoint(Player& p, const Vector3& from, const Vector3& dir, bool& hit);
			/** A gun's shot from the eye this moment (VRGunAimPoint from the eye
			 * along the gun, or along the scope): its direction, where it lands,
			 * and whether that is
			 * on something. Called for the shot and again for the laser, where it
			 * is drawn, so the laser keeps up with the gun. */
			Vector3 VRGunShot(Player& p, Vector3& impact, bool& hit);
			/** The spade or blocks have a block in reach along their ray. */
			bool VRToolHasReach(Player& p);
			void UpdateVRRigTransforms(const VRFrameState&, const Vector3& origin);
			/** Takes and lets go of a gun with the off hand (vr_twoHandedGrip). */
			void UpdateVRTwoHandedGrip(Player&, const VRFrameState&);
			/** Points the gun hand from itself to the off hand. */
			void ApplyVRTwoHandedAim();
			/** Points the gun hand along `forward`, keeping its roll. */
			void SetVRToolForward(const Vector3& forward);
			/** The gun hand's forward turned by the recoil now, if a gun is held. */
			void ApplyVRRecoil();
			/** The virtual shoulder, world space, for a stocked gun. */
			Vector3 VRShoulderPoint() const;
			/** A two-handed gun's forward: hand to hand, eased to the stock's line
			 * (shoulder through front hand) as far as the stock has hold. */
			Vector3 VRTwoHandedForward() const;
			/** Shoulders and unshoulders the gun, and smooths its aim; once a frame. */
			void UpdateVRAimAssist(Player&, float dt);
			/** How far the held tool reaches ahead of the hand, blocks. */
			float VRToolReach(Player&) const;
			/** Stops the hands, and the tool in the tool hand, at the map. */
			void ApplyVRToolCollision();

			/** A button on the wrist menu, in wrist canvas pixels. */
			struct VRWristButton {
				enum Kind { Chat, TeamChat, Phrase, QuickTarget, Colour, Menu, Light, Team };
				Kind kind;
				int index;
				AABB2 rect;
				IntVector3 color;
			};
			std::vector<VRWristButton> VRWristMenuLayout() const;
			/** The wrist menu's pointer and buttons; once a frame. */
			void UpdateVRWrist(const VRFrameState&, float dt, bool menu);
			void PressVRWristButton(const VRWristButton&);
			void DrawVRWristMenu();
			static const char* VRQuickPhrase(int);
			/** How much of `offset` the view can move from `eye` before it would
			 * be inside a block: 0..1. */
			float VRHeadClearance(const Vector3& eye, const Vector3& offset) const;
			/** Whether nothing solid lies between `from` and `to`. */
			bool VRLineOfSight(const Vector3& from, const Vector3& to) const;
			/** The wrist HUD's health and ammo, large (DrawJoinedAlivePlayerHUD). */
			void DrawVRWatch(Player&, int clipNum, int stockNum, int clipSize, bool isToolWeapon,
			                 bool isReloading);
			/** How long a damage number waits for the server to refuse the hit. */
			float DamageIndicatorDelay();
			void DrawVRFrameRate();
			void DrawVRScopeLens();
			/** The reflex sight's dot, in the eye being drawn (DrawInEachVREye). */
			void DrawVRReflexDot();
			void UpdateVRComfortCamera(float dt);
			/** The in-eye tip box (the training course's tips, the no-vr_presence
			 * notice), drawn in the eye being drawn. */
			void DrawVRTutorial();
			/** Until when (Client::time) the no-vr_presence notice shows in a headset. */
			float vrPresenceNoticeUntil = -1.0F;
			/** When to decide whether to show it (the server's extensions have had
			 * time to arrive), or -1 once decided. */
			float vrPresenceCheckAt = -1.0F;
			void UpdateVRPresenceNotice();
			/** On the training course (its marker voxel is in the map)? */
			bool IsOnTrainingCourse();
			/** The training course's tip for where the local player is, for a
			 * headset or a monitor; false off the course or without a body. */
			bool TrainingTip(bool vr, std::string& title, std::string& tip);
			/** That tip on a monitor, at the top of the screen. */
			void DrawTrainingTip();
			/** A tip box: a title line in `accent`, then the text wrapped to
			 * `layoutW` and drawn `scale` times its size, its top centre at `at`. */
			void DrawTipBox(const std::string& title, const std::string& text, Vector4 accent,
			                Vector2 at, float layoutW, float scale);
			/** A tip's text in lines of at most `width`, breaking at newlines too. */
			static std::vector<std::string> WrapTipText(IFont& font, const std::string& text,
			                                            float width);
			const GameMap* trainingCheckedMap = nullptr;
			bool trainingCourse = false;

			// Training (Client_Training.cpp): the tutorial's steps done (bits of
			// training::Step, and of training::VRStep in a headset), its time,
			// and the aim run as last seen, with its result against the best.
			int trainingSteps = 0, trainingVRSteps = 0;
			float trainingTime = 0.0F;
			bool trainingStarted = false, trainingFinished = false, trainingNewBest = false;
			float trainingPreviousBest = -1.0F;
			int aimRunSeen = -1;
			bool aimResultSaved = false, aimNewBest = false;
			float aimPreviousBest = -1.0F;
			void UpdateTraining(float dt);
			/** The checklist under the course's tips: done steps and the time. */
			std::string TrainingChecklist(bool vr);
			/** The aim run for the tip box: the countdown, the clock and the
			 * score, or the results. False when no scenario is running. */
			bool AimTip(bool vr, std::string& title, std::string& tip);
			/** The key that starts another run once one is over; true if taken. */
			bool AimRestartKey(bool jumpPressed);

		public:
			/** Training's best scores (TrainingBests.txt): "aim <scenario>
			 * <weapon>" to the score, "tutorial <vr>" to the seconds. */
			static std::map<std::string, float> ReadTrainingBests();
			static void WriteTrainingBests(const std::map<std::string, float>& bests);
			/** A VR step of the tutorial done (training::VRStep). */
			void MarkTrainingVRStep(int step);
			/** A scenario's name, as the menu and the HUD show it. */
			static std::string AimScenarioName(int scenario);

		private:
			/** The comfort vignette, drawn in the eye being drawn. */
			void DrawVRVignette();
			Handle<IImage> vrVignetteImage;
			/** The physical reload (vr_reloadGesture); true to press reload now. */
			bool UpdateVRReload(Player&, const VRFrameState&);
			/** The camera modes that move the view with nothing of yours moving. */
			static bool IsVRComfortMode(ClientCameraMode);
			/** `deliberate`: the player held both sticks in, rather than the
			 * runtime or the session asking. */
			void RecenterVR(const VRFrameState&, bool deliberate);
			void VRSetKey(const std::string& key, bool down);
			void VRReleaseAllKeys();
			Vector3 VRToWorld(const Vector3& trackingVector) const;
			/** Replaces a flat scene definition's view with the head and the eyes. */
			void ApplyVRToSceneDefinition(SceneDefinition&, ClientCameraMode);
			void SendVRPose();
			/** Rumbles a controller (-1: both) when a headset is in use. */
			void VRPulse(int hand, float amplitude, float seconds,
			             VRHapticShape shape = VRHapticShape::Flat);
			/** True while the player HUD is being drawn onto the wrist panel. */
			bool vrWristPass = false;
			void DrawVRWristBackdrop();
			/** True while drawing into one eye's picture (DrawInEachVREye). */
			bool vrEyeOverlayPass = false;
			/** Runs `draw` once per eye, with the projection (`Project`) and the 2D
			 * canvas set to that eye: what it pins to a place in the world is then
			 * seen at that place's depth, not on the flat panel. */
			/** `draw` once in each eye's picture: on canvases 2 and 3, or with
			 * `firstCanvas` 4 on the headset-only ones (IRenderer::SetVRCanvas). */
			void DrawInEachVREye(const std::function<void()>& draw, int firstCanvas = 2);

			void DrawScene();
			void AddGrenadeToScene(Grenade&);
			void AddDebugObjectToScene(const OBB3&, const Vector4& col = MakeVector4(1, 1, 1, 1));
			void AddMapObjectsToScene();

			SceneDefinition CreateSceneDefinition();

			void TakeScreenShot(bool sceneOnly, bool scoreboardOnly = false);

			// Demo helpers (see EnableDemoReplayFollow).
			void UpdateDemoReplayFollow();
			int ResolveDemoPlayer(const std::string& spec);

			std::string BuildDemoContext();

			std::string MapShotPath();
			void TakeMapShot();

			void NetLog(const char* format, ...);

		protected:
			~Client();

		public:
			Client(Handle<IRenderer>, Handle<IAudioDevice>,
				const ServerAddress& host, Handle<FontManager>,
				const std::string& demoPath = "");

			bool HasLocalPlayer();
			bool IsDemoMode() const { return demoNet != nullptr; }
			/** True while a paused demo exposes the mouse as an absolute cursor. */
			bool IsDemoCursorMode() const {
				return demoNet != nullptr && demoNet->IsPaused() && demoHudVisible;
			}
			DemoNetClient* GetDemoNetClient() { return demoNet.get(); }
			void ReloadDemo();

			const std::string& GetServerName() const { return serverName; }
			/** Game mode announced by the server list; empty for direct connects and demos. */
			const std::string& GetGameMode() const { return gameMode; }

			/** Extensions announced by the server; empty in demo mode or before the handshake. */
			std::vector<ServerExtensionInfo> GetServerExtensions() const {
				return (net && !IsDemoMode()) ? net->GetServerExtensions()
											  : std::vector<ServerExtensionInfo>();
			}

			/**
			 * Auto-follows a player once the demo world has loaded (empty = first
			 * player; otherwise a player id or name) and keeps normal playback
			 * running. Used by menuless demo replay (--replay-demo). Only
			 * meaningful when the client was created with a demo path.
			 */
			void EnableDemoReplayFollow(const std::string& playerSpec);

			void RunFrame(float dt) override;

			void Closing() override;
			void MouseEvent(float x, float y) override;
			void WheelEvent(float x, float y) override;
			void KeyEvent(const std::string&, bool down) override;
			void TextInputEvent(const std::string&) override;
			void TextEditingEvent(const std::string&, int start, int len) override;
			bool AcceptsTextInput() override;
			AABB2 GetTextInputRect() override;
			bool NeedsAbsoluteMouseCoordinate() override;
			bool ExecCommand(const Handle<gui::ConsoleCommand>&) override;
			Handle<gui::ConsoleCommandCandidateIterator>
			AutocompleteCommandName(const std::string& name) override;

			void SetWorld(World*);
			World* GetWorld() const { return world.get(); }

			// Snapshot of all view-related state, used to preserve camera across demo seeks
			struct ViewState {
				bool followEnabled;
				bool followFirstPerson;
				int followedPlayerId;
				Vector3 freePosition;
				Vector3 freeVelocity;
				float yaw;
				float pitch;
				float worldSetTime;
			};
			ViewState SaveViewState() const;
			void RestoreViewState(const ViewState&);
			void AddLocalEntity(std::unique_ptr<ILocalEntity>&& ent) {
				localEntities.emplace_back(std::move(ent));
			}

			void MarkWorldUpdate();

			IRenderer& GetRenderer() { return *renderer; }
			SceneDefinition GetLastSceneDef() { return lastSceneDef; }
			IAudioDevice& GetAudioDevice() { return *audioDevice; }
			gui::SoftwareCursor* GetCursor() { return cursor.get(); }

			float GetTime() { return time; }
			const std::vector<SoundFeedbackIndicator>& GetSoundFeedbackIndicators() { return soundFeedbackIndicators; }

			bool WantsToBeClosed() override;
			bool IsMuted();
			bool IsScoreboardVisible() { return scoreboardVisible; }
			bool IsNetgraphVisible() { return netgraphVisible; }
			bool IsDebugScreenVisible() { return debugScreenVisible; }

			/** What the Teamplay extension has made this client remember. */
			const Teamplay& GetTeamplay() const { return *teamplay; }

			/** The model sets the Player Models extension has assigned. */
			const PlayerOutfits& GetPlayerOutfits() const { return playerOutfits; }
			PlayerOutfits& GetPlayerOutfits() { return playerOutfits; }

			// ── VR ─────────────────────────────────────────────────
			/** The headset drives the camera this frame. */
			bool IsVRActive() const { return vrRig.active; }
			/** Stand height and play space centre from the head as it is next
			 * frame, as holding both sticks in does. */
			void RequestVRHeightReset() { vrRig.heightResetRequested = true; }
			/** The local player's hand this frame, world space; axes are forward,
			 * right and down. False when the controller is not tracked. */
			bool GetLocalVRHand(int hand, Vector3& position, Vector3 axes[3]) const;
			/** The block colour held when the local player last died, if they have. */
			stmp::optional<IntVector3> GetKeptBlockColor() const { return keptBlockColor; }
			stmp::optional<IntVector3> keptBlockColor;
			/** A VR player's latest pose, or null if they sent none lately. */
			const VRNetPose* GetRemoteVRPose(int playerId) const;
			/** Called by the net client. */
			void VRPoseReceived(int playerId, const VRNetPose&);
			void VRPresenceConfigured(uint8_t flags, uint8_t maxRate);

			void PlayerSentChatMessage(Player&, bool global, const std::string&);
			void ServerSentMessage(bool system, const std::string&);

			// ── Teamplay, called by the net client ─────────────────
			/** The server sent a Config: which of the extension's features it permits,
			 * and which way north is. */
			void TeamplayConfigured(uint8_t features, float northX, float northY);
			/** A ping was relayed to us. `playerId` is `255` for a server-origin ping,
			 * and a `duration` of `0` removes that player's ping. */
			void TeamplayPingReceived(int playerId, Vector3 position, float duration,
											  uint8_t surfaces, const IntVector3& color,
											  std::string reason);
			/** The server marked (or, with a `0` duration, unmarked) a player. */
			void TeamplayMarkReceived(int playerId, float duration, uint8_t surfaces,
											  uint8_t flags, const IntVector3& color,
											  std::string reason);
			/** A player spawned, which ends a mark that was flagged to end there. Split
			 * out of `PlayerSpawned` because a demo seek skips the rest of that work but
			 * still has to rebuild the marks in force at the destination. */
			void TeamplayPlayerSpawned(int playerId);

			// ── Pie Menu, called by the net client ─────────────────
			/** The server sent a menu, which replaces the one shown from the next
			 * time the menu opens. One with no pies means no menu at all. */
			void PieMenuReceived(PieMenu);

			// ── Damage Markers, called by the net client ───────────
			/** The server applied `amount` damage to a player the local player hit. A
			 * negative amount is a heal. */
			void DamageMarkerReceived(int playerId, int amount);

			void PlayerCapturedIntel(Player&);
			void PlayerPickedIntel(Player&);
			void PlayerDropIntel(Player&);
			void TeamCapturedTerritory(int teamId, int territoryId);
			void TeamWon(int);
			void JoinedGame();
			void LocalPlayerCreated();
			void RegisterPlacedBlocks(int c) { placedBlocks += c; };
			void PlayerCreatedBlock(Player&);
			void PlayBlockDestroySound(Vector3);
			void PlayerDestroyedBlockWithWeaponOrTool(IntVector3);
			void PlayerDiggedBlock(IntVector3);
			void GrenadeDestroyedBlock(IntVector3);
			void PlayerLeaving(Player&);
			void PlayerJoinedTeam(Player&);
			void PlayerSpawned(Player&);

			/** Called after a player's flashlight was switched, to give the feedback. */
			void PlayerSwitchedFlashlight(Player&);
			/** Switches the local player's flashlight, through the server when it
			 * synchronizes them: the F key, and in a headset the wrist menu's Light
			 * and a squeeze of the off hand at the head. */
			void ToggleLocalFlashlight();

			// IWorldListener begin
			void PlayerObjectSet(int) override;
			void PlayerMadeFootstep(Player&) override;
			void PlayerJumped(Player&) override;
			void PlayerLanded(Player&, bool hurt) override;
			void PlayerFiredWeapon(Player&) override;
			void PlayerRecoiledInVR(Player&, float yaw, float pitch) override;
			void PlayerEjectedBrass(Player&) override;
			void PlayerDryFiredWeapon(Player&) override;
			void PlayerReloadingWeapon(Player&) override;
			void PlayerAbortedReload(Player&, float progress) override;
			void PlayerReloadedWeapon(Player&) override;
			void PlayerChangedTool(Player&) override;
			void PlayerPulledGrenadePin(Player&) override;
			void PlayerThrewGrenade(Player&, stmp::optional<const Grenade&>) override;
			void PlayerMissedSpade(Player&) override;
			void PlayerRestocked(Player&) override;

			/** @deprecated use BulletHitPlayer */
			void PlayerHitBlockWithSpade(Player&, Vector3 hitPos,
				IntVector3 blockPos, IntVector3 normal) override;
			void PlayerKilledPlayer(Player& killer, Player& victim, KillType) override;

			void BulletHitPlayer(Player& hurtPlayer, HitType, Vector3 hitPos, Player& by,
				std::unique_ptr<IBulletHitScanState>& stateCell) override;
			void BulletNearPlayer(Player& by) override;
			void BulletHitBlock(Vector3, IntVector3 blockPos, IntVector3 normal) override;
			void AddBulletTracer(Player& player, Vector3 muzzlePos, Vector3 hitPos) override;
			void GrenadeExploded(const Grenade&) override;
			void GrenadeBounced(const Grenade&) override;
			void GrenadeDroppedIntoWater(const Grenade&) override;

			void BlocksFell(std::vector<IntVector3>) override;

			void LocalPlayerBlockAction(IntVector3, BlockActionType type) override;
			void LocalPlayerCreatedLineBlock(IntVector3, IntVector3) override;
			void LocalPlayerHurt(HurtType type, Vector3 source) override;
			void LocalPlayerBuildError(BuildFailureReason reason) override;
			// IWorldListener end
		};
	} // namespace client
} // namespace spades