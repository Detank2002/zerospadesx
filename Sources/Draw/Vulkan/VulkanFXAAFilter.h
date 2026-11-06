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

#include <vector>
#include <vulkan/vulkan.h>
#include "VulkanFramesInFlight.h"
#include "VulkanPostProcessFilter.h"

namespace spades {
	namespace draw {

		// FXAA filter (Vulkan port of GLFXAAFilter).
		//
		// Single fullscreen pass that reads the scene image, runs the standard
		// FXAA neighbour-tap algorithm, and writes to `output`.
		//
		// Call Filter(cmd, input, output).  input must be in
		// SHADER_READ_ONLY_OPTIMAL; output ends up in SHADER_READ_ONLY_OPTIMAL.

		class VulkanFXAAFilter : public VulkanPostProcessFilter {

			VkFormat colorFormat;
			VkSampler linearSampler;

			VkRenderPass ppRenderPass;

			VkDescriptorSetLayout singleSamplerDSL;

			VkPipelineLayout fxaaLayout;
			VkPipeline fxaaPipeline;
			// Multiview twins (both eyes in one pass), made only in multiview.
			VkRenderPass ppRenderPassMV = VK_NULL_HANDLE;
			VkPipeline fxaaPipelineMV = VK_NULL_HANDLE;

			static constexpr int MAX_FRAME_SLOTS = MaxFrameSlots;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			void InitRenderPass();
			void InitDescriptorSetLayout();
			void InitPipeline();
			void InitDescriptorPools();

			VkShaderModule LoadSPIRV(const char* path);

			VkFramebuffer MakeFramebuffer(VulkanImage* image, int frameSlot, VkRenderPass pass);
			VkDescriptorSet BindTexture(int frameSlot, VkImageView view);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanFXAAFilter(VulkanRenderer& renderer);
			~VulkanFXAAFilter();

			void Filter(VkCommandBuffer cmd,
			            VulkanImage* input, VulkanImage* output) override;

			bool SupportsLayers() const override { return fxaaPipelineMV != VK_NULL_HANDLE; }
		};

	} // namespace draw
} // namespace spades
