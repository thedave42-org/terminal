// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "ApplicationState.h"
#include "CascadiaSettings.h"
#include "ApplicationState.g.cpp"

#include "JsonUtils.h"
#include "FileUtils.h"

static constexpr std::string_view CloseAllTabsWarningDismissedKey{ "closeAllTabsWarningDismissed" };
static constexpr std::string_view LargePasteWarningDismissedKey{ "largePasteWarningDismissed" };
static constexpr std::string_view MultiLinePasteWarningDismissedKey{ "multiLinePasteWarningDismissed" };

using namespace ::Microsoft::Terminal::Settings::Model;

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    // Returns the application-global ApplicationState object.
    Microsoft::Terminal::Settings::Model::ApplicationState ApplicationState::SharedInstance()
    {
        static auto state = winrt::make_self<ApplicationState>(GetBaseSettingsPath() / L"state.json");
        return *state;
    }

    ApplicationState::ApplicationState(std::filesystem::path path) noexcept :
        _path{ std::move(path) },
        _throttler{ std::chrono::seconds(1), [this]() { _write(); } }
    {
        _read();
    }

    // The destructor ensures that the last write is flushed to disk before returning.
    ApplicationState::~ApplicationState()
    {
        // In order to flush the last write to disk as soon as possible,
        // pending timers and wait for the completion of ongoing writes
        // (the destructor of wil::unique_threadpool_timer ensures to either
        // cancel pending timers or wait for in-progress callbacks to complete).
        //
        // If _writeScheduled is still true afterwards we must've
        // canceled a pending timer. -> _write() for the last time.
        _throttler.wait_for_completion();
        if (_state.lock_shared()->_writeScheduled)
        {
            _write();
        }
    }

    void ApplicationState::Reload() const noexcept
    {
        _read();
    }

#define MTSM_APPLICATION_STATE_GEN(type, name, ...)         \
    type ApplicationState::name() const noexcept            \
    {                                                       \
        const auto state = _state.lock_shared();            \
        return state->name;                                 \
    }                                                       \
                                                            \
    void ApplicationState::name(const type& value) noexcept \
    {                                                       \
        {                                                   \
            auto state = _state.lock();                     \
            state->name = value;                            \
        }                                                   \
                                                            \
        _throttler();                                       \
    }
    MTSM_APPLICATION_STATE_FIELDS(MTSM_APPLICATION_STATE_GEN)
#undef MTSM_APPLICATION_STATE_GEN

    // Deserializes the state.json at _path into this ApplicationState.
    // * *ANY* errors will result in the creation of a new empty state.
    // * Doesn't acquire any locks - may only be called by ApplicationState's constructor.
    void ApplicationState::_read() const noexcept
    try
    {
        const auto data = ReadUTF8FileIfExists(_path).value_or(std::string{});
        if (data.empty())
        {
            return;
        }

        std::string errs;
        std::unique_ptr<Json::CharReader> reader{ Json::CharReaderBuilder::CharReaderBuilder().newCharReader() };

        Json::Value root;
        if (!reader->parse(data.data(), data.data() + data.size(), &root, &errs))
        {
            throw winrt::hresult_error(WEB_E_INVALID_JSON_STRING, winrt::to_hstring(errs));
        }

        auto state = _state.lock();
#define MTSM_APPLICATION_STATE_GEN(type, name, ...) JsonUtils::GetValueForKey(root, name##Key, state->name);
        MTSM_APPLICATION_STATE_FIELDS(MTSM_APPLICATION_STATE_GEN)
#undef MTSM_APPLICATION_STATE_GEN
    }
    CATCH_LOG()

    // Serialized this ApplicationState (in `context`) into the state.json at _path.
    // * Errors are only logged.
    // * _state->_writeScheduled is set to false, signaling our
    //   setters that _synchronize() needs to be called again.
    void ApplicationState::_write() const noexcept
    try
    {
        Json::Value root{ Json::objectValue };

        {
            auto state = _state.lock_shared();
#define MTSM_APPLICATION_STATE_GEN(type, name, ...) JsonUtils::SetValueForKey(root, name##Key, state->name);
            MTSM_APPLICATION_STATE_FIELDS(MTSM_APPLICATION_STATE_GEN)
#undef MTSM_APPLICATION_STATE_GEN
        }

        Json::StreamWriterBuilder wbuilder;
        const auto content = Json::writeString(wbuilder, root);
        WriteUTF8FileAtomic(_path, content);
    }
    CATCH_LOG()
}
