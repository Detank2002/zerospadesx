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

// VMA single-header implementation unit.
// Compiled once to avoid duplicate symbol issues.
#define VMA_IMPLEMENTATION
#if defined(__ANDROID__)
// Android's libvulkan.so exports Vulkan 1.1 at most, and the headers are newer:
// linked statically, VMA reaches for 1.3 entry points that are not there. Look
// them up through the device instead (SDLVulkanDevice passes the proc-address
// functions), which takes whatever the headset's driver has.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#endif
#include "VulkanMemoryAllocator.h"
