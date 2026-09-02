#include "Launcher.hpp"

#include "CommandRunner.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Feedback.hpp"
#include "Str.hpp"
#include "Tokens.hpp"

namespace {

    /**
     * @brief Turns a configured command line into the call that launches it.
     *
     * {stdout} in the notification is what asks for the output, so it also picks how the command
     * is run. The foreground window is read on the worker, when the key is pressed, because {dir}
     * means the window the user was looking at - not the one we are about to open.
     *
     * This action announces itself rather than letting the kernel do it: with {stdout} there is
     * nothing to announce until the command has finished, which can be minutes later.
     */
    ActionFn MakeRun(const ActionContext& context) {
        const std::string command = context.Args;
        if (command.empty()) {
            return {};
        }

        // Safe to hold onto across a launch that answers that late: Feedback is created once in
        // main and outlives every action and every command thread
        Feedback* const feedback = &context.Fb;
        const std::string text = context.Notification;

        if (!text.contains(Tokens::Stdout)) {
            return [feedback, command, text] {
                CommandRunner::Run(command, GetForegroundWindow());
                feedback->Notify(text);
            };
        }

        return [feedback, command, text] {
            // Resolved before launching: the state tokens have to read what the user pressed the
            // key on, and the thread that answers later cannot be trusted with them
            const std::string resolved = feedback->Expand(text);

            CommandRunner::RunCaptured(command, GetForegroundWindow(),
                [feedback, resolved](const std::string& output) {
                    feedback->Post(Str::Replace(resolved, Tokens::Stdout, output));
                });
        };
    }

    constexpr ActionDesc Actions[] = {
        {.Id = "launcher.run",
         .Title = "Command",
         .Group = "Launcher",
         .Flags = ActionFlags::RunsCommand,
         .DefaultNotification = ActionRegistry::DefaultNotification,
         .Make = MakeRun},
    };

} // anonymous namespace

namespace Launcher {

    void Register(Host&) {
        for (const auto& desc : Actions) {
            ActionRegistry::Add(desc);
        }
    }
}
