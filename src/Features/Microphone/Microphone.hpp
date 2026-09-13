#pragma once

#include "Core/Host.hpp"
#include "Event.hpp"

class AudioManager;

/**
 * @brief The microphone: its actions, the state they change, and the device that reports back.
 *
 * The module owns the AudioManager, so nothing above it has to thread one through. Action bodies
 * run on the Dispatcher worker; everything else here is the UI thread, except the WASAPI
 * callbacks, which write atomics and then raise OnStateChanged.
 */
namespace Mic {

    /// Registers the actions and subscribes to the device. Once, before any config is restored.
    void Register(Host& host);

    /// The manager the module's own page, layer and tray provider read directly.
    AudioManager& Audio();

    bool HasDevice();
    bool Muted();

    /// Re-reads the device and republishes everything the config and the device say. Ends in
    /// OnStateChanged, silently - nothing has flipped, so nothing chimes.
    void Refresh();

    /// Subscribe side only - the module raises it, on the UI thread, once the state it carries
    /// is settled. What the listener draws it reads back through the accessors above.
    extern IEvent<>& OnStateChanged;
}
