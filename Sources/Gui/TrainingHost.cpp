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

#include "TrainingHost.h"

#include <atomic>
#include <mutex>
#include <thread>

#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Training/Server.h>

DEFINE_SPADES_SETTING(cg_localPort, "32890");

namespace spades {
	namespace gui {

		struct TrainingHost::Impl {
			std::mutex mutex;
			State state = State::Stopped;
			std::string error;
			std::atomic<bool> stop{false};
			std::thread thread;

			void Join() {
				if (thread.joinable()) {
					stop = true;
					thread.join();
				}
			}
		};

		TrainingHost& TrainingHost::Get() {
			static TrainingHost instance;
			return instance;
		}

		TrainingHost::TrainingHost() : impl(new Impl()) {}

		TrainingHost::~TrainingHost() { impl->Join(); }

		TrainingHost::State TrainingHost::GetState() {
			std::lock_guard<std::mutex> lock(impl->mutex);
			return impl->state;
		}

		std::string TrainingHost::GetError() {
			std::lock_guard<std::mutex> lock(impl->mutex);
			return impl->error;
		}

		bool TrainingHost::IsActive() {
			std::lock_guard<std::mutex> lock(impl->mutex);
			return impl->state == State::Ready;
		}

		void TrainingHost::Stop() {
			impl->Join();
			std::lock_guard<std::mutex> lock(impl->mutex);
			impl->state = State::Stopped;
		}

		std::string TrainingHost::Start() {
			Stop();
			{
				std::lock_guard<std::mutex> lock(impl->mutex);
				impl->state = State::Starting;
				impl->error.clear();
			}
			port = (int)cg_localPort;
			SPLog("Starting the training course on 127.0.0.1:%d", port);

			// Before the thread, not in it: a Stop() in between must not be lost.
			impl->stop = false;
			Impl* self = impl.get();
			const int listenPort = port;
			impl->thread = std::thread([self, listenPort]() {
				host::HostConfig cfg;
				cfg.port = static_cast<uint16_t>(listenPort);
				cfg.log = [](const std::string& line) { SPLog("[Training] %s", line.c_str()); };
				try {
					host::Server server(cfg);
					std::string err;
					if (!server.start(err)) {
						std::lock_guard<std::mutex> lock(self->mutex);
						self->state = State::Failed;
						self->error = err;
						return;
					}
					{
						std::lock_guard<std::mutex> lock(self->mutex);
						if (self->state == State::Starting)
							self->state = State::Ready;
					}
					server.run(self->stop);
				} catch (const std::exception& ex) {
					std::lock_guard<std::mutex> lock(self->mutex);
					self->state = State::Failed;
					self->error = ex.what();
					return;
				}
				std::lock_guard<std::mutex> lock(self->mutex);
				if (self->state == State::Ready)
					self->state = State::Stopped;
			});
			return "";
		}
	} // namespace gui
} // namespace spades
