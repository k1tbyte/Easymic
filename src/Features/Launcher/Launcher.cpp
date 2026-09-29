#include "Launcher.hpp"

#include "CommandLine.hpp"
#include "CommandRunner.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Feedback.hpp"
#include "Str.hpp"
#include "Tokens.hpp"

namespace {

    constexpr char StdoutToken[] = "{stdout}";

    ActionFn MakeRun(const ActionContext& context) {
        const std::string command = context.Args;
        if (command.empty()) {
            return {};
        }

        // Only the action body holds it: the worker is joined before Feedback dies
        Feedback* const feedback = &context.Fb;
        const std::string text = context.Notification;

        if (!text.contains(StdoutToken)) {
            return [feedback, command, text] {
                CommandRunner::Run(command, GetForegroundWindow());
                feedback->Notify(text);
            };
        }

        return [feedback, command, text] {
            // Expanded now: the state tokens must read what the key was pressed on, not the state at the answer
            const std::string resolved = feedback->Expand(text);

            CommandRunner::RunCaptured(command, GetForegroundWindow(),
                [post = feedback->Poster(), resolved](const std::string& output) {
                    post(Str::Replace(resolved, StdoutToken, output));
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
        Tokens::Add({StdoutToken, "Command output - waits up to 10 s", Tokens::Notification, true});
        Tokens::Add({CommandLine::DirToken, "Folder the active Explorer tab shows", Tokens::Command, true});
    }
}
