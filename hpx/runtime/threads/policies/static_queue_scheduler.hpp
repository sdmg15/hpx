//  Copyright (c) 2007-2017 Hartmut Kaiser
//  Copyright (c) 2011      Bryce Lelbach
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#if !defined(HPX_THREADMANAGER_SCHEDULING_STATIC_QUEUE_JUL_22_2015_0103PM)
#define HPX_THREADMANAGER_SCHEDULING_STATIC_QUEUE_JUL_22_2015_0103PM

#include <hpx/config.hpp>

#if defined(HPX_HAVE_STATIC_SCHEDULER)
#include <hpx/assertion.hpp>
#include <hpx/runtime/threads/policies/local_queue_scheduler.hpp>
#include <hpx/runtime/threads/policies/lockfree_queue_backends.hpp>
#include <hpx/runtime/threads/policies/thread_queue.hpp>
#include <hpx/runtime/threads/thread_data.hpp>
#include <hpx/topology/topology.hpp>
#include <hpx/runtime/threads_fwd.hpp>
#include <hpx/logging.hpp>

#include <mutex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <hpx/config/warnings_prefix.hpp>

// TODO: add branch prediction and function heat

///////////////////////////////////////////////////////////////////////////////
namespace hpx { namespace threads { namespace policies
{
#ifdef HPX_HAVE_THREAD_MINIMAL_DEADLOCK_DETECTION
    ///////////////////////////////////////////////////////////////////////////
    // We globally control whether to do minimal deadlock detection using this
    // global bool variable. It will be set once by the runtime configuration
    // startup code
    extern bool minimal_deadlock_detection;
#endif

    ///////////////////////////////////////////////////////////////////////////
#if defined(HPX_HAVE_CXX11_STD_ATOMIC_128BIT)
    using default_static_queue_scheduler_terminated_queue = lockfree_lifo;
#else
    using default_static_queue_scheduler_terminated_queue = lockfree_fifo;
#endif

    ///////////////////////////////////////////////////////////////////////////
    /// The local_queue_scheduler maintains exactly one queue of work
    /// items (threads) per OS thread, where this OS thread pulls its next work
    /// from.
    template <typename Mutex = std::mutex,
        typename PendingQueuing = lockfree_fifo,
        typename StagedQueuing = lockfree_fifo,
        typename TerminatedQueuing =
            default_static_queue_scheduler_terminated_queue>
    class static_queue_scheduler
      : public local_queue_scheduler<Mutex, PendingQueuing, StagedQueuing,
            TerminatedQueuing>
    {
    public:
        typedef local_queue_scheduler<
            Mutex, PendingQueuing, StagedQueuing, TerminatedQueuing
        > base_type;

        static_queue_scheduler(
                typename base_type::init_parameter_type const& init,
                bool deferred_initialization = true)
          : base_type(init, deferred_initialization)
        {}

        static std::string get_scheduler_name()
        {
            return "static_queue_scheduler";
        }

        void set_scheduler_mode(scheduler_mode mode) override
        {
            // this scheduler does not support stealing or numa stealing
            mode = scheduler_mode(mode & ~scheduler_mode::enable_stealing);
            mode = scheduler_mode(mode & ~scheduler_mode::enable_stealing_numa);
            scheduler_base::set_scheduler_mode(mode);
        }

        /// Return the next thread to be executed, return false if none is
        /// available
        bool get_next_thread(std::size_t num_thread, bool,
            threads::thread_data*& thrd, bool /*enable_stealing*/) override
        {
            typedef typename base_type::thread_queue_type thread_queue_type;

            {
                HPX_ASSERT(num_thread < this->queues_.size());

                thread_queue_type* q = this->queues_[num_thread];
                bool result = q->get_next_thread(thrd);

                q->increment_num_pending_accesses();
                if (result)
                    return true;
                q->increment_num_pending_misses();
            }

            return false;
        }

        /// This is a function which gets called periodically by the thread
        /// manager to allow for maintenance tasks to be executed in the
        /// scheduler. Returns true if the OS thread calling this function
        /// has to be terminated (i.e. no more work has to be done).
        bool wait_or_add_new(std::size_t num_thread, bool running,
            std::int64_t& idle_loop_count, bool /*enable_stealing*/,
            std::size_t& added) override
        {
            HPX_ASSERT(num_thread < this->queues_.size());

            added = 0;

            bool result = true;

            result =
                this->queues_[num_thread]->wait_or_add_new(running, added) &&
                result;
            if (0 != added) return result;

            // Check if we have been disabled
            if (!running)
            {
                return true;
            }

#ifdef HPX_HAVE_THREAD_MINIMAL_DEADLOCK_DETECTION
            // no new work is available, are we deadlocked?
            if (HPX_UNLIKELY(minimal_deadlock_detection && LHPX_ENABLED(error)))
            {
                bool suspended_only = true;

                for (std::size_t i = 0;
                     suspended_only && i != this->queues_.size(); ++i)
                {
                    suspended_only = this->queues_[i]->dump_suspended_threads(
                        i, idle_loop_count, running);
                }

                if (HPX_UNLIKELY(suspended_only)) {
                    LTM_(error) //-V128
                        << "queue(" << num_thread << "): "
                        << "no new work available, are we deadlocked?";
                }
            }
#endif

            return result;
        }
    };
}}}

#include <hpx/config/warnings_suffix.hpp>

#endif
#endif

