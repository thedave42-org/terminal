// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

namespace til
{
    namespace details
    {
        template<typename... Args>
        class throttled_func_storage
        {
        public:
            template<typename... MakeArgs>
            bool emplace(MakeArgs&&... args)
            {
                std::scoped_lock guard{ _lock };

                const bool hadValue = _pendingRunArgs.has_value();
                _pendingRunArgs.emplace(std::forward<MakeArgs>(args)...);
                return hadValue;
            }

            template<typename F>
            void modify_pending(F f)
            {
                std::scoped_lock guard{ _lock };

                if (_pendingRunArgs.has_value())
                {
                    std::apply(f, _pendingRunArgs.value());
                }
            }

            std::tuple<Args...> extract()
            {
                decltype(_pendingRunArgs) args;
                std::scoped_lock guard{ _lock };
                _pendingRunArgs.swap(args);
                return args.value();
            }

        private:
            // std::mutex uses imperfect Critical Sections on Windows.
            // --> std::shared_mutex uses SRW locks that are small and fast.
            std::shared_mutex _lock;
            std::optional<std::tuple<Args...>> _pendingRunArgs;
        };

        template<>
        class throttled_func_storage<>
        {
        public:
            bool emplace()
            {
                return _isPending.exchange(true, std::memory_order_relaxed);
            }

            std::tuple<> extract()
            {
                reset();
                return {};
            }

            void reset()
            {
                _isPending.store(false, std::memory_order_relaxed);
            }

        private:
            std::atomic<bool> _isPending;
        };
    } // namespace details

    // Class Description:
    // - Represents a function that takes arguments and whose invocation is
    //   delayed by a specified duration and rate-limited such that if the code
    //   tries to run the function while a call to the function is already
    //   pending, then the previous call with the previous arguments will be
    //   cancelled and the call will be made with the new arguments instead.
    // - The function will be run on the the specified dispatcher.
    template<bool leading, typename... Args>
    class throttled_func
    {
    public:
        using Func = std::function<void(Args...)>;

        throttled_func(winrt::Windows::Foundation::TimeSpan delay, Func func) :
            _delay{ -delay.count() },
            _func{ std::move(func) },
            _timer{ winrt::check_pointer(CreateThreadpoolTimer(&_timer_callback, this, nullptr)) }
        {
        }

        // throttled_func uses its `this` pointer when creating _timer.
        // Since the timer cannot be recreated, instances cannot be moved either.
        throttled_func(const throttled_func&) = delete;
        throttled_func& operator=(const throttled_func&) = delete;
        throttled_func(throttled_func&&) = delete;
        throttled_func& operator=(throttled_func&&) = delete;

        // Method Description:
        // - Runs the function later with the specified arguments, except if it
        //   is called again before with new arguments, in which case the new
        //   arguments will be used instead.
        // - For more information, read the class' documentation.
        // - This method is always thread-safe. It can be called multiple times on
        //   different threads.
        // Arguments:
        // - args: the arguments to pass to the function
        // Return Value:
        // - <none>
        template<typename... MakeArgs>
        void operator()(MakeArgs&&... args)
        {
            if (!_storage.emplace(std::forward<MakeArgs>(args)...))
            {
                _fire();
            }
        }

        // Method Description:
        // - Modifies the pending arguments for the next function invocation, if
        //   there is one pending currently.
        // - Let's say that you just called the `operator()` method with some arguments.
        //   After the delay specified in the constructor, the function specified
        //   in the constructor will be called with these arguments.
        // - By using this method, you can modify the arguments before the function
        //   is called.
        // - You pass a function to this method which will take references to
        //   the arguments (one argument corresponds to one reference to an
        //   argument) and will modify them.
        // - When there is no pending invocation of the function, this method will
        //   not do anything.
        // - This method is always thread-safe. It can be called multiple times on
        //   different threads.
        // Arguments:
        // - f: the function to call with references to the arguments
        // Return Value:
        // - <none>
        template<typename F>
        void modify_pending(F f)
        {
            _storage.modify_pending(f);
        }

        // Method Description:
        // - Makes sure that all outstanding timers are canceled and
        //   in-progress ones are awaited on for their completion.
        // - Reason for its existence: We have code that needs to explicitly
        //   ensure that the throttled_func will not call the callback anymore.
        void wait_for_completion()
        {
            WaitForThreadpoolTimerCallbacks(_timer.get(), true);
        }

    private:
        void _fire()
        {
            if constexpr (leading)
            {
                self._func();
            }

            SetThreadpoolTimerEx(_timer.get(), reinterpret_cast<PFILETIME>(&_delay), 0, 0);
        }

        static void _timer_callback(PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_TIMER timer) noexcept
        try
        {
            auto& self = *static_cast<throttled_func*>(context);

            if constexpr (leading)
            {
                self._storage.reset();
            }
            else
            {
                std::apply(self._func, self._storage.extract());
            }
        }
        CATCH_LOG()

        int64_t _delay;
        Func _func;
        wil::unique_threadpool_timer _timer;

        details::throttled_func_storage<Args...> _storage;
    };

    template<typename... Args>
    using throttled_func_trailing = throttled_func<false, Args...>;
    using throttled_func_leading = throttled_func<true>;
} // namespace til
