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

#include "SamplingProfiler.h"

#include <Core/Debug.h>

#if defined(_WIN32) && defined(_M_X64)

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#pragma comment(lib, "dbghelp.lib")

namespace spades {
	namespace SamplingProfiler {
		namespace {
			constexpr int kDepth = 32;
			// Reserved whole at the start (about 160 MB): a sample is added while
			// its thread is suspended, which may hold the heap's lock, so the list
			// must never grow then. The main thread at a thousand a second and the
			// others at a tenth of that: some minutes of a game.
			constexpr size_t kMaxSamples = 600000;
			constexpr int kOtherThreadsEvery = 10;

			struct Sample {
				int64_t time;
				uint32_t thread; // index into `threadIds`
				uint16_t depth;
				uint64_t pcs[kDepth];
			};
			struct Mark {
				int64_t time;
				int stage; // 0 a frame starts
			};

			std::atomic<bool> running{false};
			std::thread sampler;
			std::string reportPath;
			DWORD mainThreadId = 0;
			int64_t startTime = 0;
			std::vector<Sample> samples;
			std::vector<Mark> marks; // the main thread's alone
			std::vector<DWORD> threadIds;

			int64_t Now() {
				LARGE_INTEGER t;
				QueryPerformanceCounter(&t);
				return t.QuadPart;
			}
			double Seconds(int64_t ticks) {
				LARGE_INTEGER f;
				QueryPerformanceFrequency(&f);
				return static_cast<double>(ticks) / static_cast<double>(f.QuadPart);
			}

			// The stack of a suspended thread, by the x64 unwind tables. Allocates
			// nothing: the thread may be holding the heap's lock.
			int Walk(CONTEXT ctx, uint64_t* pcs) {
				int n = 0;
				while (n < kDepth && ctx.Rip != 0) {
					pcs[n++] = ctx.Rip;
					DWORD64 imageBase = 0;
					PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
					if (!fn) {
						// A leaf function: the return address is on top of the stack
						if (ctx.Rsp == 0)
							break;
						ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
						ctx.Rsp += 8;
					} else {
						PVOID handlerData = nullptr;
						DWORD64 establisher = 0;
						RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx,
						                 &handlerData, &establisher, nullptr);
					}
				}
				return n;
			}

			std::vector<DWORD> GameThreads() {
				std::vector<DWORD> ids;
				HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
				if (snap == INVALID_HANDLE_VALUE)
					return ids;
				THREADENTRY32 te{};
				te.dwSize = sizeof(te);
				const DWORD pid = GetCurrentProcessId();
				const DWORD self = GetCurrentThreadId();
				if (Thread32First(snap, &te)) {
					do {
						if (te.th32OwnerProcessID == pid && te.th32ThreadID != self)
							ids.push_back(te.th32ThreadID);
					} while (Thread32Next(snap, &te));
				}
				CloseHandle(snap);
				return ids;
			}

			void Run() {
				SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
				std::map<DWORD, HANDLE> handles;
				std::map<DWORD, uint32_t> index;
				int64_t lastScan = 0;
				std::vector<DWORD> ids;
				unsigned tick = 0;
				while (running.load()) {
					tick++;
					const int64_t now = Now();
					// The threads come and go: looked for again twice a second
					if (now - lastScan > static_cast<int64_t>(0.5 / Seconds(1)) || ids.empty()) {
						lastScan = now;
						ids = GameThreads();
						for (DWORD id : ids)
							if (!handles.count(id)) {
								HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
								                        THREAD_QUERY_INFORMATION,
								                      FALSE, id);
								if (h) {
									handles[id] = h;
									index[id] = static_cast<uint32_t>(threadIds.size());
									threadIds.push_back(id);
								}
							}
					}
					for (DWORD id : ids) {
						auto it = handles.find(id);
						if (it == handles.end() || samples.size() >= kMaxSamples)
							continue;
						if (id != mainThreadId && tick % kOtherThreadsEvery != 0)
							continue;
						if (SuspendThread(it->second) == static_cast<DWORD>(-1))
							continue;
						CONTEXT ctx{};
						ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
						if (GetThreadContext(it->second, &ctx)) {
							samples.emplace_back();
							Sample& s = samples.back();
							s.time = Now();
							s.thread = index[id];
							s.depth = static_cast<uint16_t>(Walk(ctx, s.pcs));
						}
						ResumeThread(it->second);
					}
					Sleep(1);
				}
				for (auto& h : handles)
					CloseHandle(h.second);
			}

			struct Symbols {
				HANDLE process = GetCurrentProcess();
				std::unordered_map<uint64_t, std::string> names;
				Symbols() {
					SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
					SymInitialize(process, nullptr, TRUE);
				}
				~Symbols() { SymCleanup(process); }
				const std::string& Name(uint64_t pc) {
					auto it = names.find(pc);
					if (it != names.end())
						return it->second;
					char buf[sizeof(SYMBOL_INFO) + 512];
					auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
					sym->SizeOfStruct = sizeof(SYMBOL_INFO);
					sym->MaxNameLen = 511;
					DWORD64 disp = 0;
					std::string name;
					if (SymFromAddr(process, pc, &disp, sym)) {
						name = sym->Name;
					} else {
						IMAGEHLP_MODULE64 mod{};
						mod.SizeOfStruct = sizeof(mod);
						char hex[32];
						std::snprintf(hex, sizeof(hex), "0x%llx",
						              static_cast<unsigned long long>(pc));
						name = SymGetModuleInfo64(process, pc, &mod)
						         ? std::string(mod.ModuleName) + "!" + hex
						         : std::string(hex);
					}
					if (SymGetModuleBase64(process, pc)) {
						IMAGEHLP_MODULE64 mod{};
						mod.SizeOfStruct = sizeof(mod);
						if (SymGetModuleInfo64(process, pc, &mod) &&
						    _stricmp(mod.ModuleName, "ZeroSpades") != 0 &&
						    name.find('!') == std::string::npos)
							name = std::string(mod.ModuleName) + "!" + name;
					}
					return names[pc] = name;
				}
			};

			void Report() {
				FILE* f = std::fopen(reportPath.c_str(), "w");
				if (!f) {
					SPLog("[Profile] Cannot write %s", reportPath.c_str());
					return;
				}
				Symbols sym;
				const uint32_t mainIndex = static_cast<uint32_t>(
				  std::find(threadIds.begin(), threadIds.end(), mainThreadId) -
				  threadIds.begin());

				// Frames, and each one's stages, from the marks
				struct Frame {
					int64_t start, end;
					int64_t stage[4] = {0, 0, 0, 0};
				};
				std::vector<Frame> frames;
				for (size_t i = 0; i < marks.size(); i++) {
					if (marks[i].stage != 0)
						continue;
					Frame fr;
					fr.start = marks[i].time;
					size_t j = i + 1;
					int stage = 0;
					int64_t at = fr.start;
					for (; j < marks.size() && marks[j].stage != 0; j++) {
						fr.stage[stage] += marks[j].time - at;
						at = marks[j].time;
						stage = std::min(marks[j].stage, 3);
					}
					if (j >= marks.size())
						break;
					fr.end = marks[j].time;
					fr.stage[stage] += fr.end - at;
					frames.push_back(fr);
				}
				auto ms = [](int64_t t) { return Seconds(t) * 1000.0; };

				std::vector<double> times;
				for (auto& fr : frames)
					times.push_back(ms(fr.end - fr.start));
				std::vector<double> sorted = times;
				std::sort(sorted.begin(), sorted.end());
				auto pct = [&](double p) {
					return sorted.empty() ? 0.0
					                      : sorted[std::min(sorted.size() - 1,
					                                        static_cast<size_t>(p * sorted.size()))];
				};
				const double median = pct(0.5);
				// A freeze: three times the usual frame and 15 ms over it at least
				const double freezeMs = std::max(median * 3.0, median + 15.0);
				std::vector<size_t> freezes;
				for (size_t i = 0; i < frames.size(); i++)
					if (times[i] > freezeMs)
						freezes.push_back(i);

				const double run = Seconds(Now() - startTime);
				std::fprintf(f, "ZeroSpades sampling profile (core_profile)\n\n");
				std::fprintf(f, "Run %.1f s, %zu samples over %zu threads, %zu frames\n", run,
				             samples.size(), threadIds.size(), frames.size());
				std::fprintf(f,
				             "Frame ms: median %.2f, 90%% %.2f, 99%% %.2f, 99.9%% %.2f, max %.1f\n",
				             median, pct(0.9), pct(0.99), pct(0.999),
				             sorted.empty() ? 0.0 : sorted.back());
				std::fprintf(f, "Freezes (over %.1f ms): %zu\n\n", freezeMs, freezes.size());

				// Which frame a time is in
				auto frameAt = [&](int64_t t) -> long {
					auto it = std::upper_bound(
					  frames.begin(), frames.end(), t,
					  [](int64_t v, const Frame& fr) { return v < fr.start; });
					if (it == frames.begin())
						return -1;
					--it;
					return t < it->end ? static_cast<long>(it - frames.begin()) : -1;
				};
				std::vector<char> isFreeze(frames.size(), 0);
				for (size_t i : freezes)
					isFreeze[i] = 1;

				const char* stageNames[4] = {"events", "update", "render+present", "after"};
				std::fprintf(f, "Frames that froze (first 80): at s, ms, then each stage's ms, "
				                "then the main thread's leaf functions in it\n");
				for (size_t k = 0; k < freezes.size() && k < 80; k++) {
					const Frame& fr = frames[freezes[k]];
					std::fprintf(f, "  %8.2f s  %7.1f ms  [", Seconds(fr.start - startTime),
					             times[freezes[k]]);
					for (int s = 0; s < 4; s++)
						std::fprintf(f, "%s %.1f%s", stageNames[s], ms(fr.stage[s]),
						             s < 3 ? ", " : "]");
					std::map<std::string, int> leaves;
					for (const Sample& smp : samples)
						if (smp.thread == mainIndex && smp.time >= fr.start &&
						    smp.time < fr.end && smp.depth > 0) {
							// The first frame of ours from the leaf: what the game asked
							std::string leaf = sym.Name(smp.pcs[0]);
							for (int d = 0; d < smp.depth; d++) {
								const std::string& n = sym.Name(smp.pcs[d]);
								if (n.find('!') == std::string::npos) {
									if (d > 0)
										leaf += " <- " + n;
									break;
								}
							}
							leaves[leaf]++;
						}
					std::vector<std::pair<int, std::string>> top;
					for (auto& l : leaves)
						top.push_back({l.second, l.first});
					std::sort(top.rbegin(), top.rend());
					for (size_t t = 0; t < top.size() && t < 3; t++)
						std::fprintf(f, "\n      %3d  %s", top[t].first, top[t].second.c_str());
					std::fprintf(f, "\n");
				}

				// The stages' average, in every frame and in the frozen ones
				double all[4] = {0, 0, 0, 0}, frozen[4] = {0, 0, 0, 0};
				for (size_t i = 0; i < frames.size(); i++)
					for (int s = 0; s < 4; s++) {
						all[s] += ms(frames[i].stage[s]);
						if (isFreeze[i])
							frozen[s] += ms(frames[i].stage[s]);
					}
				std::fprintf(f, "\nStage ms, average of all frames / of the frozen ones:\n");
				for (int s = 0; s < 4; s++)
					std::fprintf(f, "  %-15s %7.2f / %7.2f\n", stageNames[s],
					             frames.empty() ? 0.0 : all[s] / frames.size(),
					             freezes.empty() ? 0.0 : frozen[s] / freezes.size());

				// Functions by the share of samples they are in (inclusive) and on top
				// of (exclusive), for a set of samples
				auto table = [&](const char* title, auto&& pick) {
					std::map<std::string, int> incl, excl;
					int total = 0;
					for (const Sample& smp : samples) {
						if (!pick(smp) || smp.depth == 0)
							continue;
						total++;
						excl[sym.Name(smp.pcs[0])]++;
						std::set<std::string> seen;
						for (int d = 0; d < smp.depth; d++)
							seen.insert(sym.Name(smp.pcs[d]));
						for (auto& n : seen)
							incl[n]++;
					}
					std::fprintf(f, "\n%s: %d samples\n", title, total);
					for (int which = 0; which < 2; which++) {
						auto& m = which ? excl : incl;
						std::vector<std::pair<int, std::string>> v;
						for (auto& e : m)
							v.push_back({e.second, e.first});
						std::sort(v.rbegin(), v.rend());
						std::fprintf(f, "  %s:\n", which ? "self" : "total (with what it calls)");
						for (size_t i = 0; i < v.size() && i < 45; i++)
							std::fprintf(f, "    %5.1f%%  %s\n", 100.0 * v[i].first / std::max(1, total),
							             v[i].second.c_str());
					}
				};
				table("Main thread, the whole run",
				      [&](const Sample& s) { return s.thread == mainIndex; });
				table("Main thread, in the frames that froze", [&](const Sample& s) {
					if (s.thread != mainIndex)
						return false;
					long i = frameAt(s.time);
					return i >= 0 && isFreeze[static_cast<size_t>(i)];
				});
				// Every other thread, by its busiest function (a thread that sleeps
				// shows its wait)
				std::map<uint32_t, std::map<std::string, int>> perThread;
				std::map<uint32_t, int> perThreadCount;
				for (const Sample& smp : samples)
					if (smp.thread != mainIndex && smp.depth > 0) {
						perThreadCount[smp.thread]++;
						// The deepest frame that is the game's or OpenAL's own
						std::string name = sym.Name(smp.pcs[0]);
						for (int d = 0; d < smp.depth; d++) {
							const std::string& n = sym.Name(smp.pcs[d]);
							if (n.find('!') == std::string::npos) {
								name = n;
								break;
							}
						}
						perThread[smp.thread][name]++;
					}
				std::fprintf(f, "\nOther threads (their 5 commonest places of ours):\n");
				for (auto& t : perThread) {
					std::fprintf(f, "  thread %lu, %d samples\n",
					             static_cast<unsigned long>(threadIds[t.first]),
					             perThreadCount[t.first]);
					std::vector<std::pair<int, std::string>> v;
					for (auto& e : t.second)
						v.push_back({e.second, e.first});
					std::sort(v.rbegin(), v.rend());
					for (size_t i = 0; i < v.size() && i < 5; i++)
						std::fprintf(f, "    %5.1f%%  %s\n",
						             100.0 * v[i].first / std::max(1, perThreadCount[t.first]),
						             v[i].second.c_str());
				}

				// The commonest whole stacks of the frozen frames
				std::map<std::string, int> stacks;
				for (const Sample& smp : samples) {
					if (smp.thread != mainIndex || smp.depth == 0)
						continue;
					long i = frameAt(smp.time);
					if (i < 0 || !isFreeze[static_cast<size_t>(i)])
						continue;
					std::string st;
					for (int d = 0; d < smp.depth && d < 18; d++)
						st += (d ? "\n        <- " : "        ") + sym.Name(smp.pcs[d]);
					stacks[st]++;
				}
				std::vector<std::pair<int, std::string>> sv;
				for (auto& e : stacks)
					sv.push_back({e.second, e.first});
				std::sort(sv.rbegin(), sv.rend());
				std::fprintf(f, "\nCommonest stacks in the frozen frames (main thread):\n");
				for (size_t i = 0; i < sv.size() && i < 25; i++)
					std::fprintf(f, "  %d samples:\n%s\n", sv[i].first, sv[i].second.c_str());
				std::fclose(f);

				// Folded stacks, root first, for a flame graph
				FILE* g = std::fopen((reportPath + ".folded").c_str(), "w");
				if (g) {
					std::map<std::string, int> folded;
					for (const Sample& smp : samples) {
						if (smp.depth == 0)
							continue;
						std::string st = smp.thread == mainIndex
						                   ? "main"
						                   : "thread " + std::to_string(threadIds[smp.thread]);
						for (int d = smp.depth - 1; d >= 0; d--)
							st += ";" + sym.Name(smp.pcs[d]);
						folded[st]++;
					}
					for (auto& e : folded)
						std::fprintf(g, "%s %d\n", e.first.c_str(), e.second);
					std::fclose(g);
				}
				SPLog("[Profile] Written %s (%zu samples, %zu frames, %zu froze)",
				      reportPath.c_str(), samples.size(), frames.size(), freezes.size());
			}
		} // namespace

		void Start(const std::string& path) {
			if (running.load())
				return;
			reportPath = path;
			mainThreadId = GetCurrentThreadId();
			startTime = Now();
			samples.reserve(kMaxSamples);
			marks.reserve(1000000);
			running = true;
			sampler = std::thread(Run);
			SPLog("[Profile] Sampling every thread to %s", path.c_str());
		}

		void MarkFrame() {
			if (running.load(std::memory_order_relaxed))
				marks.push_back({Now(), 0});
		}

		void MarkStage(int stage) {
			if (running.load(std::memory_order_relaxed))
				marks.push_back({Now(), stage});
		}

		void Stop() {
			if (!running.load())
				return;
			running = false;
			sampler.join();
			marks.push_back({Now(), 0}); // the last frame's end
			Report();
		}

		bool IsRunning() { return running.load(); }
	} // namespace SamplingProfiler
} // namespace spades

#else

namespace spades {
	namespace SamplingProfiler {
		void Start(const std::string&) {
			SPLog("[Profile] core_profile: only on 64-bit Windows");
		}
		void MarkFrame() {}
		void MarkStage(int) {}
		void Stop() {}
		bool IsRunning() { return false; }
	} // namespace SamplingProfiler
} // namespace spades

#endif
