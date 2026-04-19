// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "GeoToolbox/Asserts.hpp"
#include "GeoToolbox/StlExtensions.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#define TRACK_ALLOCATED_MEMORY 01

void* TrackedMalloc(size_t size);
void TrackedFree(void* block);

namespace GeoToolbox
{
	inline std::atomic<size_t> TotalAllocatedSize;


	template <typename T>
	T DoNotOptimize(T value)
	{
		static T volatile zero = 0;
		return value + zero;
	}

	class Stopwatch
	{
		using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

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
			start_ = std::chrono::steady_clock::now();
			isRunning_ = true;
		}

		void Stop() noexcept
		{
			isRunning_ = false;
		}

		[[nodiscard]] int64_t ElapsedMicroseconds() const noexcept
		{
			using namespace std::chrono;
			return isRunning_ ? int64_t(duration_cast<microseconds>(steady_clock::now() - start_).count()) : 0;
		}

		[[nodiscard]] int ElapsedMilliseconds() const noexcept
		{
			using namespace std::chrono;
			return isRunning_ ? int(duration_cast<milliseconds>(steady_clock::now() - start_).count()) : 0;
		}
	};


	template <typename T>
	class AggregateStats
	{
	public:
		AggregateStats() noexcept = default;

		void AddValue( T value ) noexcept
		{
			minimum_ = std::min( minimum_, value );
			maximum_ = std::max( maximum_, value );
			++count_;
			sum_ += value;
		}

		[[nodiscard]] bool IsEmpty() const noexcept
		{
			return count_ == 0;
		}

		[[nodiscard]] int Count() const noexcept
		{
			return count_;
		}

		[[nodiscard]] T Minimum() const noexcept
		{
			return minimum_;
		}

		[[nodiscard]] T Maximum() const noexcept
		{
			return maximum_;
		}

		[[nodiscard]] T Sum() const noexcept
		{
			return sum_;
		}

		[[nodiscard]] double Average() const noexcept
		{
			return count_ > 0 ? sum_ / double( count_ ) : 0;
		}

		[[nodiscard]] bool IsConstant() const noexcept
		{
			return count_ > 0 && minimum_ == maximum_;
		}

		[[nodiscard]] bool IsConstant( T x ) const noexcept
		{
			return count_ > 0 && x == minimum_ && x == maximum_;
		}


		void clear() noexcept
		{
			count_ = 0;
			sum_ = 0;
		}

		friend std::ostream& operator<<( std::ostream& stream, AggregateStats const& stats )
		{
			stream << '[' << stats.Count() << "] " << std::fixed << std::setprecision( 1 ) << stats.Minimum() << "  " << stats.Average() << "  " << stats.Maximum();
			//stream << stats.minimum_ << '/' << stats.Average() << '/' << stats.maximum_ << " [" << stats.count_;
			//stream << '/' << stats.sum_ << ']';
			return stream;
		}

	private:
		int count_ = 0;
		T minimum_ = std::numeric_limits<T>::max();
		T maximum_ = std::numeric_limits<T>::lowest();
		T sum_ = 0;
	};


	class ProfileMemoryResource : public std::pmr::memory_resource
	{
		memory_resource* upstream_;
		ptrdiff_t allocationsCount = 0;
		ptrdiff_t deallocationsCount = 0;
		ptrdiff_t allocatedSize = 0;
		ptrdiff_t deallocatedSize = 0;

	public:

		explicit ProfileMemoryResource(memory_resource* upstream = nullptr)
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

		void Remove(std::size_t size, std::size_t count = 1) const noexcept
		{
			*stats -= size * count;
		}

		[[nodiscard]] std::int64_t GetTotalAllocated() const
		{
			return stats->load();
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

		void deallocate(pointer ptr, size_type count)
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

	// This is needed for profiling structures, whose memory usage should not be included when memory is tracked
	template<typename T>
	struct MallocAllocator : std::allocator<T>
	{
		using value_type = T;
		using size_type = size_t;
		using difference_type = ptrdiff_t;

		template<typename U>
		struct rebind
		{
			using other = MallocAllocator<U>;
		};

		MallocAllocator() = default;

		template<typename U>
		explicit MallocAllocator( MallocAllocator<U> const& u )
			: std::allocator<T>(u)
		{
		}

		T* allocate( size_t size, void const* /*hint*/ = nullptr )
		{
			auto const result = std::malloc( size * sizeof( T ) );
			if ( result == nullptr )
			{
				throw std::bad_alloc();
			}

			return static_cast<T*>( result );
		}

		void deallocate( T* p, size_type )
		{
			std::free( p );
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

	inline std::string PrintMilliSeconds(double ms)
	{
		if (ms < Kilo)
		{
			return std::to_string(ms) + "ms";
		}

		return std::to_string(ms / Kilo) + "s";
	}

	inline std::string PrintMicroSeconds(double us)
	{
		if (us < Kilo)
		{
			return std::to_string(us) + "us";
		}

		return PrintMilliSeconds(us / Kilo);
	}

	inline std::string PrintMicroSeconds(int64_t us)
	{
		return PrintMicroSeconds(double(us));
	}

	//! Runs a set of named actions for several iterations until none of the actions improves its time and records the best time of each action
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
			std::shared_ptr<void> extra;
		};

		using ContainerType = std::unordered_map<char const*, ActionStats, std::hash<char const*>, std::equal_to<char const*>, MallocAllocator<std::pair<char const* const, ActionStats>>>;

	private:

		int64_t const minimumRunningTimeUs_;

		int const stopWhenNotImprovedNTimes_;

		int const maximumIterationCount_;

		ContainerType actions_;

		Stopwatch timer_{ false };

		int64_t totalRunningTime_ = 0;

		int64_t iterationStartTime_ = 0;

		int64_t bestIterationTime_ = std::numeric_limits<int64_t>::max();

		int iterationCount_ = 0;

		bool anyImproved_ = false;

		int notImprovedRuns_ = 0;

	public:

		static constexpr int64_t MsPerSecond = 1'000;

		static constexpr int64_t UsPerSecond = 1'000'000;

		static constexpr auto MaximumIterationCount = 10'000;


		explicit Timings(int64_t minimumRunningTimeMs = 0, int stopWhenNotImprovedNTimes = 0, int maximumIterationCount = 0)
			: minimumRunningTimeUs_{ minimumRunningTimeMs > 0 ? 1000 * minimumRunningTimeMs : UsPerSecond }
			, stopWhenNotImprovedNTimes_{ stopWhenNotImprovedNTimes }
			, maximumIterationCount_{ SelectDebugRelease(1, maximumIterationCount > 0 ? maximumIterationCount : MaximumIterationCount) }
		{
		}

		Timings(Timings const&) = delete;
		Timings(Timings&&) = delete;
		~Timings() = default;
		Timings& operator=(Timings const&) = delete;
		Timings& operator=(Timings&&) = delete;

		ActionStats& AddSample(char const* actionName, int64_t runTime, int repeats = 1, int64_t memoryDelta = 0)
		{
			auto& action = actions_[actionName];
			action.iterationCount += repeats;
			action.totalTime += runTime;
			auto const avgTime = double(runTime) / repeats;
			if (avgTime < action.bestTime)
			{
				action.bestTime = avgTime;
				anyImproved_ = true;
			}

			action.memoryDelta = std::min(action.memoryDelta, memoryDelta);
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

		template <class F>
		std::invoke_result_t<F> Record(char const* actionName, F action, ActionStats** statsPtr = nullptr, int64_t* elapsedUs = nullptr)
		{
			return Record(actionName, 1, nullptr, std::move(action), statsPtr, elapsedUs);
		}

		template <class F>
		std::invoke_result_t<F> Record(char const* actionName, int repeats, F action, ActionStats** statsPtr = nullptr, int64_t* elapsedUs = nullptr)
		{
			return Record(actionName, repeats, nullptr, std::move(action), statsPtr, elapsedUs);
		}

		template <class F>
		std::invoke_result_t<F> Record(char const* actionName, SharedAllocatedSize const& allocatorStats, F action, ActionStats** statsPtr = nullptr, int64_t* elapsedUs = nullptr)
		{
			return Record(actionName, 1, allocatorStats, std::move(action), statsPtr, elapsedUs);
		}

		template <class F>
		std::invoke_result_t<F> Record(char const* actionName, int repeats, SharedAllocatedSize const& allocatorStats, F action, ActionStats** statsPtr = nullptr, int64_t* elapsedUs = nullptr)
		{
			auto initialMemory = allocatorStats != nullptr ? allocatorStats->load() : int64_t(TotalAllocatedSize.load());
			Stopwatch actionTimer;

			for (auto i = 0; i < repeats - 1; ++i)
			{
				action();
			}

			[[maybe_unused]] std::conditional_t<std::is_void_v<std::invoke_result_t<F>>, int, std::invoke_result_t<F>> result;
			if constexpr (std::is_void_v<std::invoke_result_t<F>>)
			{
				action();
			}
			else
			{
				result = action();
			}

			auto const us = actionTimer.ElapsedMicroseconds();
			if (elapsedUs != nullptr)
			{
				*elapsedUs = us;
			}

			auto& stats = AddSample(actionName, us, repeats, (allocatorStats != nullptr ? allocatorStats->load() : int64_t(TotalAllocatedSize.load())) - initialMemory);
			if (statsPtr != nullptr)
			{
				*statsPtr = &stats;
			}

			if constexpr (!std::is_void_v<std::invoke_result_t<F>>)
			{
				return result;
			}
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
			if (time < bestIterationTime_)
			{
				bestIterationTime_ = time;
				anyImproved_ = true;
			}

			if (anyImproved_)
			{
				anyImproved_ = false;
				notImprovedRuns_ = 0;
			}
			else
			{
				++notImprovedRuns_;
			}

			if (iterationCount_ >= maximumIterationCount_ || timer_.ElapsedMicroseconds() > minimumRunningTimeUs_ && (stopWhenNotImprovedNTimes_ <= 0 || notImprovedRuns_ >= stopWhenNotImprovedNTimes_))
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
