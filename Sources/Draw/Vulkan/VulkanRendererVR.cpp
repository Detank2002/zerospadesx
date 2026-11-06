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

// The headset half of VulkanRenderer. A headset frame goes:
//
//   StartScene   no window image is acquired; the 3D targets are sized to the
//                headset's eyes.
//   EndScene     RenderVREyes: each eye is recorded by the ordinary
//                RecordCommandBuffer as a frame of its own (own command
//                buffer, fence and frame slot) and ends in a blit into the
//                headset's swapchain image for that eye.
//   Flip         FlipVRFrame: the 2D layer is drawn into `hudImage`, copied
//                to the headset's HUD panel, and the window gets the left eye
//                (or the panel, when there is no 3D) as a mirror.
//
// Nothing here runs for a window-only frame, which takes the paths in
// VulkanRenderer.cpp exactly as before.

#include <cmath>
#include <cstring>
#include "VulkanRenderer.h"
#include "VulkanBuffer.h"
#include "VulkanProgram.h"

#include <algorithm>
#include <vector>

#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanImageRenderer.h"
#include "VulkanImageWrapper.h"
#include "VulkanLongSpriteRenderer.h"
#include "VulkanModelRenderer.h"
#include "VulkanSpriteRenderer.h"
#include "VulkanTemporaryImagePool.h"
#include "VulkanWaterRenderer.h"
#include <Client/VRState.h>
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/Exception.h>
#include <Gui/SDLVulkanDevice.h>
#if USE_OPENXR
#include <VR/OpenXRSystem.h>
#endif

// The headset's 2D layer: laid out at vr_uiWidth by 9/16 of it, whatever the
// window, and drawn at vr_uiSupersample times that before being filtered down.
DEFINE_SPADES_SETTING(vr_uiWidth, "1280");
SPADES_SETTING(vr_debugScope);
// Which eye the monitor shows (0 left, 1 right).
DEFINE_SPADES_SETTING(vr_mirrorEye, "0");
DEFINE_SPADES_SETTING(vr_uiSupersample, "2");
// Draw the eyes smaller when the GPU cannot keep up with the headset.
DEFINE_SPADES_SETTING(vr_dynamicResolution, "1");
// The brightest a lit block or model gets in the headset, 0.5 to 1 (1: as on a
// monitor, clipping at white). See GetVRVoxelPeak.
DEFINE_SPADES_SETTING(vr_brightness, "0.75");

namespace spades {
	namespace draw {

		namespace {
			void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from,
			                  VkImageLayout to, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
			                  VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
				VkImageMemoryBarrier b{};
				b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				b.oldLayout = from;
				b.newLayout = to;
				b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.image = image;
				b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS};
				b.srcAccessMask = srcAccess;
				b.dstAccessMask = dstAccess;
				vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
			}

			void Blit(VkCommandBuffer cmd, VkImage src, int32_t sx0, int32_t sy0, int32_t sx1,
			          int32_t sy1, VkImage dst, int32_t dw, int32_t dh) {
				VkImageBlit r{};
				r.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
				r.srcOffsets[0] = {sx0, sy0, 0};
				r.srcOffsets[1] = {sx1, sy1, 1};
				r.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
				r.dstOffsets[0] = {0, 0, 0};
				r.dstOffsets[1] = {dw, dh, 1};
				vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
				               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r, VK_FILTER_LINEAR);
			}
		} // namespace

		float VulkanRenderer::GetVRVoxelPeak() const {
			// The lit map and models only: the sky, water and effects are as
			// they were. Testers found white blocks filling the view, a snow
			// map's ground, hard on the eyes; on a monitor they fill far less.
			if (!vrUILayout)
				return 0.0F;
			const float peak = std::max(0.5F, std::min(1.0F, (float)vr_brightness));
			return peak >= 0.999F ? 0.0F : peak;
		}

		void VulkanRenderer::SetupVRUILayout() {
#if USE_OPENXR
			// For the whole run, headset or not this frame: the menus lay
			// themselves out once, so the layout cannot follow the session.
			if (!vr::GetOpenXRSystem())
				return;
			vrUILayout = true;
			uiWidth = std::max(640, std::min(3840, static_cast<int>(vr_uiWidth)));
			uiHeight = uiWidth * 9 / 16;
			uiPixelRatio = std::max(1, std::min(3, static_cast<int>(vr_uiSupersample)));
			SPLog("VR 2D layer: laid out at %dx%d, drawn at %dx that", uiWidth, uiHeight,
			      uiPixelRatio);
#endif
		}

		bool VulkanRenderer::IsHeadsetFrame() const {
#if USE_OPENXR
			vr::OpenXRSystem* xr = vr::GetOpenXRSystem();
			return xr && xr->GetFrameState().running;
#else
			return false;
#endif
		}

		void VulkanRenderer::EnsureVRResources() {
			if (vrEyeFences[0] != VK_NULL_HANDLE)
				return;
			VkDevice dev = device->GetDevice();

			VkCommandBufferAllocateInfo ai{};
			ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			ai.commandPool = device->GetCommandPool();
			ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			ai.commandBufferCount = 3;
			if (vkAllocateCommandBuffers(dev, &ai, vrEyeCommandBuffers) != VK_SUCCESS)
				SPRaise("Failed to allocate the VR eye command buffers");

			VkFenceCreateInfo fi{};
			fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			for (auto& f : vrEyeFences)
				if (vkCreateFence(dev, &fi, nullptr, &f) != VK_SUCCESS)
					SPRaise("Failed to create the VR eye fences");

			// Timestamps, if the graphics queue has them.
			VkPhysicalDeviceProperties props{};
			vkGetPhysicalDeviceProperties(device->GetPhysicalDevice(), &props);
			uint32_t families = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device->GetPhysicalDevice(), &families, nullptr);
			std::vector<VkQueueFamilyProperties> queues(families);
			vkGetPhysicalDeviceQueueFamilyProperties(device->GetPhysicalDevice(), &families,
			                                         queues.data());
			const uint32_t family = device->GetGraphicsQueueFamily();
			if (family < families && queues[family].timestampValidBits > 0 &&
			    props.limits.timestampPeriod > 0.0F) {
				VkQueryPoolCreateInfo qi{};
				qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
				qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
				qi.queryCount = 8;
				if (vkCreateQueryPool(dev, &qi, nullptr, &vrTimestampPool) == VK_SUCCESS) {
					vrTimestampsOk = true;
					vrTimestampPeriodNs = props.limits.timestampPeriod;
					const uint32_t bits = queues[family].timestampValidBits;
					vrTimestampMask = bits >= 64 ? ~0ull : ((1ull << bits) - 1ull);
				}
			}
			if (!vrTimestampsOk)
				SPLog("[VR] No GPU timestamps: the eyes stay at full size");
		}

		void VulkanRenderer::SetVRScopeView(const Vector3& origin, const Vector3& right,
		                                    const Vector3& up, const Vector3& forward, float fov) {
			if (!IsHeadsetFrame())
				return;
			vrScopeWanted = true;
			vrScopeView.origin = origin;
			vrScopeView.axis[0] = right;
			vrScopeView.axis[1] = up;
			vrScopeView.axis[2] = forward;
			vrScopeView.fovX = vrScopeView.fovY = fov;
		}

		client::IImage* VulkanRenderer::GetVRScopeImage() {
			if (!vrScopeRendered || !scopeImageWrapper)
				return nullptr;
			return scopeImageWrapper.GetPointerOrNull();
		}

		void VulkanRenderer::EnsureScopeTargets(int size) {
			if (scopeFramebufferManager && scopeImage &&
			    scopeImage->GetWidth() == static_cast<uint32_t>(size))
				return;
			vkDeviceWaitIdle(device->GetDevice());
			const uint32_t s = static_cast<uint32_t>(size);
			// Foveated as the eyes' are: the two render passes must match for the
			// eyes' pipelines to draw the scope
			scopeFramebufferManager = stmp::make_unique<VulkanFramebufferManager>(
			  device, settings, size, size, multiview ? 2 : 1, foveation, SceneResolvesInPass());
			// Sampled too, though it is only copied: every VulkanImage gets a view,
			// and a view needs a usage beyond the transfers (validation)
			scopeSrgbImage = Handle<VulkanImage>(
			  new VulkanImage(device, s, s, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
			                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			                    VK_IMAGE_USAGE_SAMPLED_BIT,
			                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
			  false);
			scopeImage = Handle<VulkanImage>(
			  new VulkanImage(device, s, s, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
			                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
			  false);
			scopeImage->CreateSampler();
			scopeImageWrapper = Handle<VulkanImageWrapper>(
			  new VulkanImageWrapper(scopeImage, static_cast<float>(s), static_cast<float>(s)), false);
		}

		float VulkanRenderer::VRResolutionScale() const {
			static const float levels[] = {1.0F, 0.9F, 0.8F, 0.7F, 0.6F};
			return levels[std::max(0, std::min(4, vrResolutionLevel))];
		}

		bool VulkanRenderer::GetVRFrameStats(float& gpuMilliseconds, float& resolutionScale) {
			if (!vrTimestampsOk || vrGpuMs <= 0.0F || !IsHeadsetFrame())
				return false;
			gpuMilliseconds = vrGpuMs;
			resolutionScale = VRResolutionScale();
			return true;
		}

		void VulkanRenderer::RecordVRTimestamp(VkCommandBuffer cmd, bool start) {
			if (!vrTimestampsOk)
				return;
			// A span opens where an eye's command buffer starts and closes where
			// that buffer's last eye is handed out: in multiview the one buffer
			// opens as the left eye and closes after the right. The scope's view
			// (eye 2) is not counted, as before.
			const uint32_t set = static_cast<uint32_t>(vrTimestampSet) * 4u;
			if (start) {
				vrTimestampOpen = -1;
				if (vrEyeIndex != 0 && vrEyeIndex != 1)
					return;
				const uint32_t q = set + static_cast<uint32_t>(vrEyeIndex) * 2u;
				if (vrEyeIndex == 0)
					vrTimestampSpans[vrTimestampSet] = 0; // a new frame's set
				vkCmdResetQueryPool(cmd, vrTimestampPool, q, 2);
				vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vrTimestampPool, q);
				vrTimestampOpen = vrEyeIndex;
				vrTimestampSpans[vrTimestampSet] |= 1u << vrEyeIndex;
			} else if (vrTimestampOpen >= 0 && (vrEyeIndex == vrTimestampOpen || vrEyeIndex == 1)) {
				if (multiview && vrEyeIndex != 1)
					return; // the left eye's output, half way through the one buffer
				const uint32_t q = set + static_cast<uint32_t>(vrTimestampOpen) * 2u + 1u;
				vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vrTimestampPool, q);
				vrTimestampSpans[vrTimestampSet] |= 4u << vrTimestampOpen;
				vrTimestampOpen = -1;
			}
		}

		void VulkanRenderer::UpdateVRResolution(float budgetMs, float dt) {
			// Read the set written two frames ago: the right eye of that frame was
			// waited for (its fence) while recording the last frame's right eye.
			// Every span the set opened must have closed, or the frame is skipped.
			const uint32_t spans = vrTimestampsOk ? vrTimestampSpans[vrTimestampSet] : 0u;
			if (spans != 0 && (spans >> 2) == (spans & 3u)) {
				uint64_t t[4] = {0, 0, 0, 0};
				const uint32_t count = (spans & 2u) ? 4u : 2u;
				VkResult r = vkGetQueryPoolResults(
				  device->GetDevice(), vrTimestampPool, static_cast<uint32_t>(vrTimestampSet) * 4u,
				  count, sizeof(t), t, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
				if (r == VK_SUCCESS) {
					uint64_t busy = 0;
					for (uint32_t i = 0; i < 2; i++)
						if (spans & (1u << i))
							busy += ((t[i * 2 + 1] & vrTimestampMask) -
							         (t[i * 2] & vrTimestampMask)) &
							        vrTimestampMask;
					const float ms = static_cast<float>(busy) * vrTimestampPeriodNs * 1.0E-6F;
					if (ms > 0.0F && ms < 1000.0F)
						vrGpuMs = vrGpuMs <= 0.0F ? ms : vrGpuMs + (ms - vrGpuMs) * 0.1F;
				}
			}

			if (!(int)vr_dynamicResolution || !vrTimestampsOk || vrGpuMs <= 0.0F ||
			    budgetMs <= 0.0F) {
				vrResolutionLevel = 0;
				return;
			}

			// Down a step after half a second over 85% of the budget (the 2D layer
			// and the runtime want the rest); up a step after four seconds in which
			// the bigger size would have fitted in 72% of it (its cost goes with its
			// pixels). At least two seconds between steps: each rebuilds the render
			// targets, which is a stall.
			vrHoldTime += dt;
			const int top = 4;
			vrOverTime = vrGpuMs > budgetMs * 0.85F ? vrOverTime + dt : 0.0F;
			bool roomToGrow = false;
			if (vrResolutionLevel > 0) {
				const float now = VRResolutionScale();
				--vrResolutionLevel;
				const float bigger = VRResolutionScale();
				++vrResolutionLevel;
				const float predicted = vrGpuMs * (bigger * bigger) / (now * now);
				roomToGrow = predicted < budgetMs * 0.72F;
			}
			vrUnderTime = roomToGrow ? vrUnderTime + dt : 0.0F;

			if (vrHoldTime < 2.0F)
				return;
			if (vrOverTime > 0.5F && vrResolutionLevel < top) {
				++vrResolutionLevel;
			} else if (vrUnderTime > 4.0F && vrResolutionLevel > 0) {
				--vrResolutionLevel;
			} else {
				return;
			}
			vrHoldTime = vrOverTime = vrUnderTime = 0.0F;
			SPLog("[VR] GPU %.1fms of a %.1fms frame: eyes now drawn at %d%%", vrGpuMs, budgetMs,
			      static_cast<int>(VRResolutionScale() * 100.0F + 0.5F));
		}

		void VulkanRenderer::DestroyVRResources() {
			VkDevice dev = device ? device->GetDevice() : VK_NULL_HANDLE;
			if (dev == VK_NULL_HANDLE)
				return;
			vkDeviceWaitIdle(dev);
			if (vrEyeCommandBuffers[0] != VK_NULL_HANDLE) {
				vkFreeCommandBuffers(dev, device->GetCommandPool(), 3, vrEyeCommandBuffers);
				for (auto& cb : vrEyeCommandBuffers)
					cb = VK_NULL_HANDLE;
			}
			scopeImageWrapper = nullptr;
			scopeImage = nullptr;
			scopeSrgbImage = nullptr;
			scopeFramebufferManager.reset();
			for (auto& f : vrEyeFences) {
				if (f != VK_NULL_HANDLE)
					vkDestroyFence(dev, f, nullptr);
				f = VK_NULL_HANDLE;
			}
			if (vrTimestampPool != VK_NULL_HANDLE)
				vkDestroyQueryPool(dev, vrTimestampPool, nullptr);
			vrTimestampPool = VK_NULL_HANDLE;
			vrTimestampsOk = false;
			vrTimestampSpans[0] = vrTimestampSpans[1] = 0;
			vrTimestampOpen = -1;
			if (hudFramebuffer != VK_NULL_HANDLE)
				vkDestroyFramebuffer(dev, hudFramebuffer, nullptr);
			hudFramebuffer = VK_NULL_HANDLE;
			if (wristFramebuffer != VK_NULL_HANDLE)
				vkDestroyFramebuffer(dev, wristFramebuffer, nullptr);
			wristFramebuffer = VK_NULL_HANDLE;
			wristImage = nullptr;
			if (hudRenderPass != VK_NULL_HANDLE)
				vkDestroyRenderPass(dev, hudRenderPass, nullptr);
			hudRenderPass = VK_NULL_HANDLE;
			hudImage = nullptr;
			for (int e = 0; e < 2; e++) {
				if (vrEyeFramebuffers[e] != VK_NULL_HANDLE)
					vkDestroyFramebuffer(dev, vrEyeFramebuffers[e], nullptr);
				vrEyeFramebuffers[e] = VK_NULL_HANDLE;
				vrEyeImages[e] = nullptr;
			}
			if (vrMirrorFramebuffer != VK_NULL_HANDLE)
				vkDestroyFramebuffer(dev, vrMirrorFramebuffer, nullptr);
			vrMirrorFramebuffer = VK_NULL_HANDLE;
			vrMirrorImage = nullptr;
			if (eyeOverlayRenderPass != VK_NULL_HANDLE)
				vkDestroyRenderPass(dev, eyeOverlayRenderPass, nullptr);
			eyeOverlayRenderPass = VK_NULL_HANDLE;
		}

		void VulkanRenderer::EnsureRenderSize(int width, int height) {
			if (width <= 0 || height <= 0 || !framebufferManager)
				return;
			vrRenderExtent = VkExtent2D{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
			if (width == renderWidth && height == renderHeight)
				return;
			// Only when a headset session starts or stops, or the dynamic resolution
			// steps, so a full stall is fine.
			RecreateRenderTargets();
		}

		void VulkanRenderer::EnsureHudTarget() {
			VkDevice dev = device->GetDevice();
			VkFormat format = device->GetSwapchainImageFormat();

			if (hudRenderPass == VK_NULL_HANDLE) {
				// Compatible with the window's UI pass (same format, one sample), so
				// the image renderer's pipelines draw into either.
				VkAttachmentDescription color{};
				color.format = format;
				color.samples = VK_SAMPLE_COUNT_1_BIT;
				color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

				VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
				VkSubpassDescription subpass{};
				subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				subpass.colorAttachmentCount = 1;
				subpass.pColorAttachments = &ref;

				// In: last frame's copy out of this image. Out: this frame's copy.
				VkSubpassDependency deps[2]{};
				deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
				deps[0].dstSubpass = 0;
				deps[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
				deps[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				deps[1].srcSubpass = 0;
				deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
				deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
				deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

				VkRenderPassCreateInfo rpi{};
				rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
				rpi.attachmentCount = 1;
				rpi.pAttachments = &color;
				rpi.subpassCount = 1;
				rpi.pSubpasses = &subpass;
				rpi.dependencyCount = 2;
				rpi.pDependencies = deps;
				if (vkCreateRenderPass(dev, &rpi, nullptr, &hudRenderPass) != VK_SUCCESS)
					SPRaise("Failed to create the HUD render pass");
			}

			const uint32_t hudW = static_cast<uint32_t>(uiWidth * uiPixelRatio);
			const uint32_t hudH = static_cast<uint32_t>(uiHeight * uiPixelRatio);
			if (hudImage && hudImage->GetWidth() == hudW && hudImage->GetHeight() == hudH)
				return;

			vkDeviceWaitIdle(dev);
			if (hudFramebuffer != VK_NULL_HANDLE)
				vkDestroyFramebuffer(dev, hudFramebuffer, nullptr);
			hudFramebuffer = VK_NULL_HANDLE;

			hudImage = Handle<VulkanImage>(
			  new VulkanImage(device, hudW, hudH, format, VK_IMAGE_TILING_OPTIMAL,
			                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			                    VK_IMAGE_USAGE_SAMPLED_BIT,
			                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
			  false);

			VkImageView view = hudImage->GetImageView();
			VkFramebufferCreateInfo fi{};
			fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fi.renderPass = hudRenderPass;
			fi.attachmentCount = 1;
			fi.pAttachments = &view;
			fi.width = hudW;
			fi.height = hudH;
			fi.layers = 1;
			if (vkCreateFramebuffer(dev, &fi, nullptr, &hudFramebuffer) != VK_SUCCESS)
				SPRaise("Failed to create the HUD framebuffer");
		}

		void VulkanRenderer::EnsureWristTarget() {
			// After EnsureHudTarget, whose render pass this shares.
			if (wristFramebuffer != VK_NULL_HANDLE)
				return;
			const uint32_t w = static_cast<uint32_t>(client::kVRWristCanvasWidth * uiPixelRatio);
			const uint32_t h = static_cast<uint32_t>(client::kVRWristCanvasHeight * uiPixelRatio);
			wristImage = Handle<VulkanImage>(
			  new VulkanImage(device, w, h, device->GetSwapchainImageFormat(),
			                  VK_IMAGE_TILING_OPTIMAL,
			                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			                    VK_IMAGE_USAGE_SAMPLED_BIT,
			                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
			  false);
			wristImage->CreateSampler(); // the window's preview draws it (RecordVRMirror)
			VkImageView view = wristImage->GetImageView();
			VkFramebufferCreateInfo fi{};
			fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fi.renderPass = hudRenderPass;
			fi.attachmentCount = 1;
			fi.pAttachments = &view;
			fi.width = w;
			fi.height = h;
			fi.layers = 1;
			if (vkCreateFramebuffer(device->GetDevice(), &fi, nullptr, &wristFramebuffer) !=
			    VK_SUCCESS)
				SPRaise("Failed to create the wrist panel framebuffer");
		}

		bool VulkanRenderer::RecordWristPanel(VkCommandBuffer cmd, uint32_t frameIndex) {
#if USE_OPENXR
			vr::OpenXRSystem* xr = vr::GetOpenXRSystem();
			const bool used = wristCanvasUsed;
			wristCanvasUsed = false;
			if (!wristImageRenderer)
				return false;
			VkImage target = VK_NULL_HANDLE;
			if (!used || !xr->GetFrameState().shouldRender || !xr->AcquireWristImage(target)) {
				wristImageRenderer->Discard();
				return false;
			}
			EnsureWristTarget();
			// Drawn at the pixel ratio, then filtered down to the canvas.
			const int32_t w = client::kVRWristCanvasWidth;
			const int32_t h = client::kVRWristCanvasHeight;
			const int32_t sw = w * uiPixelRatio, sh = h * uiPixelRatio;

			VkClearValue clear{};
			clear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
			VkRenderPassBeginInfo rp{};
			rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rp.renderPass = hudRenderPass;
			rp.framebuffer = wristFramebuffer;
			rp.renderArea.extent = {static_cast<uint32_t>(sw), static_cast<uint32_t>(sh)};
			rp.clearValueCount = 1;
			rp.pClearValues = &clear;
			vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
			wristImageRenderer->Flush(cmd, frameIndex,
			                          {static_cast<uint32_t>(sw), static_cast<uint32_t>(sh)});
			vkCmdEndRenderPass(cmd); // -> TRANSFER_SRC_OPTIMAL

			ImageBarrier(cmd, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			             0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			             VK_PIPELINE_STAGE_TRANSFER_BIT);
			Blit(cmd, wristImage->GetImage(), 0, 0, sw, sh, target, w, h);
			ImageBarrier(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
			             VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			             VK_PIPELINE_STAGE_TRANSFER_BIT,
			             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
			return true;
#else
			return false;
#endif
		}

		VkExtent2D VulkanRenderer::EyeCanvasLayout(int eye) const {
			// Half the headset's image, not of the (possibly smaller) picture the
			// eye was drawn at, so labels keep their size when the resolution drops.
#if USE_OPENXR
			if (vr::OpenXRSystem* xr = vr::GetOpenXRSystem()) {
				const auto& fs = xr->GetFrameState();
				if (fs.eyeWidth > 0 && fs.eyeHeight > 0)
					return {static_cast<uint32_t>(std::max(1, fs.eyeWidth / 2)),
					        static_cast<uint32_t>(std::max(1, fs.eyeHeight / 2))};
			}
#endif
			if (!vrEyeImages[eye])
				return {1, 1};
			return {std::max(1u, vrEyeImages[eye]->GetWidth() / 2),
			        std::max(1u, vrEyeImages[eye]->GetHeight() / 2)};
		}

		void VulkanRenderer::DiscardEyeOverlays() {
			for (int e = 0; e < 2; e++) {
				if (eyeImageRenderers[e])
					eyeImageRenderers[e]->Discard();
				eyeCanvasUsed[e] = false;
				if (eyeComfortRenderers[e])
					eyeComfortRenderers[e]->Discard();
				eyeComfortUsed[e] = false;
			}
		}

		void VulkanRenderer::EnsureVRMirrorImage(uint32_t width, uint32_t height) {
			// After EnsureVREyeImage, whose overlay pass this shares
			if (vrMirrorImage && vrMirrorImage->GetWidth() == width &&
			    vrMirrorImage->GetHeight() == height)
				return;
			VkDevice dev = device->GetDevice();
			vkDeviceWaitIdle(dev); // a submitted frame may still read the old one
			if (vrMirrorFramebuffer != VK_NULL_HANDLE)
				vkDestroyFramebuffer(dev, vrMirrorFramebuffer, nullptr);
			vrMirrorFramebuffer = VK_NULL_HANDLE;
			vrMirrorImage = Handle<VulkanImage>(
			  new VulkanImage(device, width, height, device->GetSwapchainImageFormat(),
			                  VK_IMAGE_TILING_OPTIMAL,
			                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			                    VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
			  false);
			VkImageView view = vrMirrorImage->GetImageView();
			VkFramebufferCreateInfo fi{};
			fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fi.renderPass = eyeOverlayRenderPass;
			fi.attachmentCount = 1;
			fi.pAttachments = &view;
			fi.width = width;
			fi.height = height;
			fi.layers = 1;
			if (vkCreateFramebuffer(dev, &fi, nullptr, &vrMirrorFramebuffer) != VK_SUCCESS)
				SPRaise("Failed to create the window preview's framebuffer");
		}

		void VulkanRenderer::RecordVRMirror(VkCommandBuffer cmd, uint32_t frameIndex,
		                                    VulkanImage& eye, int eyeIndex, bool wristDrawn) {
#if USE_OPENXR
			const uint32_t w = eye.GetWidth(), h = eye.GetHeight();
			EnsureVRMirrorImage(w, h);
			VkImage mirror = vrMirrorImage->GetImage();
			ImageBarrier(cmd, mirror, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			             0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			             VK_PIPELINE_STAGE_TRANSFER_BIT);
			VkImageCopy copy{};
			copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			copy.extent = {w, h, 1};
			vkCmdCopyImage(cmd, eye.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mirror,
			               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

			// The wrist panel where this eye sees it: its corners through the
			// eye's own frustum (OpenXR's: +X right, +Y up, looking down -Z)
			const auto& fs = vr::GetOpenXRSystem()->GetFrameState();
			bool drawWrist = wristDrawn && wristImage && fs.wristShown;
			Vector2 corners[4];
			if (drawWrist) {
				const client::VRPose& eyePose = fs.eyes[eyeIndex];
				const client::VRQuat toEye = eyePose.orientation.Conjugate();
				const client::VRPose& panel = fs.wristPanel;
				const float pw = fs.wristWidth;
				const float ph = pw * static_cast<float>(client::kVRWristCanvasHeight) /
				                 static_cast<float>(client::kVRWristCanvasWidth);
				const Vector3 right = panel.Axis(0) * (pw * 0.5F);
				const Vector3 up = panel.Axis(1) * (ph * 0.5F);
				const Vector3 at[4] = {panel.position - right + up, panel.position + right + up,
				                       panel.position + right - up, panel.position - right - up};
				const float l = fs.eyeLeft[eyeIndex], r = fs.eyeRight[eyeIndex];
				const float d = fs.eyeDown[eyeIndex], u = fs.eyeUp[eyeIndex];
				for (int i = 0; i < 4 && drawWrist; i++) {
					const Vector3 v = toEye.Rotate(at[i] - eyePose.position);
					if (v.z > -0.01F) {
						drawWrist = false; // a corner behind the eye
						break;
					}
					const float tx = v.x / -v.z, ty = v.y / -v.z;
					corners[i] = MakeVector2((tx - l) / (r - l) * static_cast<float>(w),
					                         (u - ty) / (u - d) * static_cast<float>(h));
				}
			}
			if (drawWrist) {
				// Its picture is left for the headset's blit (TRANSFER_SRC); read
				// here as a texture
				ImageBarrier(cmd, wristImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
				             VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
				if (!mirrorImageRenderer)
					mirrorImageRenderer = stmp::make_unique<VulkanImageRenderer>(*this);
				mirrorImageRenderer->SetSurfaceExtent({w, h});
				mirrorImageRenderer->SetImage(wristImage.GetPointerOrNull());
				const float iw = static_cast<float>(wristImage->GetWidth());
				const float ih = static_cast<float>(wristImage->GetHeight());
				mirrorImageRenderer->Add(corners[0].x, corners[0].y, corners[1].x, corners[1].y,
				                         corners[2].x, corners[2].y, corners[3].x, corners[3].y,
				                         0.0F, 0.0F, iw, 0.0F, iw, ih, 0.0F, ih, 1.0F, 1.0F, 1.0F,
				                         1.0F);
				VkRenderPassBeginInfo rp{};
				rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rp.renderPass = eyeOverlayRenderPass;
				rp.framebuffer = vrMirrorFramebuffer;
				rp.renderArea.extent = {w, h};
				vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
				mirrorImageRenderer->Flush(cmd, frameIndex, {w, h});
				vkCmdEndRenderPass(cmd); // -> TRANSFER_SRC_OPTIMAL
			} else {
				ImageBarrier(cmd, mirror, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
				             VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				             VK_PIPELINE_STAGE_TRANSFER_BIT);
			}
#endif
		}

		void VulkanRenderer::EnsureVREyeImage(int eye, uint32_t width, uint32_t height) {
			VkDevice dev = device->GetDevice();
			VkFormat format = device->GetSwapchainImageFormat();

			if (eyeOverlayRenderPass == VK_NULL_HANDLE) {
				// Draws the eye's labels over its picture: LOAD, from the blit that
				// wrote it, to TRANSFER_SRC for the blit that sends it on. The same
				// format and sample count as the UI pass, so the image renderer's
				// pipeline draws into it.
				VkAttachmentDescription color{};
				color.format = format;
				color.samples = VK_SAMPLE_COUNT_1_BIT;
				color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				color.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

				VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
				VkSubpassDescription subpass{};
				subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				subpass.colorAttachmentCount = 1;
				subpass.pColorAttachments = &ref;

				VkSubpassDependency deps[2]{};
				deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
				deps[0].dstSubpass = 0;
				deps[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
				deps[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				deps[0].dstAccessMask =
				  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				deps[1].srcSubpass = 0;
				deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
				deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
				deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

				VkRenderPassCreateInfo rpi{};
				rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
				rpi.attachmentCount = 1;
				rpi.pAttachments = &color;
				rpi.subpassCount = 1;
				rpi.pSubpasses = &subpass;
				rpi.dependencyCount = 2;
				rpi.pDependencies = deps;
				if (vkCreateRenderPass(dev, &rpi, nullptr, &eyeOverlayRenderPass) != VK_SUCCESS)
					SPRaise("Failed to create the eye overlay render pass");
			}

			if (vrEyeImages[eye] && vrEyeImages[eye]->GetWidth() == width &&
			    vrEyeImages[eye]->GetHeight() == height)
				return;

			// Only when the eye size changes. The old image may still be read by a
			// submitted frame.
			vkDeviceWaitIdle(dev);
			if (vrEyeFramebuffers[eye] != VK_NULL_HANDLE)
				vkDestroyFramebuffer(dev, vrEyeFramebuffers[eye], nullptr);
			vrEyeFramebuffers[eye] = VK_NULL_HANDLE;
			vrEyeImages[eye] = Handle<VulkanImage>(
			  new VulkanImage(device, width, height, format, VK_IMAGE_TILING_OPTIMAL,
			                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
			  false);

			VkImageView view = vrEyeImages[eye]->GetImageView();
			VkFramebufferCreateInfo fi{};
			fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fi.renderPass = eyeOverlayRenderPass;
			fi.attachmentCount = 1;
			fi.pAttachments = &view;
			fi.width = width;
			fi.height = height;
			fi.layers = 1;
			if (vkCreateFramebuffer(dev, &fi, nullptr, &vrEyeFramebuffers[eye]) != VK_SUCCESS)
				SPRaise("Failed to create an eye overlay framebuffer");
		}

		// ---- Multiview ------------------------------------------------------
		//
		// Every scene shader already works out a clip position from its own
		// matrices (push constants, a per-model buffer, ...), all built for one
		// view. Rather than give each of them two sets of matrices, the scene is
		// drawn with eye 0's, and each .mv vertex shader ends with
		//     gl_Position = correction[gl_ViewIndex] * gl_Position;
		// where correction[i] = PV_i * PV_0^-1 takes eye 0's clip space to eye
		// i's. It is exact (every PV is invertible, and the product is linear in
		// homogeneous coordinates) and needs one line per shader. For the water's
		// mirror pass the reflection cancels out of the product
		// (P_i V_i R (P_0 V_0 R)^-1 = P_i V_i V_0^-1 P_0^-1), so one set serves
		// both passes. It is worked out in double: a perspective inverse loses
		// digits in float.

		namespace {
			struct M4d {
				double m[16]; // column-major, as Matrix4
			};
			M4d ToD(const Matrix4& a) {
				M4d r;
				for (int i = 0; i < 16; i++)
					r.m[i] = a.m[i];
				return r;
			}
			Matrix4 ToF(const M4d& a) {
				Matrix4 r;
				for (int i = 0; i < 16; i++)
					r.m[i] = static_cast<float>(a.m[i]);
				return r;
			}
			M4d Mul(const M4d& a, const M4d& b) {
				M4d r;
				for (int c = 0; c < 4; c++)
					for (int row = 0; row < 4; row++) {
						double v = 0.0;
						for (int k = 0; k < 4; k++)
							v += a.m[k * 4 + row] * b.m[c * 4 + k];
						r.m[c * 4 + row] = v;
					}
				return r;
			}
			// Gauss-Jordan with partial pivoting.
			M4d Inverse(const M4d& a) {
				double w[4][8];
				for (int row = 0; row < 4; row++)
					for (int c = 0; c < 4; c++) {
						w[row][c] = a.m[c * 4 + row];
						w[row][c + 4] = row == c ? 1.0 : 0.0;
					}
				for (int c = 0; c < 4; c++) {
					int p = c;
					for (int row = c + 1; row < 4; row++)
						if (std::fabs(w[row][c]) > std::fabs(w[p][c]))
							p = row;
					for (int k = 0; k < 8; k++)
						std::swap(w[c][k], w[p][k]);
					const double d = w[c][c] != 0.0 ? w[c][c] : 1.0e-300;
					for (int k = 0; k < 8; k++)
						w[c][k] /= d;
					for (int row = 0; row < 4; row++) {
						if (row == c)
							continue;
						const double f = w[row][c];
						for (int k = 0; k < 8; k++)
							w[row][k] -= f * w[c][k];
					}
				}
				M4d r;
				for (int row = 0; row < 4; row++)
					for (int c = 0; c < 4; c++)
						r.m[c * 4 + row] = w[row][c + 4];
				return r;
			}

			// std140, as ZsViews in the .mv shaders.
			struct ViewSetData {
				float correction[2][16];
				float view[2][16];
				float projection[2][16];
				float eyeOrigin[2][4];
				float fovTan[2][4]; // the water's (right, -up, left, -down)
			};
		} // namespace

		void VulkanRenderer::CopyImageLayer(VkCommandBuffer cmd, VulkanImage* src, uint32_t srcLayer,
		                                    VulkanImage* dst, uint32_t dstLayer) {
			VkImageMemoryBarrier pre[2] = {};
			pre[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			pre[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			pre[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			pre[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
			                       VK_ACCESS_TRANSFER_WRITE_BIT;
			pre[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			pre[0].srcQueueFamilyIndex = pre[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			pre[0].image = src->GetImage();
			pre[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, srcLayer, 1};
			pre[1] = pre[0];
			pre[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // the whole layer is replaced
			pre[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			pre[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			pre[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			pre[1].image = dst->GetImage();
			pre[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, dstLayer, 1};
			vkCmdPipelineBarrier(cmd,
			                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, pre);
			VkImageCopy region{};
			region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, srcLayer, 1};
			region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, dstLayer, 1};
			region.extent = {dst->GetWidth(), dst->GetHeight(), 1};
			vkCmdCopyImage(cmd, src->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->GetImage(),
			               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
			VkImageMemoryBarrier post[2] = {pre[0], pre[1]};
			post[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			post[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			post[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			post[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
			post[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			post[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			post[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			post[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     0, 0, nullptr, 0, nullptr, 2, post);
		}

		uint32_t VulkanRenderer::AppendViewSetLayout(std::vector<VkDescriptorSetLayout>& layouts) const {
			const uint32_t index = static_cast<uint32_t>(layouts.size());
			if (multiview)
				layouts.push_back(viewSetLayout);
			return index;
		}

		void VulkanRenderer::CreateViewSets() {
			VkDevice dev = device->GetDevice();
			VkDescriptorSetLayoutBinding b{};
			b.binding = 0;
			b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			b.descriptorCount = 1;
			b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			VkDescriptorSetLayoutCreateInfo li{};
			li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			li.bindingCount = 1;
			li.pBindings = &b;
			if (vkCreateDescriptorSetLayout(dev, &li, nullptr, &viewSetLayout) != VK_SUCCESS)
				SPRaise("Failed to create the multiview set layout");

			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kViewSetSlots};
			VkDescriptorPoolCreateInfo pi{};
			pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			pi.maxSets = kViewSetSlots;
			pi.poolSizeCount = 1;
			pi.pPoolSizes = &size;
			if (vkCreateDescriptorPool(dev, &pi, nullptr, &viewSetPool) != VK_SUCCESS)
				SPRaise("Failed to create the multiview set pool");

			for (int i = 0; i < kViewSetSlots; i++) {
				viewSetBuffers[i] = Handle<VulkanBuffer>::New(
				  device, sizeof(ViewSetData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				VkDescriptorSetAllocateInfo ai{};
				ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				ai.descriptorPool = viewSetPool;
				ai.descriptorSetCount = 1;
				ai.pSetLayouts = &viewSetLayout;
				if (vkAllocateDescriptorSets(dev, &ai, &viewSets[i]) != VK_SUCCESS)
					SPRaise("Failed to allocate a multiview set");
				VkDescriptorBufferInfo bi{viewSetBuffers[i]->GetBuffer(), 0, sizeof(ViewSetData)};
				VkWriteDescriptorSet w{};
				w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w.dstSet = viewSets[i];
				w.dstBinding = 0;
				w.descriptorCount = 1;
				w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				w.pBufferInfo = &bi;
				vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
				// Until a headset frame writes it: both views are eye 0's (a frame
				// drawn while the headset sleeps shows the same picture twice).
				client::SceneDefinition flat;
				flat.fovX = flat.fovY = 1.0F;
				flat.zNear = 0.05F;
				flat.zFar = 128.0F;
				currentFrameSlot = static_cast<uint32_t>(i);
				WriteViewSet(flat, flat);
			}
			currentFrameSlot = 0;
			SPLog("Multiview: per-eye sets created");
		}

		void VulkanRenderer::DestroyViewSets() {
			VkDevice dev = device->GetDevice();
			for (auto& b : viewSetBuffers)
				b = nullptr;
			if (viewSetPool != VK_NULL_HANDLE)
				vkDestroyDescriptorPool(dev, viewSetPool, nullptr);
			viewSetPool = VK_NULL_HANDLE;
			if (viewSetLayout != VK_NULL_HANDLE)
				vkDestroyDescriptorSetLayout(dev, viewSetLayout, nullptr);
			viewSetLayout = VK_NULL_HANDLE;
		}

		void VulkanRenderer::BindViewSet(VkCommandBuffer cmd, VkPipelineLayout layout,
		                                 uint32_t setIndex) {
			if (!multiview)
				return;
			const uint32_t slot = std::min<uint32_t>(currentFrameSlot, kViewSetSlots - 1);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, setIndex, 1,
			                        &viewSets[slot], 0, nullptr);
		}

		void VulkanRenderer::WriteViewSet(const client::SceneDefinition& eye0,
		                                  const client::SceneDefinition& eye1) {
			if (!multiview)
				return;
			const client::SceneDefinition* eyes[2] = {&eye0, &eye1};
			ViewSetData d{};
			M4d pv[2];
			for (int i = 0; i < 2; i++) {
				const Matrix4 v = eyes[i]->ToViewMatrix();
				const Matrix4 p = eyes[i]->ToVulkanProjectionMatrix();
				pv[i] = Mul(ToD(p), ToD(v));
				std::memcpy(d.view[i], v.m, sizeof(d.view[i]));
				std::memcpy(d.projection[i], p.m, sizeof(d.projection[i]));
				d.eyeOrigin[i][0] = eyes[i]->viewOrigin.x;
				d.eyeOrigin[i][1] = eyes[i]->viewOrigin.y;
				d.eyeOrigin[i][2] = eyes[i]->viewOrigin.z;
				const client::FovTangents f = eyes[i]->GetFovTangents();
				d.fovTan[i][0] = f.right;
				d.fovTan[i][1] = -f.up;
				d.fovTan[i][2] = f.left;
				d.fovTan[i][3] = -f.down;
			}
			// Eye 0's correction is the identity by construction; written exactly.
			for (int k = 0; k < 16; k++)
				d.correction[0][k] = (k % 5 == 0) ? 1.0F : 0.0F;
			const Matrix4 c1 = ToF(Mul(pv[1], Inverse(pv[0])));
			std::memcpy(d.correction[1], c1.m, sizeof(d.correction[1]));
			const uint32_t slot = std::min<uint32_t>(currentFrameSlot, kViewSetSlots - 1);
			viewSetBuffers[slot]->UpdateData(&d, sizeof(d));
		}

		void VulkanRenderer::RenderVREyes() {
			SPADES_MARK_FUNCTION();
#if USE_OPENXR
			vr::OpenXRSystem* xr = vr::GetOpenXRSystem();
			if (!xr)
				return;
			EnsureVRResources();
			VkDevice dev = device->GetDevice();

			const client::SceneDefinition head = vrHeadSceneDef;

			// Frame-wide animation, stepped once however many views there are.
			{
				float dt = (float)(head.time - lastTime) / 1000.0f;
				if (dt > 0.1f)
					dt = 0.1f;
				if (dt < 0.0f || lastTime == 0)
					dt = 0.0f;
				lastDt = dt;
				if (waterRenderer)
					waterRenderer->UpdateSimulation(dt);
				lastTime = head.time;
			}

			UpdateVRResolution(xr->GetFrameState().displayPeriod * 1000.0F, lastDt);

			const bool scopeView = vrScopeWanted;
			vrScopeWanted = false;
			vrScopeRendered = false;

			bool ok = true;
			// Multiview: one recording, one submit. The scene is drawn from the
			// left eye's matrices, culled for both eyes, and each vertex moved into
			// its eye by the view set's correction; the post chain then runs for
			// each eye (RecordCommandBuffer).
			if (multiview) {
				vkWaitForFences(dev, 1, &vrEyeFences[0], VK_TRUE, UINT64_MAX);
				vkResetFences(dev, 1, &vrEyeFences[0]);
				for (int eye = 0; eye < 2; eye++) {
					client::SceneDefinition& d = vrMultiviewEyes[eye];
					d = head;
					const auto& e = head.vrEyes[eye];
					d.viewOrigin = e.origin;
					d.viewAxis[0] = e.axis[0];
					d.viewAxis[1] = e.axis[1];
					d.viewAxis[2] = e.axis[2];
					d.fovX = e.fovX;
					d.fovY = e.fovY;
					d.asymmetric = e.asymmetric;
					d.tangents = e.tangents;
				}
				currentFrameSlot = 0;
				WriteViewSet(vrMultiviewEyes[0], vrMultiviewEyes[1]);
				sceneDef = vrMultiviewEyes[0];
				BuildProjectionMatrix();
				BuildView();
				BuildFrustrum();
				WidenFrustrumTo(vrMultiviewEyes[1]);
				vrViewLast = !scopeView;
				vrEyeIndex = 0;
				RecordCommandBuffer(0);
				vrEyeIndex = -1;

				VkSubmitInfo si{};
				si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				si.commandBufferCount = 1;
				si.pCommandBuffers = &vrEyeCommandBuffers[0];
				VkResult r = vkQueueSubmit(device->GetGraphicsQueue(), 1, &si, vrEyeFences[0]);
				HandleSubmitResult(r, "VR eyes (multiview)");
				ok = r == VK_SUCCESS;
			}
			for (int eye = 0; eye < 2 && ok && !multiview; eye++) {
				vrViewLast = eye == 1 && !scopeView;
				// This eye's slot was last used by this eye one frame ago.
				vkWaitForFences(dev, 1, &vrEyeFences[eye], VK_TRUE, UINT64_MAX);
				vkResetFences(dev, 1, &vrEyeFences[eye]);

				sceneDef = head;
				const auto& e = head.vrEyes[eye];
				sceneDef.viewOrigin = e.origin;
				sceneDef.viewAxis[0] = e.axis[0];
				sceneDef.viewAxis[1] = e.axis[1];
				sceneDef.viewAxis[2] = e.axis[2];
				sceneDef.fovX = e.fovX;
				sceneDef.fovY = e.fovY;
				sceneDef.asymmetric = e.asymmetric;
				sceneDef.tangents = e.tangents;
				BuildProjectionMatrix();
				BuildView();
				BuildFrustrum();

				currentFrameSlot = static_cast<uint32_t>(eye);
				vrEyeIndex = eye;
				RecordCommandBuffer(static_cast<uint32_t>(eye));
				vrEyeIndex = -1;

				VkSubmitInfo si{};
				si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				si.commandBufferCount = 1;
				si.pCommandBuffers = &vrEyeCommandBuffers[eye];
				VkResult r = vkQueueSubmit(device->GetGraphicsQueue(), 1, &si, vrEyeFences[eye]);
				HandleSubmitResult(r, "VR eye");
				if (r != VK_SUCCESS)
					ok = false;
			}
			vrEyesRendered = ok;
			vrTimestampSet ^= 1;

			// The scope: after the eyes, so it reuses the left eye's shadow cascade
			// (placed around the player, which is right for the eyes) instead of
			// placing it for itself. Its picture is drawn into the eyes in Flip, so
			// it is still this frame's.
			if (ok && scopeView) {
				const int size = 512;
				EnsureScopeTargets(size);
				vkWaitForFences(dev, 1, &vrEyeFences[2], VK_TRUE, UINT64_MAX);
				vkResetFences(dev, 1, &vrEyeFences[2]);
				// vr_debugScope: how the last picture came out, now it is done
				if (scopeDebugPending && scopeDebugReadback) {
					scopeDebugPending = false;
					const uint8_t* px = static_cast<const uint8_t*>(scopeDebugReadback->Map());
					const size_t n = static_cast<size_t>(scopeDebugReadback->GetSize() / 4);
					double sum = 0.0;
					size_t dark = 0;
					for (size_t i = 0; i < n; i++) {
						const int v = px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2];
						sum += v / 3.0;
						if (v < 12)
							dark++;
					}
					scopeDebugReadback->Unmap();
					SPLog("[VR] Scope picture: mean %.1f of 255, %.0f%% near black",
					      n ? sum / n : 0.0, n ? 100.0 * dark / n : 0.0);
				}

				sceneDef = head;
				sceneDef.viewOrigin = vrScopeView.origin;
				sceneDef.viewAxis[0] = vrScopeView.axis[0];
				sceneDef.viewAxis[1] = vrScopeView.axis[1];
				sceneDef.viewAxis[2] = vrScopeView.axis[2];
				sceneDef.fovX = vrScopeView.fovX;
				sceneDef.fovY = vrScopeView.fovY;
				sceneDef.asymmetric = false; // the scope's lens is centred

				std::swap(framebufferManager, scopeFramebufferManager);
				const int eyeW = renderWidth, eyeH = renderHeight;
				renderWidth = renderHeight = size;
				BuildProjectionMatrix();
				BuildView();
				BuildFrustrum();

				currentFrameSlot = 2;
				WriteViewSet(sceneDef, sceneDef); // multiview: both layers the scope's
				vrEyeIndex = 2;
				vrScopePass = true;
				vrViewLast = true;
				RecordCommandBuffer(2);
				vrScopePass = false;
				vrEyeIndex = -1;

				renderWidth = eyeW;
				renderHeight = eyeH;
				std::swap(framebufferManager, scopeFramebufferManager);

				VkSubmitInfo si{};
				si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				si.commandBufferCount = 1;
				si.pCommandBuffers = &vrEyeCommandBuffers[2];
				VkResult r = vkQueueSubmit(device->GetGraphicsQueue(), 1, &si, vrEyeFences[2]);
				HandleSubmitResult(r, "VR scope");
				vrScopeRendered = r == VK_SUCCESS;
			}
			vrViewLast = false;

			sceneDef = head;
			BuildProjectionMatrix();
			BuildView();
			BuildFrustrum();
			currentFrameSlot = device->GetCurrentFrame();
#endif
		}

		void VulkanRenderer::RecordVREyeOutput(VkCommandBuffer cmd, VulkanImage* finalImage) {
			if (!finalImage || vrEyeIndex < 0)
				return;
			if (vrScopePass) {
				const int32_t w = static_cast<int32_t>(finalImage->GetWidth());
				const int32_t h = static_cast<int32_t>(finalImage->GetHeight());
				ImageBarrier(cmd, finalImage->GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				             VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				             VK_ACCESS_TRANSFER_READ_BIT,
				             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
				               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				             VK_PIPELINE_STAGE_TRANSFER_BIT);
				// Linear in, sRGB out (the blit encodes), then copied bit for bit to
				// the UNORM image the 2D layer samples: it decodes sRGB itself.
				ImageBarrier(cmd, scopeSrgbImage->GetImage(), VK_IMAGE_LAYOUT_UNDEFINED,
				             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
				             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
				Blit(cmd, finalImage->GetImage(), 0, 0, w, h, scopeSrgbImage->GetImage(),
				     static_cast<int32_t>(scopeSrgbImage->GetWidth()),
				     static_cast<int32_t>(scopeSrgbImage->GetHeight()));
				ImageBarrier(cmd, scopeSrgbImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
				             VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				             VK_PIPELINE_STAGE_TRANSFER_BIT);
				// vr_debugScope: a copy every 90th picture, read once it is done
				if ((int)vr_debugScope && ++scopeDebugFrames % 90 == 0) {
					const VkDeviceSize bytes = static_cast<VkDeviceSize>(scopeSrgbImage->GetWidth()) *
					                           scopeSrgbImage->GetHeight() * 4;
					if (!scopeDebugReadback || scopeDebugReadback->GetSize() != bytes)
						scopeDebugReadback = Handle<VulkanBuffer>::New(
						  device, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
						  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
					VkBufferImageCopy region{};
					region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
					region.imageExtent = {scopeSrgbImage->GetWidth(), scopeSrgbImage->GetHeight(), 1};
					vkCmdCopyImageToBuffer(cmd, scopeSrgbImage->GetImage(),
					                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					                       scopeDebugReadback->GetBuffer(), 1, &region);
					VkMemoryBarrier toHost{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
					toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
					vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
					                     VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0, nullptr, 0,
					                     nullptr);
					scopeDebugPending = true;
				}
				// Last frame's Flip sampled it: wait for that (fragment) first.
				ImageBarrier(cmd, scopeImage->GetImage(), VK_IMAGE_LAYOUT_UNDEFINED,
				             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
				             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
				VkImageCopy copy{};
				copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
				copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
				copy.extent = {scopeImage->GetWidth(), scopeImage->GetHeight(), 1};
				vkCmdCopyImage(cmd, scopeSrgbImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				               scopeImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
				ImageBarrier(cmd, scopeImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
				             VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
				ImageBarrier(cmd, finalImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
				             VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
				return;
			}
			const uint32_t w = finalImage->GetWidth();
			const uint32_t h = finalImage->GetHeight();
			EnsureVREyeImage(vrEyeIndex, w, h);
			VkImage kept = vrEyeImages[vrEyeIndex]->GetImage();

			ImageBarrier(cmd, finalImage->GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT |
			               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			             VK_ACCESS_TRANSFER_READ_BIT,
			             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			             VK_PIPELINE_STAGE_TRANSFER_BIT);

			// Replaced whole. Last frame's Flip read it (transfer), earlier on the
			// queue: an execution dependency is all that write-after-read needs.
			ImageBarrier(cmd, kept, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			             0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			             VK_PIPELINE_STAGE_TRANSFER_BIT);
			// Linear float in, sRGB out: the blit does the encoding, as it does for
			// the window. Left in TRANSFER_DST for Flip's overlay pass.
			Blit(cmd, finalImage->GetImage(), 0, 0, static_cast<int32_t>(w), static_cast<int32_t>(h),
			     kept, static_cast<int32_t>(w), static_cast<int32_t>(h));

			ImageBarrier(cmd, finalImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
			             VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			RecordVRTimestamp(cmd, false);
		}

		void VulkanRenderer::FlipVRFrame() {
			SPADES_MARK_FUNCTION();
#if USE_OPENXR
			vr::OpenXRSystem* xr = vr::GetOpenXRSystem();
			const auto& fs = xr->GetFrameState();
			VkDevice dev = device->GetDevice();

			EnsureHudTarget();

			// The window is only a mirror here: if it has no image to give (it is
			// minimised, or its swapchain was just rebuilt) the headset still gets
			// its frame.
			currentFrameSlot = device->GetCurrentFrame();
			vkWaitForFences(dev, 1, &inFlightFences[currentFrameSlot], VK_TRUE, UINT64_MAX);
			currentImageIndex =
			  device->AcquireNextImage(&imageAvailableSemaphore, &renderFinishedSemaphore);
			// As in StartScene: this acquire may have rebuilt the swapchain.
			if (currentImageIndex != UINT32_MAX &&
			    device->GetSwapchainGeneration() != lastSwapchainGeneration)
				RecreateSwapchainDependencies();
			const bool toWindow =
			  currentImageIndex != UINT32_MAX && currentImageIndex < commandBuffers.size();
			const uint32_t cbIndex = toWindow ? currentImageIndex : 0;
			if (toWindow)
				WaitForImageInFlight();
			else
				vkDeviceWaitIdle(dev); // index 0's per-frame resources may still be in use
			vkResetFences(dev, 1, &inFlightFences[currentFrameSlot]);

			FlushPendingUploads();
			ProcessDeferredDeletions();

			VkCommandBuffer cmd = commandBuffers[cbIndex];
			VkCommandBufferBeginInfo bi{};
			bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) {
				SPLog("Warning: Failed to begin the VR frame's command buffer");
				if (wristImageRenderer)
					wristImageRenderer->Discard();
				wristCanvasUsed = false;
				DiscardEyeOverlays();
				// The headset's eye images are only taken later in Flip, so there
				// are none to submit.
				xr->EndFrame(false, false);
				return;
			}

			// 1. The 2D layer, onto a transparent panel.
			{
				VkClearValue clear{};
				clear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
				VkRenderPassBeginInfo rp{};
				rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rp.renderPass = hudRenderPass;
				rp.framebuffer = hudFramebuffer;
				rp.renderArea.extent = {hudImage->GetWidth(), hudImage->GetHeight()};
				rp.clearValueCount = 1;
				rp.pClearValues = &clear;
				vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

				// Screen tints (hurt flash, underwater) multiply the 3D view; on a
				// transparent panel they have nothing to multiply.
				pendingMultiplyColors.clear();

				if (imageRenderer)
					imageRenderer->Flush(cmd, cbIndex,
					                     {hudImage->GetWidth(), hudImage->GetHeight()});

				vkCmdEndRenderPass(cmd); // -> TRANSFER_SRC_OPTIMAL
			}

			// 2. The panel into the headset.
			VkImage hudTarget = VK_NULL_HANDLE;
			const bool toHud =
			  fs.shouldRender && xr->AcquireHudImage(static_cast<uint32_t>(uiWidth),
			                                         static_cast<uint32_t>(uiHeight), hudTarget);
			if (toHud) {
				ImageBarrier(cmd, hudTarget, VK_IMAGE_LAYOUT_UNDEFINED,
				             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
				             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
				// The supersampled layer filtered down to the layout's size: at 2x,
				// each headset texel is the average of 2x2 drawn ones, and the
				// text has the edges of four times the samples.
				Blit(cmd, hudImage->GetImage(), 0, 0, static_cast<int32_t>(hudImage->GetWidth()),
				     static_cast<int32_t>(hudImage->GetHeight()), hudTarget, uiWidth, uiHeight);
				ImageBarrier(cmd, hudTarget, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
				             VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
				               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				             VK_PIPELINE_STAGE_TRANSFER_BIT,
				             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
			}

			// 2b. The wrist panel, when it is up.
			const bool toWrist = RecordWristPanel(cmd, cbIndex);

			// 2c. The eyes: each kept picture gets its labels drawn over it, then
			// goes to the headset. The headset's images are taken here rather
			// than with the eye passes, because the labels are drawn after them.
			bool eyesOut = false;
			bool mirrorMade = false;
			const int mirrorEye = (int)vr_mirrorEye == 1 ? 1 : 0;
			if (vrEyesRendered && fs.shouldRender && vrEyeImages[0] && vrEyeImages[1]) {
				VkImage targets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
				eyesOut = xr->AcquireEyeImage(0, targets[0]) && xr->AcquireEyeImage(1, targets[1]);
				for (int e = 0; eyesOut && e < 2; e++) {
					VulkanImage& kept = *vrEyeImages[e];
					const VkExtent2D size{kept.GetWidth(), kept.GetHeight()};
					if (eyeCanvasUsed[e] && eyeImageRenderers[e]) {
						VkRenderPassBeginInfo rp{};
						rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
						rp.renderPass = eyeOverlayRenderPass;
						rp.framebuffer = vrEyeFramebuffers[e];
						rp.renderArea.extent = size;
						vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
						eyeImageRenderers[e]->Flush(cmd, cbIndex, size);
						vkCmdEndRenderPass(cmd); // -> TRANSFER_SRC_OPTIMAL
					} else {
						ImageBarrier(cmd, kept.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
						             VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
						             VK_PIPELINE_STAGE_TRANSFER_BIT);
					}
					// The window's preview: this eye as labelled, before the
					// headset-only layer
					if (e == mirrorEye && toWindow) {
						RecordVRMirror(cmd, cbIndex, kept, e, toWrist);
						mirrorMade = true;
					}
					if (eyeComfortUsed[e] && eyeComfortRenderers[e]) {
						ImageBarrier(cmd, kept.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
						             VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
						             VK_PIPELINE_STAGE_TRANSFER_BIT);
						VkRenderPassBeginInfo rp{};
						rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
						rp.renderPass = eyeOverlayRenderPass;
						rp.framebuffer = vrEyeFramebuffers[e];
						rp.renderArea.extent = size;
						vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
						eyeComfortRenderers[e]->Flush(cmd, cbIndex, size);
						vkCmdEndRenderPass(cmd); // -> TRANSFER_SRC_OPTIMAL
					}
					const int32_t w = static_cast<int32_t>(size.width);
					const int32_t h = static_cast<int32_t>(size.height);
					// Scaled up to the headset's image when drawn smaller.
					const int32_t tw = fs.eyeWidth > 0 ? fs.eyeWidth : w;
					const int32_t th = fs.eyeHeight > 0 ? fs.eyeHeight : h;
					ImageBarrier(cmd, targets[e], VK_IMAGE_LAYOUT_UNDEFINED,
					             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
					             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
					Blit(cmd, kept.GetImage(), 0, 0, w, h, targets[e], tw, th);
					// The runtime takes the image back in the layout it gave it out in.
					ImageBarrier(cmd, targets[e], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
					             VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
					               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					             VK_PIPELINE_STAGE_TRANSFER_BIT,
					             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
				}
				if (!eyesOut) {
					xr->ReleaseEyeImage(0);
					xr->ReleaseEyeImage(1);
				}
			}
			DiscardEyeOverlays();

			// 3. The window: the left eye cropped to the window's shape, or the
			// panel itself when nothing 3D was drawn (the menus).
			if (toWindow) {
				VkImage sc = device->GetSwapchainImage(currentImageIndex);
				VkExtent2D ext = device->GetSwapchainExtent();
				ImageBarrier(cmd, sc, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				             0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				             VK_PIPELINE_STAGE_TRANSFER_BIT);
				if (ext.width == 0 || ext.height == 0 || device->IsHeadless()) {
					// Minimised, or no window at all (vr_mirror 0): there is an image
					// to give back but nothing to show.
					// The crop below would divide 0 by 0, and a NaN aspect made a
					// blit from y = INT_MIN.
				} else if (eyesOut) {
					VulkanImage& mirror = mirrorMade ? *vrMirrorImage : *vrEyeImages[mirrorEye];
					int32_t w = static_cast<int32_t>(mirror.GetWidth());
					int32_t h = static_cast<int32_t>(mirror.GetHeight());
					float aspect = static_cast<float>(ext.width) / static_cast<float>(ext.height);
					int32_t x0 = 0, y0 = 0, x1 = w, y1 = h;
					if (static_cast<float>(w) / static_cast<float>(h) > aspect) {
						int32_t cw = static_cast<int32_t>(h * aspect);
						x0 = (w - cw) / 2;
						x1 = x0 + cw;
					} else {
						int32_t ch = static_cast<int32_t>(w / aspect);
						y0 = (h - ch) / 2;
						y1 = y0 + ch;
					}
					Blit(cmd, mirror.GetImage(), x0, y0, x1, y1, sc,
					     static_cast<int32_t>(ext.width), static_cast<int32_t>(ext.height));
				} else {
					Blit(cmd, hudImage->GetImage(), 0, 0,
					     static_cast<int32_t>(hudImage->GetWidth()),
					     static_cast<int32_t>(hudImage->GetHeight()), sc,
					     static_cast<int32_t>(ext.width), static_cast<int32_t>(ext.height));
				}
				ImageBarrier(cmd, sc, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				             VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
				             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
			}

			vkEndCommandBuffer(cmd);

			VkSubmitInfo si{};
			si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			if (toWindow) {
				si.waitSemaphoreCount = 1;
				si.pWaitSemaphores = &imageAvailableSemaphore;
				si.pWaitDstStageMask = &waitStage;
				si.signalSemaphoreCount = 1;
				si.pSignalSemaphores = &renderFinishedSemaphore;
			}
			si.commandBufferCount = 1;
			si.pCommandBuffers = &cmd;
			VkResult r = vkQueueSubmit(device->GetGraphicsQueue(), 1, &si,
			                           inFlightFences[currentFrameSlot]);
			HandleSubmitResult(r, "VR frame");

			if (toHud)
				xr->ReleaseHudImage();
			if (toWrist)
				xr->ReleaseWristImage();
			if (eyesOut) {
				// The runtime orders its reads after work already on the queue.
				xr->ReleaseEyeImage(0);
				xr->ReleaseEyeImage(1);
			}
			if (toWindow) {
				VkSemaphore wait[] = {renderFinishedSemaphore};
				device->PresentImage(currentImageIndex, wait, 1);
			}

			xr->EndFrame(eyesOut, toHud, toWrist);
#endif
		}
	} // namespace draw
} // namespace spades
