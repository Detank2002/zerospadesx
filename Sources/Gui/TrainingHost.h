/*
 Copyright (c) 2026 Detank2002, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <memory>
#include <string>

#include <Training/TrainingModes.h>

namespace spades {
	namespace gui {

		/**
		 * The training course, hosted on this machine: the course's server
		 * (Sources/Training) on a thread of this process, on 127.0.0.1. The menu
		 * starts it, waits for it to listen, then connects to it like any other
		 * server.
		 *
		 * A thread, not a second process: a standalone headset cannot start a
		 * second copy of its app, and the PC takes the same path so that path is
		 * the one played. Leaving the course stops it, and so does the game
		 * closing.
		 */
		/** What a start of Training runs: the tutorial, or an aim scenario with
		 * this weapon (0 rifle, 1 SMG, 2 shotgun), its targets outlined if
		 * `highlight`. Outside TrainingHost, so a default argument can make one
		 * inside it (GCC and Clang want the initializers complete by then). */
		struct TrainingLaunch {
			training::Mode mode = training::Mode::Tutorial;
			training::Scenario scenario = training::Scenario::Gridshot;
			int weapon = 0;
			bool highlight = false;
		};

		class TrainingHost {
		public:
			enum class State { Stopped, Starting, Ready, Failed };

			static TrainingHost& Get();

			using Launch = TrainingLaunch;

			/** Starts the course's server on its thread. Returns an error
			 * message, or "". */
			std::string Start(const Launch& launch = Launch());

			/** What the running server was started with. */
			const Launch& GetLaunch() const { return launch; }
			bool IsAim() { return IsActive() && launch.mode == training::Mode::Aim; }

			/** The aim run as the server has it now. */
			training::AimStatus GetAimStatus();
			/** Asks for another run of the scenario, from its countdown. */
			void RestartAim();
			/** Stops it and waits for its thread, normally well under a second. */
			void Stop();

			/** Polled by the menu while it waits. */
			State GetState();
			/** Why it failed, in the server's own words. */
			std::string GetError();
			int GetPort() const { return port; }

			/** True while the course is up: the client then plays it (joins at
			 * once, shows the station tips) and sends its position as often as
			 * a local server can use it. */
			bool IsActive();

			~TrainingHost();

		private:
			TrainingHost();
			struct Impl;
			std::unique_ptr<Impl> impl;
			int port = 0;
			Launch launch;
		};
	} // namespace gui
} // namespace spades
