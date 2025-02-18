// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

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


	class ProfileMemoryResource : public std::pmr::memory_resource
	{
		memory_resource* upstream_;
		ptrdiff_t allocationsCount = 0;
		ptrdiff_t deallocationsCount = 0;
		ptrdiff_t allocatedSize = 0;
		ptrdiff_t deallocatedSize = 0;

	public:

		ProfileMemoryResource(memory_resource* upstream = nullptr)
			: upstream_{ upstream != nullptr ? upstream : std::pmr::get_default_resource() }
		{
		}

		~ProfileMemoryResource() override = default;
		ProfileMemoryResource(ProfileMemoryResource const&) = default;
		ProfileMemoryResource(ProfileMemoryResource&&) = default;
		ProfileMemoryResource& operator=(ProfileMemoryResource const&) = default;
		ProfileMemoryResource& operator=(ProfileMemoryResource&&) = default;

		[[nodiscard]] ptrdiff_t GetCurrentAllocationsCount() const noexcept
		{
			return allocationsCount - deallocationsCount;
		}

		[[nodiscard]] ptrdiff_t GetCurrentAllocatedSize() const noexcept
		{
			return allocatedSize - deallocatedSize;
		}

	private:

		void* do_allocate(size_t bytes, size_t alignment) override
		{
			++allocationsCount;
			allocatedSize += ptrdiff_t(bytes);
			return upstream_->allocate(bytes, alignment);
		}

		void do_deallocate(void* p, size_t bytes, size_t alignment) override
		{
			//ASSERT(deallocationsCount < allocationsCount);
			//ASSERT(deallocatedSize < allocatedSize);
			++deallocationsCount;
			deallocatedSize += ptrdiff_t(bytes);
			//if (deallocationsCount == allocationsCount)
			//{
			//	ASSERT(deallocatedSize == allocatedSize);
			//}

			return upstream_->deallocate(p, bytes, alignment);
		}

		[[nodiscard]] bool do_is_equal(memory_resource const& other) const noexcept override
		{
			return this == &other;
		}
	};

	struct TotalAllocatedStats
	{
		using StatsType = std::shared_ptr<std::atomic<std::int64_t>>;

		StatsType stats = std::make_shared<std::atomic<std::int64_t>>();

		~TotalAllocatedStats() = default;
		TotalAllocatedStats() = default;

		/*explicit(false)*/ TotalAllocatedStats(StatsType stats)
			: stats(std::move(stats))
		{
		}

		TotalAllocatedStats(TotalAllocatedStats const&) noexcept = default;
		TotalAllocatedStats& operator=(TotalAllocatedStats const&) noexcept = default;

		TotalAllocatedStats(TotalAllocatedStats&& other) noexcept
			: stats(other.stats)
		{
		}

		TotalAllocatedStats& operator=(TotalAllocatedStats&& other) noexcept
		{
			stats = other.stats;
			return *this;
		}

		void Add(std::size_t size, std::size_t count = 1) const
		{
			*stats += size * count;
		}

		void Remove(std::size_t size, std::size_t count = 1) const
		{
			*stats -= size * count;
		}
	};

	using SharedAllocatedSize = std::shared_ptr<std::atomic<std::int64_t>>;

	// Adapted from boost/container/new_allocator.hpp
	template <typename T, class TAllocator = std::allocator<T>, class TStats = TotalAllocatedStats>
	class ProfileAllocator : public TAllocator
	{
		TStats stats_;

	public:

		using value_type = typename TAllocator::value_type;
		using pointer = typename std::allocator_traits<TAllocator>::pointer;
		using const_pointer = typename std::allocator_traits<TAllocator>::const_pointer;
		//using reference = typename TAllocator::reference;
		//using const_reference = typename TAllocator::const_reference;
		using size_type = std::size_t;
		using difference_type = std::ptrdiff_t;

		template <class T2>
		struct rebind
		{
			using TAllocator2 = typename std::allocator_traits<TAllocator>::template rebind_alloc<T2>;
			using other = ProfileAllocator<T2, TAllocator2, TStats>;
		};

		~ProfileAllocator() = default;
		ProfileAllocator() = default;

		explicit ProfileAllocator(TStats stats) noexcept
			: stats_(std::move(stats))
		{
		}

		ProfileAllocator(ProfileAllocator const&) noexcept = default;
		ProfileAllocator(ProfileAllocator&&) noexcept = default;

		ProfileAllocator& operator=(ProfileAllocator const&) noexcept = default;
		ProfileAllocator& operator=(ProfileAllocator&&) noexcept = default;

		template <class T2, class TAllocator2>
		/*explicit( false )*/ ProfileAllocator(ProfileAllocator<T2, TAllocator2, TStats> const& other)
			: TAllocator(other)
			, stats_(other.GetStats())
		{
		}

		pointer allocate(size_type count)
		{
			static constexpr auto MaxCount = std::size_t(-1) / (2 * sizeof(value_type));
			if (count > MaxCount)
			{
				throw std::bad_alloc{};
			}

			stats_.Add(count, sizeof(value_type));
			return TAllocator::allocate(count);
		}

		void deallocate(pointer ptr, size_type count) noexcept
		{
			stats_.Remove(count, sizeof(value_type));
			TAllocator::deallocate(ptr, count);
		}

		friend void swap(ProfileAllocator& a, ProfileAllocator& b) noexcept
		{
			std::swap(a.stats_, b.stats_);
		}

		friend bool operator==(ProfileAllocator const& a, ProfileAllocator const& b) noexcept
		{
			//return static_cast<TAllocator const&>( a ) == static_cast<TAllocator const&>( b );
			return a.stats_ == b.stats_;
		}

		friend bool operator!=(ProfileAllocator const& a, ProfileAllocator const& b) noexcept
		{
			return !(a == b);
		}


		[[nodiscard]] TStats const& GetStats() const
		{
			return stats_;
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

		int64_t const minimumRunningTimeUs_;

		int const maximumIterationCount_;

		ContainerType actions_;

		Stopwatch timer_{ false };

		int64_t totalRunningTime_ = 0;

		int64_t iterationStartTime_ = 0;

		int64_t bestIterationTime_ = std::numeric_limits<int64_t>::max();

		int iterationCount_ = 0;


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
