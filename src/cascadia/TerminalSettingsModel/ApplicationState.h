/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- ApplicationState.h

--*/
#pragma once

#include "ApplicationState.g.h"

#include "../inc/cppwinrt_utils.h"
#include "til/mutex.h"
#include "til/throttled_func.h"

#define MTSM_APPLICATION_STATE_FIELDS(X)         \
    X(bool, CloseAllTabsWarningDismissed, false) \
    X(bool, LargePasteWarningDismissed, false)   \
    X(bool, MultiLinePasteWarningDismissed, false)

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    struct ApplicationState : ApplicationStateT<ApplicationState>
    {
        static Microsoft::Terminal::Settings::Model::ApplicationState SharedInstance();

        ApplicationState(std::filesystem::path path) noexcept;
        ~ApplicationState();

        // ApplicationState uses its `this` pointer when creating _timer.
        // Since the timer cannot be recreated, instances cannot be moved either.
        ApplicationState(const ApplicationState&) = delete;
        ApplicationState& operator=(const ApplicationState&) = delete;
        ApplicationState(ApplicationState&&) = delete;
        ApplicationState& operator=(ApplicationState&&) = delete;

        void Reload() const noexcept;

#define MTSM_APPLICATION_STATE_GEN(type, name, ...) \
public:                                             \
    type name() const noexcept;                     \
    void name(const type& value) noexcept;
        MTSM_APPLICATION_STATE_FIELDS(MTSM_APPLICATION_STATE_GEN)
#undef MTSM_APPLICATION_STATE_GEN

    private:
        static void _synchronizeCallback(PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_TIMER timer) noexcept;

        void _synchronize() const noexcept;
        void _write() const noexcept;
        void _read() const noexcept;

        struct state_t
        {
#define MTSM_APPLICATION_STATE_GEN(type, name, ...) type name{ __VA_ARGS__ };
            MTSM_APPLICATION_STATE_FIELDS(MTSM_APPLICATION_STATE_GEN)
#undef MTSM_APPLICATION_STATE_GEN

            bool _writeScheduled;
        };

        std::filesystem::path _path;
        wil::unique_threadpool_timer _timer;
        til::shared_mutex<state_t> _state;
    };
}

namespace winrt::Microsoft::Terminal::Settings::Model::factory_implementation
{
    BASIC_FACTORY(ApplicationState);
}
