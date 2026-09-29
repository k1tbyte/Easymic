#pragma once

#include "Core/Host.hpp"
#include "Event.hpp"

class AudioManager;

/// Actions run on the Dispatcher worker, the rest on the UI thread; OnStateChanged is raised there too.
namespace Mic {

    void Register(Host& host);

    AudioManager& Audio();

    bool HasDevice();
    bool Muted();

    void Refresh();

    extern IEvent<>& OnStateChanged;
}
