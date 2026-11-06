/*
 Copyright (c) 2013 Fran6nd

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

#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>

#include <Client/IGameMapListener.h>
#include <Client/IRenderer.h>
#include <Client/SceneDefinition.h>
#include <Core/Math.h>

#include "VulkanDynamicLight.h"
#include "VulkanSceneLights.h"
#include "VulkanSceneView.h"
#include "VulkanSettings.h"

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanMapRenderer;
		class VulkanModelRenderer;
		class VulkanSpriteRenderer;
		class VulkanLongSpriteRenderer;
		class VulkanGlareRenderer;
		class VulkanScreenSpaceAmbientOcclusion;
		class VulkanMapOccupancy;
		class VulkanImageRenderer;
		class VulkanWaterRenderer;
		class VulkanFlatMapRenderer;
		class VulkanShadowMapRenderer;
		class VulkanMapShadowRenderer;
		class VulkanFramebufferManager;
		class VulkanProgramManager;
		class VulkanModelManager;
		class VulkanImageManager;
		class VulkanPipelineCache;
		class VulkanProgram;
		class VulkanShader;
		class VulkanImage;
		class VulkanImageWrapper;
		class VulkanTemporaryImagePool;
		class VulkanBuffer;
		class VulkanAutoExposureFilter;
		class VulkanBloomFilter;
		class VulkanFogFilter;
		class VulkanDepthOfFieldFilter;
		class VulkanFXAAFilter;
		class VulkanTemporalAAFilter;
		class VulkanCameraBlurFilter;
		class VulkanResampleBicubicFilter;
		class VulkanCavityOutlineFilter;
		class VulkanDepthResolveFilter;
		class VulkanColorCorrectionFilter;
		class VulkanLensFlareFilter;
		class VulkanAmbientShadowRenderer;
		class VulkanRadiosityRenderer;

		class VulkanRenderer : public client::IRenderer, public client::IGameMapListener {
			struct DebugLine {
				Vector3 v1, v2;
				Vector4 color;
			};

			// Deferred deletion queue entry
			struct DeferredDeletion {
				Handle<VulkanBuffer> buffer;
				uint32_t frameIndex; // Frame when this buffer was marked for deletion
			};

			// Pending texture upload (staged, not yet submitted)
			struct PendingUpload {
				VkCommandBuffer commandBuffer;
				Handle<VulkanBuffer> stagingBuffer;
			};

			// First, so that everything built from a latched setting sees one value
			VulkanSettings settings;
			Handle<gui::SDLVulkanDevice> device;
			client::GameMap* map;
			bool inited;
			bool sceneUsedInThisFrame;

			client::SceneDefinition sceneDef;

			std::vector<DebugLine> debugLines;
			std::vector<VulkanDynamicLight> lights;

			// Vulkan rendering resources
			std::vector<VkCommandBuffer> commandBuffers;
			VkRenderPass renderPass;
			// Variant of renderPass for 2D-only frames (no scene blit): clears the
			// swapchain colour from an UNDEFINED initial layout instead of LOADing it
			// from COLOR_ATTACHMENT_OPTIMAL. Render-pass-compatible with renderPass,
			// so the same swapchainFramebuffers are reused.
			VkRenderPass renderPass2D = VK_NULL_HANDLE;
			std::vector<VkFramebuffer> swapchainFramebuffers;

			// Frame synchronization
			uint32_t currentImageIndex;
			uint32_t currentFrameSlot; // cycles 0..maxFramesInFlight-1, matches semaphore slots
			VkSemaphore imageAvailableSemaphore;
			VkSemaphore renderFinishedSemaphore;
			std::vector<VkFence> inFlightFences; // sized to maxFramesInFlight, not image count
			// One entry per swapchain image: the in-flight-slot fence of the frame
			// that last rendered to that image (VK_NULL_HANDLE if none). Because the
			// swapchain has more images than frame slots, and per-image resources
			// (command buffer, descriptor pools, vertex buffers) are reused by image
			// index, we must wait on the previous user of an image before recording
			// into it again — otherwise (notably under MAILBOX on AMD) we reset/
			// overwrite resources the GPU is still reading. Not a Handle/owned fence,
			// just a reference into inFlightFences.
			std::vector<VkFence> imagesInFlight;

			// Deferred deletion queue for buffers that may still be in use by GPU
			std::vector<DeferredDeletion> deferredDeletions;
			std::vector<PendingUpload> pendingUploads;

			float fogDistance;
			Vector3 fogColor;

			Matrix4 projectionMatrix;
			Matrix4 viewMatrix;
			Matrix4 projectionViewMatrix;

			Plane3 frustrum[6];

			Vector4 drawColorAlphaPremultiplied;
			bool legacyColorPremultiply;

			unsigned int lastTime;
			float lastDt{0.0f};
			std::uint32_t frameNumber = 0;
			uint32_t lastSwapchainGeneration{0};

			// Counts consecutive per-frame queue-submit failures so a persistent
			// failure surfaces as an error instead of spamming the log every frame.
			int consecutiveSubmitFailures{0};

			bool duringSceneRendering;
			bool renderingMirror;

			std::unique_ptr<VulkanMapRenderer> mapRenderer;
			std::unique_ptr<VulkanModelRenderer> modelRenderer;
			std::unique_ptr<VulkanSpriteRenderer> spriteRenderer;
			std::unique_ptr<VulkanLongSpriteRenderer> longSpriteRenderer;
			std::unique_ptr<VulkanGlareRenderer> glareRenderer;
			std::unique_ptr<VulkanImageRenderer> imageRenderer;
			// The VR wrist panel's 2D layer, made on first use. 2D calls go to it
			// between SetVRCanvas(1) and SetVRCanvas(0).
			std::unique_ptr<VulkanImageRenderer> wristImageRenderer;
			int vrCanvas = 0;
			bool wristCanvasUsed = false; // something was queued for it this frame
			VulkanImageRenderer* Canvas2D() const {
				if (vrCanvas == 1 && wristImageRenderer)
					return wristImageRenderer.get();
				if ((vrCanvas == 2 || vrCanvas == 3) && eyeImageRenderers[vrCanvas - 2])
					return eyeImageRenderers[vrCanvas - 2].get();
				if ((vrCanvas == 4 || vrCanvas == 5) && eyeComfortRenderers[vrCanvas - 4])
					return eyeComfortRenderers[vrCanvas - 4].get();
				return imageRenderer.get();
			}
			std::unique_ptr<VulkanWaterRenderer> waterRenderer;
			std::unique_ptr<VulkanFlatMapRenderer> flatMapRenderer;
			std::unique_ptr<VulkanShadowMapRenderer> shadowMapRenderer;
			std::unique_ptr<VulkanMapShadowRenderer> mapShadowRenderer;
			std::unique_ptr<VulkanAmbientShadowRenderer> ambientShadowRenderer;
			// The map's clearance, which hides the dynamic lights behind it
			std::unique_ptr<VulkanMapOccupancy> mapOccupancy;
			std::unique_ptr<VulkanRadiosityRenderer> radiosityRenderer;
			std::unique_ptr<VulkanFramebufferManager> framebufferManager;
			Handle<VulkanProgramManager> programManager;
			Handle<VulkanModelManager> modelManager;
			std::unique_ptr<VulkanImageManager> imageManager;
			Handle<VulkanPipelineCache> pipelineCache;
			Handle<VulkanTemporaryImagePool> temporaryImagePool;
		std::unique_ptr<VulkanAutoExposureFilter> autoExposureFilter;
		std::unique_ptr<VulkanBloomFilter> bloomFilter;
		std::unique_ptr<VulkanFogFilter> fogFilter;
		std::unique_ptr<VulkanDepthOfFieldFilter> depthOfFieldFilter;
		std::unique_ptr<VulkanFXAAFilter> fxaaFilter;
		std::unique_ptr<VulkanTemporalAAFilter> temporalAAFilter;
		std::unique_ptr<VulkanCameraBlurFilter> cameraBlurFilter;
		std::unique_ptr<VulkanResampleBicubicFilter> resampleBicubicFilter;
		std::unique_ptr<VulkanCavityOutlineFilter> cavityOutlineFilter;
		std::unique_ptr<VulkanColorCorrectionFilter> colorCorrectionFilter;
		std::unique_ptr<VulkanLensFlareFilter> lensFlareFilter;
		// MSAA only: resolves the multisampled scene depth into the single-sample
		// R32F resolved-depth image that soft sprites, water and the depth-reading
		// post filters sample. Null when MSAA is off.
		std::unique_ptr<VulkanDepthResolveFilter> depthResolveFilter;
			/** Screen-space ambient occlusion (`r_ssao`), with the render targets */
			std::unique_ptr<VulkanScreenSpaceAmbientOcclusion> ambientOcclusion;

			Handle<VulkanImage> whiteImage; // 1x1 white image for solid color rendering

			// The frame's dynamic lights as the lit shaders read them
			std::unique_ptr<VulkanSceneLights> sceneLights;
			// How the scene's passes see the world, the same for every draw of a pass
			std::unique_ptr<VulkanSceneView> sceneView;

			// Sky gradient rendering
			VkPipeline skyPipeline;
			VkPipelineLayout skyPipelineLayout;
			Handle<VulkanBuffer> skyVertexBuffer;
			Handle<VulkanBuffer> skyIndexBuffer;

			// MultiplyScreenColor pipeline (fullscreen multiplicative tint)
			VkPipeline multiplyColorPipeline;
			VkPipelineLayout multiplyColorPipelineLayout;
			std::vector<Vector3> pendingMultiplyColors;

			int renderWidth;
			int renderHeight;

			// The 2D layer's size: the window, whatever the 3D is drawn at. In VR the
			// 3D is drawn at the headset's eye size while the UI keeps the window's
			// layout on the HUD panel, so the two stop being the same number.
			int uiWidth;
			int uiHeight;
			// With a headset the 2D layer has a layout of its own (vr_uiWidth, 16:9)
			// for the whole run, whatever the window, and headset panels are
			// drawn at uiPixelRatio times it and filtered down: the window no
			// longer decides how big or how sharp the text is in the headset.
			bool vrUILayout = false;
			int uiPixelRatio = 1;
			void SetupVRUILayout();

			// --- VR -----------------------------------------------------------
			// A headset frame draws each eye as a frame of its own: its own
			// command buffer, fence and frame slot. The post filters, the water and
			// the sprite batches all recycle their per-frame resources by slot on
			// every call, so two eyes recorded into one command buffer (or on one
			// slot) would reset descriptors the other eye's commands still use.
			// The 2D layer then goes into `hudImage` and on to the headset's HUD
			// panel, and the left eye is mirrored to the window.
			bool vrFrame = false;         // this frame belongs to the headset
			bool vrEyesWanted = false;    // ...and StartScene was given eyes to draw
			bool vrEyesRendered = false;  // both eyes went to the headset
			int vrEyeIndex = -1;          // eye being recorded, -1 outside eye passes
			bool keepSceneLists = false;  // see KeepsSceneLists
			// The eyes' size while a headset draws, {0, 0} otherwise: what
			// DesiredRenderExtent gives in place of the window's
			VkExtent2D vrRenderExtent{0, 0};

			// Multiview (vr_multiview): see IsMultiview.
			bool multiview = false;
			// Foveated rendering in a headset (vr_foveation), 0 for none: every
			// scene target set (the eyes', the scope's) is made with it.
			int foveation = 0;
			VkDescriptorSetLayout viewSetLayout = VK_NULL_HANDLE;
			VkDescriptorSetLayout emptySetLayout = VK_NULL_HANDLE;
			VkDescriptorPool viewSetPool = VK_NULL_HANDLE;
			static constexpr int kViewSetSlots = 3;
			Handle<VulkanBuffer> viewSetBuffers[kViewSetSlots];
			VkDescriptorSet viewSets[kViewSetSlots]{};
			void CreateViewSets();
			void DestroyViewSets();
			/** Fill the current slot's per-eye data for the two eyes; the scene is
			 * then drawn with eye 0's matrices and each vertex is moved into eye
			 * `gl_ViewIndex` by `correction`. */
			void WriteViewSet(const client::SceneDefinition& eye0, const client::SceneDefinition& eye1);
			/** Multiview: the two eyes of the frame being recorded (the scene is
			 * drawn from [0]; the post chain runs from each). */
			client::SceneDefinition vrMultiviewEyes[2];
			/** Widen the culling frustum (built for eye 0) to cover `other` too:
			 * its right plane is the other eye's, the top and bottom the looser. */
			void WidenFrustrumTo(const client::SceneDefinition& other);
			/** One filter of the post chain, from `in` to `out` (false: it drew
			 * nothing). `layers`: it can take both eyes' layers at once. */
			struct PostStep {
				bool layers;
				std::function<bool(VkCommandBuffer, VulkanImage*, VulkanImage*)> run;
			};
			std::vector<PostStep> BuildPostSteps(bool eyePass);
			/** Multiview: the chain for both eyes, ending in each eye's output. */
			void RecordMultiviewPostChain(VkCommandBuffer cmd);
			/** Multiview: make eye `eye` the one being recorded (its view, slot). */
			void SetMultiviewEye(int eye);
			/** Copy one layer of `src` into one layer of `dst`; both in
			 * SHADER_READ_ONLY before and after (dst's layer may be undefined before). */
			void CopyImageLayer(VkCommandBuffer cmd, VulkanImage* src, uint32_t srcLayer,
			                    VulkanImage* dst, uint32_t dstLayer);
			client::SceneDefinition vrHeadSceneDef;
			// [2] is the scope view's (see SetVRScopeView).
			VkCommandBuffer vrEyeCommandBuffers[3]{VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
			VkFence vrEyeFences[3]{VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
			// The view being recorded is the last of the frame: it clears the
			// scene lists the others keep.
			bool vrViewLast = false;

			// A rifle scope's view. Rendered after the eyes, square and small, in a
			// framebuffer manager of its own (swapped in for the pass), slot 2;
			// without water, whose renderer holds the eye-sized manager's images.
			// Its picture is blitted to an sRGB image (the encode) and copied to a
			// UNORM one, which the 2D layer samples and decodes like any texture.
			bool vrScopeWanted = false;
			bool vrScopePass = false;
			bool vrScopeRendered = false;
			client::SceneDefinition::VREye vrScopeView{};
			std::unique_ptr<VulkanFramebufferManager> scopeFramebufferManager;
			Handle<VulkanImage> scopeSrgbImage;
			// vr_debugScope: the scope's picture read back, to log how bright it is
			Handle<VulkanBuffer> scopeDebugReadback;
			bool scopeDebugPending = false;
			int scopeDebugFrames = 0;
			Handle<VulkanImage> scopeImage;
			Handle<VulkanImageWrapper> scopeImageWrapper;
			void EnsureScopeTargets(int size);
			// Each eye's finished picture, kept until Flip. What the client pins to
			// places in the world (names, pings, damage numbers) is drawn over it
			// there, per eye and so at its own depth, because the 2D layer is drawn
			// after the scene; only then does it go to the headset. The left one is
			// also the window's mirror.
			Handle<VulkanImage> vrEyeImages[2];
			VkFramebuffer vrEyeFramebuffers[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
			VkRenderPass eyeOverlayRenderPass = VK_NULL_HANDLE;
			std::unique_ptr<VulkanImageRenderer> eyeImageRenderers[2];
			bool eyeCanvasUsed[2] = {false, false};
			// What only the headset shows, over each eye after the window's
			// preview was taken (SetVRCanvas 4 and 5)
			std::unique_ptr<VulkanImageRenderer> eyeComfortRenderers[2];
			bool eyeComfortUsed[2] = {false, false};
			// The window's preview: the mirrored eye with its labels, without
			// the comfort layer, and with the wrist panel drawn where that eye
			// sees it (the runtime adds the panels to the headset's own view)
			Handle<VulkanImage> vrMirrorImage;
			VkFramebuffer vrMirrorFramebuffer = VK_NULL_HANDLE;
			std::unique_ptr<VulkanImageRenderer> mirrorImageRenderer;
			void EnsureVRMirrorImage(uint32_t width, uint32_t height);
			/** Copies `eye` (TRANSFER_SRC) into the preview and draws the wrist
			 * panel over it when `wristDrawn`; the preview is left TRANSFER_SRC. */
			void RecordVRMirror(VkCommandBuffer cmd, uint32_t frameIndex, VulkanImage& eye, int eyeIndex,
			                    bool wristDrawn);
			void EnsureVREyeImage(int eye, uint32_t width, uint32_t height);

			// Holding the headset's frame rate (vr_dynamicResolution). The eyes are
			// drawn at a fraction of their full size, stepped by how long the GPU
			// takes over them. Timed with timestamps around each command buffer's
			// own work and added up: one span per eye drawn one at a time, one for
			// both in multiview. Not from the left eye's start to the right eye's
			// end, which counts the GPU waiting while the CPU records the right eye.
			// Two sets, alternating, so a set is read two frames after it was
			// written, when it is done; a set is 2 spans of 2 timestamps.
			VkQueryPool vrTimestampPool = VK_NULL_HANDLE;
			bool vrTimestampsOk = false;
			float vrTimestampPeriodNs = 0.0F;
			uint64_t vrTimestampMask = ~0ull;
			int vrTimestampSet = 0;
			// Per set, a bit for each span opened (low two bits) and closed (next two).
			uint32_t vrTimestampSpans[2] = {0, 0};
			int vrTimestampOpen = -1; // the span the recording command buffer opened
			float vrGpuMs = 0.0F;
			float vrOverTime = 0.0F, vrUnderTime = 0.0F, vrHoldTime = 0.0F;
			int vrResolutionLevel = 0; // 0: full size
			float VRResolutionScale() const;
			void RecordVRTimestamp(VkCommandBuffer cmd, bool start);
			void UpdateVRResolution(float budgetMs, float dt);
			/** The 2D layout of an eye's overlay: half the eye's pixels, drawn at
			 * 2x, so text there is the size and sharpness of the panels'. */
			VkExtent2D EyeCanvasLayout(int eye) const;
			void DiscardEyeOverlays();
			Handle<VulkanImage> hudImage;
			VkRenderPass hudRenderPass = VK_NULL_HANDLE;
			VkFramebuffer hudFramebuffer = VK_NULL_HANDLE;
			Handle<VulkanImage> wristImage;
			VkFramebuffer wristFramebuffer = VK_NULL_HANDLE;

			bool IsHeadsetFrame() const;
			void EnsureWristTarget();
			/** Draws the wrist canvas and copies it to the headset; false if the
			 * panel is not up this frame. */
			bool RecordWristPanel(VkCommandBuffer cmd, uint32_t frameIndex);
			void EnsureVRResources();
			void DestroyVRResources();
			void EnsureHudTarget();
			/** Resizes the 3D targets; waits for the GPU, so only on a real change. */
			void EnsureRenderSize(int width, int height);
			void RenderVREyes();
			void RecordVREyeOutput(VkCommandBuffer commandBuffer, VulkanImage* finalImage);
			void FlipVRFrame();

			void BuildProjectionMatrix();
			void BuildView();
			void BuildFrustrum();

			void EnsureInitialized();
			void EnsureSceneStarted();
			void EnsureSceneNotStarted();

			void InitializeVulkanResources();
			void CreateRenderPass();
			void CreateFramebuffers();
			void CreateCommandBuffers();
			void CleanupVulkanResources();
			void RecreateSwapchainDependencies();

			/** The render size the window and `r_scale` ask for now */
			VkExtent2D DesiredRenderExtent() const;

			/** Rebuilds what is sized to the render size, at the size asked for:
			 * between frames, with the device idle */
			void RecreateRenderTargets();

			/**
			 * Begins a frame on the swapchain: brings it, and what the renderer
			 * sizes from it, in line with the window, then acquires an image.
			 * Returns UINT32_MAX when the frame cannot be drawn: the window is
			 * minimised, or the swapchain went out of date; it is then dropped,
			 * and a minimised window waits about a frame rather than spin.
			 */
			uint32_t AcquireFrameImage();

			/** Sets the viewport and scissor every pass drawing into the scene's
			 * targets uses: the render size, flipped so the scene comes out the way
			 * up GL's does */
			void SetSceneViewport(VkCommandBuffer commandBuffer) const;

			/** Whether the scene's views start with a depth pre-pass, as GL's: of the
			 * map at `r_depthPrepass`, of the models too for SSAO, which reads the
			 * depth of everything opaque before anything is lit */
			bool UsesDepthPrepass() const;
			bool UsesFullDepthPrepass() const;
			/** The scene pass resolves its colour itself and stores no depth
			 * (r_resolveInPass, where nothing reads the depth after the pass). */
			bool SceneResolvesInPass();

			/** Makes the screen-space ambient occlusion for the render targets, if
			 * `r_ssao` asks for it, and points the scene views at it: with the
			 * device idle */
			void ConfigureAmbientOcclusion();

			/** Records `view`'s ambient occlusion from its depth pre-pass, between the
			 * pre-pass and the lit pass, leaving the depth an attachment again */
			void RenderAmbientOcclusion(VkCommandBuffer commandBuffer, VulkanSceneView::View view);

			/**
			 * Records the depth pre-pass of the view being drawn into `framebuffer`:
			 * the scene pass, clearing to `clearValues`, writing the depth of the map
			 * (and the models' with `withModels`) and nothing else. The lit pass
			 * then continues it (`GetContinueRenderPass`), meeting that depth.
			 */
			void RecordDepthPrepass(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer,
			                        const VkClearValue (&clearValues)[2], bool withModels);

			/** Whether this frame already found the swapchain unusable, so that
			 * its 2D pass does not try (and wait) again */
			bool frameImageUnavailable{false};

			uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
			uint32_t FindMemoryTypeWithFallback(uint32_t typeFilter,
			                                    VkMemoryPropertyFlags preferred,
			                                    VkMemoryPropertyFlags fallback);

			void RecordCommandBuffer(uint32_t imageIndex);
			/** The post-process chain on the resolved scene; returns the final image
			 * (in SHADER_READ_ONLY). The two scratch images are handed back so they
			 * live until the commands that use them are recorded. */
			VulkanImage* RecordPostChain(VkCommandBuffer commandBuffer,
			                             Handle<VulkanImage> offscreenColor, bool eyePass,
			                             Handle<VulkanImage>& ppTempImage,
			                             Handle<VulkanImage>& ppTempImage2);
			void Record2DCommandBuffer(uint32_t imageIndex);

			// Sky gradient rendering
			void CreateSkyPipeline();
			void DestroySkyPipeline();
			void RenderSky(VkCommandBuffer commandBuffer);

			// MultiplyScreenColor rendering
			void CreateMultiplyColorPipeline();
			void DestroyMultiplyColorPipeline();
			void RenderMultiplyColors(VkCommandBuffer commandBuffer);

			// Debug line rendering
			VkPipeline debugLinePipeline;
			VkPipelineLayout debugLinePipelineLayout;
			void CreateDebugLinePipeline();
			void DestroyDebugLinePipeline();
			void RenderDebugLines(VkCommandBuffer commandBuffer);

			/** Draws every model flagged `xray` again where the world hides it, in the
			 * colour the model asked for. The world marks the stencil buffer as it is
			 * drawn, which is what tells this pass where "behind the world" is. Must be
			 * called inside a render pass that still has the scene's colour and
			 * depth+stencil attached, and before the model list is cleared. */
			void RenderXRayPass(VkCommandBuffer commandBuffer);

			// Deferred deletion queue management
			void ProcessDeferredDeletions();
			void FlushPendingUploads();

			// Handles the result of a per-frame queue submit. Logs the first
			// failure, and after several consecutive failures raises an error so
			// the user sees a dialog rather than an endless silent log loop.
			// Resets on the first success.
			void HandleSubmitResult(VkResult result, const char* where);

			// Waits for the previous frame that rendered to currentImageIndex (if any)
			// and records the current slot's fence as that image's in-flight fence.
			// Call right after acquiring, before re-recording per-image resources.
			void WaitForImageInFlight();

		protected:
			~VulkanRenderer();

		public:
			/**
			 * The front of the depth range the first-person view's models are drawn
			 * into (`ModelRenderParam::depthHack`), so that they stay in front of the
			 * world. The world only reaches it within a hair of the near plane.
			 */
			static constexpr float kFirstPersonDepthEnd = 0.1F;

			VulkanRenderer(Handle<gui::SDLVulkanDevice> device);

			bool BoxFrustrumCull(const AABB3& box);
			bool SphereFrustrumCull(const Vector3& center, float radius);

			// IRenderer implementation
			void Init() override;
			void Shutdown() override;

			Handle<client::IImage> RegisterImage(const char* filename) override;
			Handle<client::IModel> RegisterModel(const char* filename) override;
			void ReloadResources(const ResourceChanges&) override;

			Handle<client::IImage> CreateImage(Bitmap& bitmap) override;
			Handle<client::IModel> CreateModel(VoxelModel& model) override;

			void SetGameMap(stmp::optional<client::GameMap&>) override;

			void SetFogDistance(float) override;
			void SetFogColor(Vector3) override;

			/** The factor the world's lighting but the sun's, the fog and the sky are
			 * drawn with, in `[0, 1]`. */
			float GetDaylight() const { return sceneDef.daylight; }

			/** The factor the sun's light is drawn with. At `0` the sun casts no light
			 * and no shadow. */
			float GetSunlight() const { return sceneDef.sunlight; }

			/** The Fog Colour as set: the colour of the fog and the sky in full daylight. */
			Vector3 GetFullDaylightFogColor() const { return fogColor; }

			/** The colour the fog and the sky are drawn in: the Fog Colour times the
			 * daylight. */
			Vector3 GetFogColor() const { return fogColor * GetDaylight(); }

			/** The colour solid geometry fades to with distance: none when a fog filter
			 * draws the fog over it afterwards. */
			Vector3 GetFogColorForSolidPass();

			/** `GetFogColorForSolidPass` in full daylight: the sky's light, which the
			 * lighting shaders scale by the daylight themselves. */
			Vector3 GetFullDaylightFogColorForSolidPass();

			/** The sun's and the sky's light of the frame, as the lit shaders take it. */
			VulkanSceneLights::SunSky GetSunSky();

			/** How the pass under way sees the world, as `VulkanSceneView` holds it */
			VulkanSceneView::Parameters GetSceneViewParameters();

			float GetFogDistance() { return fogDistance; }

			const client::SceneDefinition& GetSceneDef() const { return sceneDef; }
			const std::vector<VulkanDynamicLight>& GetDynamicLights() const { return lights; }

			// Canonical sun direction (points TOWARD the sun), matching the lens
			// flare, water and lit shaders. Single source of truth so the shadow
			// projection is derived from the sun rather than a duplicated constant.
			Vector3 GetSunDirection() const { return MakeVector3(0.0F, -1.0F, -1.0F).Normalize(); }

			const Matrix4& GetProjectionViewMatrix() const { return projectionViewMatrix; }
			const Matrix4& GetViewMatrix() const { return viewMatrix; }
			const Matrix4& GetProjectionMatrix() const { return projectionMatrix; }
			VulkanImage* GetWhiteImage() { return whiteImage.GetPointerOrNull(); }

			/** Binds the view the pass under way sees the world through, the main one or
			 * the water's mirrored one, as set `VulkanSceneView::Set` of `layout`. */
			void BindSceneView(VkCommandBuffer, VkPipelineLayout layout);

			/** The layout of the set `BindSceneView` binds */
			VkDescriptorSetLayout GetSceneViewSetLayout() const;

			/** The frame's dynamic lights, which the lit pipelines bind as set 2 */
			VulkanSceneLights& GetSceneLights() { return *sceneLights; }
			bool IsRenderingMirror() const { return renderingMirror; }
			/** The size the scene is drawn and post-processed at: the window's,
			 * scaled by `r_scale` as GL does. The final blit scales it to the
			 * swapchain, by `r_scaleFilter`. The 2D layer and `ScreenWidth` /
			 * `ScreenHeight` stay in the window's units. */
			int GetRenderWidth() const { return renderWidth; }

			/** The settings latched for the renderer's life (`VulkanSettings`) */
			VulkanSettings& GetSettings() { return settings; }
			int GetRenderHeight() const { return renderHeight; }
			/** The surface the 2D layer is being drawn onto right now. */
			VkExtent2D GetUIExtent() const;
			/** Headset eye pass being recorded, or -1. */
			int GetVREyeIndex() const { return vrEyeIndex; }
			/** Whether the scene's queued models, sprites and lights are kept after
			 * the pass being recorded: a headset's eye passes but the last draw the
			 * same lists. */
			bool KeepsSceneLists() const { return keepSceneLists; }
			/** The frame slots per-frame resources are kept for (GetCurrentFrameIndex):
			 * a frame in flight's each, and a headset's eye and scope passes'
			 * (VulkanFramesInFlight.h). */
			std::size_t GetFrameSlotCount() const;
			/** The peak the lit map and models roll off toward (vr_brightness), the
			 * VOXEL_PEAK of their shaders; 0 outside a headset run. */
			float GetVRVoxelPeak() const;

			void StartScene(const client::SceneDefinition& def) override;

			void AddDebugLine(Vector3 a, Vector3 b, Vector4 color) override;

			void AddSprite(client::IImage&, Vector3 center, float radius, float rotation) override;
			void AddLongSprite(client::IImage&, Vector3 p1, Vector3 p2, float radius) override;
			void AddGlare(client::IImage&, const client::GlareParam&) override;

			void AddLight(const client::DynamicLightParam& light) override;

			void RenderModel(client::IModel&, const client::ModelRenderParam& param) override;

			void EndScene() override;

			void MultiplyScreenColor(Vector3) override;

			void SetColor(Vector4) override;
			void SetColorAlphaPremultiplied(Vector4) override;

			void DrawImage(stmp::optional<client::IImage&>, const Vector2& outTopLeft) override;
			void DrawImage(stmp::optional<client::IImage&>, const AABB2& outRect) override;
			void DrawImage(stmp::optional<client::IImage&>, const Vector2& outTopLeft, const AABB2& inRect) override;
			void DrawImage(stmp::optional<client::IImage&>, const AABB2& outRect, const AABB2& inRect) override;
			void DrawImage(stmp::optional<client::IImage&>, const Vector2& outTopLeft, const Vector2& outTopRight,
			               const Vector2& outBottomLeft, const AABB2& inRect) override;

			void DrawFilledTriangle(const Vector2& v0, const Vector2& v1, const Vector2& v2) override;
			void DrawFilledRectFade(float x0, float y0, float x1, float y1, Vector4 color0,
			                        Vector4 color1, bool horizontal) override;

			void DrawFlatGameMap(const AABB2& outRect, const AABB2& inRect) override;
			void DrawFlatGameMap(const Vector2& outTopLeft, const Vector2& outTopRight,
			                     const Vector2& outBottomLeft, const AABB2& inRect) override;

			void BeginClippingCircle(const Vector2& center, float radius) override;
			void EndClippingCircle() override;
			void BeginClippingRect(const AABB2& outRect) override;
			void EndClippingRect() override;

			void FrameDone() override;
			void Flip() override;

			Handle<Bitmap> ReadBitmap() override;

			float ScreenWidth() override;
			float ScreenHeight() override;
			bool SetVRCanvas(int canvas) override;
			float GetUIPixelRatio() override;
			bool GetVRFrameStats(float& gpuMilliseconds, float& resolutionScale) override;
			void SetVRScopeView(const Vector3& origin, const Vector3& right, const Vector3& up,
			                    const Vector3& forward, float fov) override;
			client::IImage* GetVRScopeImage() override;

			client::RendererInfo GetRendererInfo() override;

			// IGameMapListener implementation
			void GameMapChanged(int x, int y, int z, client::GameMap*) override;

			// Vulkan-specific methods
			VulkanMapRenderer* GetMapRenderer() { return mapRenderer.get(); }
			VulkanModelRenderer* GetModelRenderer() { return modelRenderer.get(); }
			VulkanSpriteRenderer* GetSpriteRenderer() { return spriteRenderer.get(); }
			VulkanWaterRenderer* GetWaterRenderer() { return waterRenderer.get(); }
			VulkanShadowMapRenderer* GetShadowMapRenderer() { return shadowMapRenderer.get(); }
			VulkanMapShadowRenderer* GetMapShadowRenderer() { return mapShadowRenderer.get(); }
			VulkanAmbientShadowRenderer* GetAmbientShadowRenderer() { return ambientShadowRenderer.get(); }
			VulkanRadiosityRenderer* GetRadiosityRenderer() { return radiosityRenderer.get(); }
			VulkanFramebufferManager* GetFramebufferManager() { return framebufferManager.get(); }
			VkPipelineCache GetPipelineCache() const;

		// Program registration API
		VulkanProgram* RegisterProgram(const std::string& name);
		VulkanShader* RegisterShader(const std::string& name);
			Handle<gui::SDLVulkanDevice> GetDevice() { return device; }
			VkRenderPass GetRenderPass() const { return renderPass; } // Swapchain render pass (for UI)
			VkRenderPass GetOffscreenRenderPass() const; // Offscreen render pass (for 3D)
			uint32_t GetCurrentFrameIndex() const { return currentFrameSlot; }

			/** vr_multiview: both eyes drawn in one pass. Fixed for the renderer's
			 * life (every pipeline is built for it). */
			bool IsMultiview() const { return multiview; }
			/** Multiview: eye `i`'s view this frame (valid while it is recorded). */
			const client::SceneDefinition& GetMultiviewEye(int i) const { return vrMultiviewEyes[i]; }
			/** The eyes' views as a set of their own (ZsViews in the .mv shaders),
			 * where a pipeline has a set free for it: the sky's and the lines' 0,
			 * the water's 1 (see WriteViewSet). Null without multiview. */
			VkDescriptorSetLayout GetViewSetLayout() const { return viewSetLayout; }
			/** Multiview: bind the current frame slot's per-eye set at `setIndex`.
			 * Call after binding a pipeline whose layout appends GetViewSetLayout().
			 * Does nothing otherwise. */
			void BindViewSet(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t setIndex);
			/** Appends the view set layout to `layouts` in multiview; returns the
			 * index it went in at (or the count, unused, otherwise). */
			uint32_t AppendViewSetLayout(std::vector<VkDescriptorSetLayout>& layouts) const;
			/** Multiview: frame slot `slot`'s eye views (ZsViews), for the sets
			 * that carry them as a binding (the scene view's, the sprites'); null
			 * without multiview. */
			VulkanBuffer* GetViewSetBuffer(std::size_t slot) const {
				return multiview ? viewSetBuffers[std::min<std::size_t>(slot, kViewSetSlots - 1)]
				                     .GetPointerOrNull()
				                 : nullptr;
			}

			/** A set layout with nothing in it: pads a pipeline layout whose optional
			 * set is absent, so the view set sits at the index its shader expects. */
			VkDescriptorSetLayout GetEmptySetLayout() const { return emptySetLayout; }
			// Monotonic frame counter (incremented each FrameDone). Lets per-frame
			// resources distinguish multiple uses within one frame from frame-to-frame
			// reuse of the same in-flight slot.
			std::uint32_t GetFrameNumber() const { return frameNumber; }

			// Queue a buffer for deferred deletion (will be deleted after GPU is done with it)
			void QueueBufferForDeletion(Handle<VulkanBuffer> buffer);

			// Temporary image pool for render target reuse
			VulkanTemporaryImagePool* GetTemporaryImagePool() { return temporaryImagePool.GetPointerOrNull(); }
		};

	} // namespace draw
} // namespace spades
