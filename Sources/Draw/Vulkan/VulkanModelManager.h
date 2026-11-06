/*
 Copyright (c) 2013 Fran6nd
 Vulkan port (c) 2024

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

#include <map>
#include <string>

#include <Client/IModel.h>
#include <Core/RefCountedObject.h>

namespace spades {
	class ResourceChanges;
	namespace draw {
		class VulkanOptimizedVoxelModel;
		class VulkanRenderer;
		class VulkanModelManager : public RefCountedObject {
			VulkanRenderer& renderer;
			std::map<std::string, Handle<VulkanOptimizedVoxelModel>> models;
			Handle<VulkanOptimizedVoxelModel> CreateModel(const char*);

		public:
			VulkanModelManager(VulkanRenderer&);
			~VulkanModelManager();
			Handle<client::IModel> RegisterModel(const char*);

			void ClearCache();
			/**
			 * Gives the cached models from a changed file their new content. The
			 * GPU must be idle: the old geometry is freed.
			 */
			void ReloadResources(const ResourceChanges&);
		};
	} // namespace draw
} // namespace spades
