/*
 Copyright (c) 2026 ZeroSpades developers.

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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace spades {
	namespace gui {
		/**
		 * Installs a newer release over the running game, for the packages that
		 * can replace their own files (Windows and Linux x64, from GitHub's
		 * releases; see `SelfUpdatePackage`).
		 *
		 * `Start` downloads the release's package in the background, after
		 * checking the release's signed SHA256SUMS and the package's sum, and
		 * unpacks it into a staging folder beside the game (`.zsupdate`). Nothing
		 * the game uses is touched until `InstallAndRestart`: it closes the game's
		 * files, moves each running file aside (Windows will not overwrite a
		 * running .exe or .dll, but will rename one) and the new one into its
		 * place, then starts the new version. Anything that fails on the way
		 * puts the old files back. The next start removes what was moved aside.
		 */
		class SelfUpdater {
		public:
			enum class State {
				Idle,
				/** Downloading, checking or unpacking; see `Progress::status`. */
				Working,
				/** Unpacked and checked; `InstallAndRestart` installs it. */
				Ready,
				/** See `Progress::error`. The release page is the way left. */
				Failed,
			};

			struct Progress {
				State state = State::Idle;
				std::string status;
				/** Of the current step; negative while unknown. */
				float fraction = -1.0F;
				std::string error;
				/** The release being installed, e.g. "v0.3.1". */
				std::string tag;
			};

			static SelfUpdater& Get();

			/**
			 * True if this build can update itself where it is: an official
			 * package, in a folder it can write to (and on Linux, run from its
			 * own folder, where it reads its paks from).
			 */
			static bool IsSupported();

			/**
			 * At startup: removes what the last update moved aside and any
			 * unfinished download, and logs a failed install.
			 */
			static void CleanUpAfterUpdate();

			/**
			 * Called every frame of the main screen: installs the release that
			 * ZS_UPDATE_TEST_DIR and ZS_UPDATE_TEST_TAG name, if they do (see
			 * SelfUpdater.cpp), so an install can be tested without a release.
			 */
			static void RunTestIfAsked();

			/** Starts downloading `tag` unless a download already runs or is ready. */
			void Start(const std::string& tag);

			Progress GetProgress();

			/**
			 * When `Ready`: installs the update and starts the new version, and
			 * does not return. If it cannot even begin it returns, and the state
			 * is `Failed`.
			 */
			void InstallAndRestart();

		private:
			class Download;
			friend class Download;

			std::mutex mutex;
			Progress progress;
			/** The package's files, relative to the game's folder, once unpacked. */
			std::vector<std::string> files;

			SelfUpdater() = default;
			SelfUpdater(const SelfUpdater&) = delete;
			SelfUpdater& operator=(const SelfUpdater&) = delete;

			void SetStatus(const std::string& status, float fraction);
			void Fail(const std::string& error);
			void Finish(std::vector<std::string>&& files);
		};
	} // namespace gui
} // namespace spades
