#pragma once

#include "GeoToolbox/StlExtensions.hpp"

#include <chrono>
#include <sstream>
#include <unordered_map>

namespace GeoToolbox
{
	template <typename T>
	T DoNotOptimize(T value)
	{
		static T volatile zero = 0;
		return value + zero;
	}

	class Stopwatch
	{
		using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

		TimePoint start_;
		bool isRunning_ = false;

	public:

		explicit Stopwatch(bool start = true) noexcept
		{
			if (start)
			{
				Start();
			}
		}

		[[nodiscard]] bool IsRunning() const noexcept
		{
			return isRunning_;
		}

		void Start() noexcept
		{
			start_ = std::chrono::high_resolution_clock::now();
			isRunning_ = true;
		}

		void Stop() noexcept
		{
			isRunning_ = false;
		}

		[[nodiscard]] int64_t ElapsedMicroseconds() const noexcept
		{
			using namespace std::chrono;
			return isRunning_ ? int64_t(duration_cast<microseconds>(high_resolution_clock::now() - start_).count()) : 0;
		}

		[[nodiscard]] int ElapsedMilliseconds() const noexcept
		{
			using namespace std::chrono;
			return isRunning_ ? int(duration_cast<milliseconds>(high_resolution_clock::now() - start_).count()) : 0;
		}
	};


	struct MeasureResult
	{
		double result;
		int64_t timeUs;
	};

	template <class TFunction>
	MeasureResult Measure(int64_t repeats, TFunction measurable)
	{
		auto result = 0.0;
		Stopwatch const timer;
		// DoNotOptimize prevents loop unrolling, which works by unknown compiler rules and adds a random factor to the measurement
		for (int64_t i = 0; i < DoNotOptimize(repeats); ++i)
		{
			result += measurable(i);
		}

		auto runTime = timer.ElapsedMicroseconds();
		return MeasureResult{ result, runTime };
	}

	static constexpr auto Kilo = 1'000;

	inline std::string PrintMicroSeconds(double us)
	{
		if (us < Kilo)
		{
			return std::to_string(us) + "us";
		}

		if (us < Kilo * Kilo)
		{
			return std::to_string(us / Kilo) + "ms";
		}

		return std::to_string(us / (Kilo * Kilo)) + "s";
	}

	inline std::string PrintMicroSeconds(int64_t us)
	{
		return PrintMicroSeconds(double(us));
	}


	class Timings
	{
	public:

		struct ActionStats
		{
			int64_t totalTime = 0;
			double bestTime = std::numeric_limits<double>::max();
			int64_t iterationCount = 0;
			int64_t memoryDelta = std::numeric_limits<int64_t>::max();
			bool failed = false;
		};

		using ContainerType = std::unordered_map<std::string, ActionStats>;

	private:

		class Recorder final
		{
			Timings* owner_ = nullptr;
			std::string actionName_;
			int repeats_ = 1;
			SharedAllocatedSize allocStats_;
			int64_t initialMemory_ = -1;
			ActionStats** stats_ = nullptr;
			Stopwatch actionTimer_{ false };

		public:
			Recorder(Timings& owner, std::string actionName, int repeats = 1, SharedAllocatedSize allocStats = nullptr, ActionStats** stats = nullptr)
				: owner_{ &owner }
				, actionName_{ std::move(actionName) }
				, repeats_{ repeats }
				, allocStats_{ std::move(allocStats) }
				, stats_(stats)
			{
				if (allocStats_ != nullptr)
				{
					initialMemory_ = allocStats_->load();
				}

				actionTimer_.Start();
			}

			Recorder(Recorder const&) = delete;
			Recorder(Recorder&&) = default;
			Recorder& operator=(Recorder const&) = delete;
			Recorder& operator=(Recorder&&) = default;

			~Recorder() noexcept
			{
				auto const us = actionTimer_.ElapsedMicroseconds();
				try
				{
					auto& stats = owner_->AddSample(actionName_, us, repeats_, allocStats_ != nullptr ? allocStats_->load() - initialMemory_ : 0);
					if (stats_ != nullptr)
					{
						*stats_ = &stats;
					}
				}
				catch (...)
				{
					if (stats_ != nullptr)
					{
						*stats_ = nullptr;
					}
				}
			}
		};


		int64_t const minimumRunningTimeUs_;

		int const maximumIterationCount_;

		ContainerType actions_;

		Stopwatch timer_{ false };

		int64_t totalRunningTime_ = 0;

		int64_t iterationStartTime_ = 0;

		int64_t bestIterationTime_ = std::numeric_limits<int64_t>::max();

		int iterationCount_ = 0;

	public:

		static constexpr int64_t UsPerSecond = 1'000'000;

		static constexpr auto MaximumIterationCount = 10'000;


		explicit Timings(int64_t minimumRunningTimeUs = 0, int maximumIterationCount = SelectDebugRelease(1, MaximumIterationCount))
			: minimumRunningTimeUs_(minimumRunningTimeUs > 0 ? minimumRunningTimeUs : UsPerSecond)
			, maximumIterationCount_(maximumIterationCount)
		{
		}

		Timings(Timings const&) = delete;
		Timings(Timings&&) = delete;
		~Timings() = default;
		Timings& operator=(Timings const&) = delete;
		Timings& operator=(Timings&&) = delete;

		ActionStats& AddSample(std::string const& actionName, int64_t runTime, int repeats = 1, int64_t memoryDelta = 0)
		{
			auto& action = actions_[actionName];
			action.iterationCount += repeats;
			action.totalTime += runTime;
			action.bestTime = std::min(action.bestTime, double(runTime) / repeats);
			action.memoryDelta = std::min(action.memoryDelta, memoryDelta / repeats);
			return action;
		}

		[[nodiscard]] ContainerType const& GetAllActions() const noexcept
		{
			return actions_;
		}

		[[nodiscard]] int64_t MinimumRunningTime() const noexcept
		{
			return minimumRunningTimeUs_;
		}

		[[nodiscard]] int64_t TotalRunningTime() const noexcept
		{
			return totalRunningTime_;
		}

		[[nodiscard]] int64_t AverageIterationTime() const noexcept
		{
			return iterationCount_ > 0 ? totalRunningTime_ / iterationCount_ : 0;
		}

		[[nodiscard]] int64_t BestIterationTime() const noexcept
		{
			return bestIterationTime_;
		}

		[[nodiscard]] int IterationCount() const noexcept
		{
			return iterationCount_;
		}

		Recorder BeginRecordedScope(std::string actionName)
		{
			return Recorder{ *this, std::move(actionName) };
		}

		template <class F>
		std::invoke_result_t<F> Record(std::string actionName, F action, ActionStats** stats = nullptr)
		{
			[[maybe_unused]] Recorder const recorder{ *this, std::move(actionName), 1, nullptr, stats };
			return action();
		}

		template <class F>
		std::invoke_result_t<F> Record(std::string actionName, SharedAllocatedSize const& allocatorStats, F action, ActionStats** stats = nullptr)
		{
			[[maybe_unused]] Recorder const recorder{ *this, std::move(actionName), 1, allocatorStats, stats };
			return action();
		}

		template <class F>
		std::invoke_result_t<F> Record(std::string actionName, int repeats, F action, ActionStats** stats = nullptr)
		{
			[[maybe_unused]] Recorder const recorder{ *this, std::move(actionName), repeats, nullptr, stats };
			for (auto i = 0; i < repeats - 1; ++i)
			{
				action();
			}

			return action();
		}

		[[nodiscard]] bool NextIteration() noexcept
		{
			if (!timer_.IsRunning())
			{
				++iterationCount_;
				iterationStartTime_ = 0;
				bestIterationTime_ = std::numeric_limits<int64_t>::max();
				timer_.Start();
				return true;
			}

			auto const time = timer_.ElapsedMicroseconds() - iterationStartTime_;
			bestIterationTime_ = std::min(bestIterationTime_, time);

			if (iterationCount_ >= maximumIterationCount_ || timer_.ElapsedMicroseconds() > minimumRunningTimeUs_)
			{
				totalRunningTime_ = timer_.ElapsedMicroseconds();
				return false;
			}

			++iterationCount_;
			iterationStartTime_ = timer_.ElapsedMicroseconds();
			return true;
		}

		static std::string Print(ContainerType const& actions)
		{
			std::ostringstream buffer;
			for (auto const& action : actions)
			{
				buffer << action.first << ": " << PrintMicroSeconds(action.second.bestTime)
					<< " / " << action.second.iterationCount << " iterations in " << PrintMicroSeconds(action.second.totalTime);
				if (action.second.memoryDelta != 0)
				{
					buffer << ", mem delta: " << action.second.memoryDelta;
				}

				buffer << '\n';
			}

			return buffer.str();
		}

		[[nodiscard]] std::string Print() const
		{
			std::ostringstream buffer;
			buffer << "Total time: " << PrintMicroSeconds(totalRunningTime_) << ", iterations: " << iterationCount_ << '\n' << Print(actions_);

			return buffer.str();
		}

		void Reset()
		{
			actions_.clear();
			timer_.Stop();
			totalRunningTime_ = 0;
			iterationCount_ = 0;
		}
	};
}
