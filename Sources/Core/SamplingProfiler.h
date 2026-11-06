/*
 Copyright (c) 2026 ZeroSpades X contributors

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

#include <string>

namespace spades {
	/**
	 * A sampling profiler of the whole run (core_profile), no admin needed:
	 * every thread of the game, about a thousand times a second, with the
	 * main loop's frames and their stages marked, so a frame that froze can be
	 * told apart and what ran in it read. Windows (x64) only; elsewhere every
	 * call does nothing.
	 *
	 * The report, written when the game closes, is `path` (frame times, the
	 * frames that froze and their stages, the functions taking the time over
	 * the run and in those frames, the stacks of those frames) and
	 * `path`.folded (every sample's stack, for a flame graph).
	 */
	namespace SamplingProfiler {
		/** From the main thread, which is the one whose frames are marked. */
		void Start(const std::string& path);
		/** A frame of the main loop starts. */
		void MarkFrame();
		/** The frame's next stage starts: 1 update, 2 render and present, 3 after. */
		void MarkStage(int stage);
		/** Stops sampling and writes the report. Nothing if never started. */
		void Stop();
		bool IsRunning();
	} // namespace SamplingProfiler
} // namespace spades
