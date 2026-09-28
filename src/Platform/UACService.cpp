#include "UACService.hpp"

#include <comdef.h>
#include <shellapi.h>
#include <string>
#include <taskschd.h>
#include <windows.h>

#include "definitions.h"

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsupp.lib")

namespace UAC {

namespace {

    constexpr wchar_t APP_SKIPUAC_NAME[] = L"EasyLauncher_SkipUAC";
    constexpr wchar_t APP_AUTHOR[] = L"EasyLauncher";
    constexpr wchar_t APP_DESCRIPTION[] = L"EasyLauncher UAC bypass task";
    constexpr ULONG TASK_START_ATTEMPTS = 6;
    constexpr DWORD TASK_START_WAIT_MS = 250;

    /// COM has to be up for the task scheduler, but the thread that asks may already have it in
    /// another mode - that is not an error, it just means we are not the one to shut it down.
    class ComScope {
        HRESULT _hr;
        bool _ownsInit;
    public:
        ComScope() : _hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)),
                     _ownsInit(SUCCEEDED(_hr) && _hr != RPC_E_CHANGED_MODE) {}

        ~ComScope() {
            if (_ownsInit) {
                CoUninitialize();
            }
        }

        ComScope(const ComScope&) = delete;
        ComScope& operator=(const ComScope&) = delete;

        bool IsValid() const { return SUCCEEDED(_hr) || _hr == RPC_E_CHANGED_MODE; }
    };

    /// The root task folder, connected. Everything below needs both.
    class TaskSchedulerSession {
        ComScope _com;
        ComPtr<ITaskService> _service;
        ComPtr<ITaskFolder> _folder;

    public:
        bool Initialize() {
            if (!_com.IsValid()) {
                return false;
            }

            if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&_service)))) {
                return false;
            }

            const _variant_t empty;
            if (FAILED(_service->Connect(empty, empty, empty, empty))) {
                return false;
            }

            return SUCCEEDED(_service->GetFolder(_bstr_t(L"\\"), &_folder));
        }

        ITaskService* Service() const { return _service.Get(); }
        ITaskFolder* Folder() const { return _folder.Get(); }
    };

    template<typename T>
    bool QueryProcessToken(const TOKEN_INFORMATION_CLASS infoClass, T& value) {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            return false;
        }

        DWORD size = sizeof(value);
        const bool queried = GetTokenInformation(token, infoClass, &value, sizeof(value), &size);
        CloseHandle(token);
        return queried;
    }

    std::wstring GetCurrentExecutablePath() {
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);

        return length == 0 || length == MAX_PATH ? std::wstring{} : std::wstring(path, length);
    }

    /// A task pointing somewhere else is not ours, whatever its name says.
    bool TaskRunsThisExecutable(IRegisteredTask* registeredTask) {
        if (!registeredTask) {
            return false;
        }

        ComPtr<ITaskDefinition> definition;
        ComPtr<IActionCollection> actions;
        ComPtr<IAction> action;
        ComPtr<IExecAction> execAction;

        if (FAILED(registeredTask->get_Definition(&definition))
            || FAILED(definition->get_Actions(&actions))
            || FAILED(actions->get_Item(1, &action))
            || FAILED(action.As(&execAction))) {
            return false;
        }

        _bstr_t path;
        if (FAILED(execAction->get_Path(path.GetAddress()))) {
            return false;
        }

        const std::wstring currentPath = GetCurrentExecutablePath();
        return !currentPath.empty() && _wcsicmp(path, currentPath.c_str()) == 0;
    }

    bool ConfigureTaskDefinition(ITaskDefinition* taskDefinition) {
        ComPtr<IRegistrationInfo> registration;
        if (SUCCEEDED(taskDefinition->get_RegistrationInfo(&registration))) {
            registration->put_Author(_bstr_t(APP_AUTHOR));
            registration->put_Description(_bstr_t(APP_DESCRIPTION));
        }

        // The whole point of the task: it starts elevated without a prompt
        ComPtr<IPrincipal> principal;
        if (FAILED(taskDefinition->get_Principal(&principal))
            || FAILED(principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST))
            || FAILED(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN))) {
            return false;
        }

        ComPtr<ITaskSettings> settings;
        if (SUCCEEDED(taskDefinition->get_Settings(&settings))) {
            settings->put_StartWhenAvailable(VARIANT_TRUE);
            settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
            settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
            settings->put_AllowDemandStart(VARIANT_TRUE);
            settings->put_Enabled(VARIANT_TRUE);
            settings->put_Hidden(VARIANT_FALSE);
            settings->put_MultipleInstances(TASK_INSTANCES_PARALLEL);
        }

        return true;
    }

    bool CreateTaskAction(ITaskDefinition* taskDefinition) {
        const std::wstring exePath = GetCurrentExecutablePath();
        if (exePath.empty()) {
            return false;
        }

        ComPtr<IActionCollection> actions;
        ComPtr<IAction> action;
        ComPtr<IExecAction> execAction;

        if (FAILED(taskDefinition->get_Actions(&actions))
            || FAILED(actions->Create(TASK_ACTION_EXEC, &action))
            || FAILED(action.As(&execAction))) {
            return false;
        }

        return SUCCEEDED(execAction->put_Path(_bstr_t(exePath.c_str())));
    }

    std::wstring GetCommandLineArguments() {
        std::wstring arguments;
        int argumentCount = 0;
        LPWSTR* argumentList = CommandLineToArgvW(GetCommandLineW(), &argumentCount);

        if (argumentList) {
            for (int i = 1; i < argumentCount; i++) {
                if (i > 1) arguments += L" ";
                arguments += argumentList[i];
            }
            LocalFree(argumentList);
        }

        return arguments;
    }

    bool WaitForTaskStart(IRunningTask* runningTask) {
        ULONG attempts = TASK_START_ATTEMPTS;

        while (attempts--) {
            runningTask->Refresh();

            TASK_STATE state;
            if (SUCCEEDED(runningTask->get_State(&state))) {
                if (state == TASK_STATE_DISABLED) {
                    return false;
                }
                if (state == TASK_STATE_RUNNING) {
                    return true;
                }
            }

            Sleep(TASK_START_WAIT_MS);
        }

        return false;
    }

    /// True when the user has an administrator token to elevate into at all.
    bool CanElevate() {
        TOKEN_ELEVATION_TYPE elevationType;
        return QueryProcessToken(TokenElevationType, elevationType)
               && (elevationType == TokenElevationTypeLimited || elevationType == TokenElevationTypeFull);
    }

    /// Looks the task up and checks it still belongs to us.
    bool OpenOwnTask(const TaskSchedulerSession& session, ComPtr<IRegisteredTask>& task) {
        return SUCCEEDED(session.Folder()->GetTask(_bstr_t(APP_SKIPUAC_NAME), &task))
               && task && TaskRunsThisExecutable(task.Get());
    }

} // anonymous namespace

bool IsElevated() {
    TOKEN_ELEVATION elevation;
    return QueryProcessToken(TokenElevation, elevation) && elevation.TokenIsElevated != FALSE;
}

bool RequestElevation() {
    if (IsElevated()) {
        return true;
    }

    const std::wstring exePath = GetCurrentExecutablePath();
    if (!CanElevate() || exePath.empty()) {
        return false;
    }

    const HINSTANCE result = ShellExecuteW(nullptr, L"runas", exePath.c_str(), nullptr, nullptr,
                                           SW_SHOWNORMAL);

    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        return false;
    }

    ExitProcess(0);
}

bool IsSkipUACEnabled() {
    TaskSchedulerSession session;
    ComPtr<IRegisteredTask> task;

    return session.Initialize() && OpenOwnTask(session, task);
}

bool EnableSkipUAC() {
    TaskSchedulerSession session;
    if (!IsElevated() || !session.Initialize()) {
        return false;
    }

    ComPtr<ITaskDefinition> definition;
    if (FAILED(session.Service()->NewTask(0, &definition))
        || !ConfigureTaskDefinition(definition.Get())
        || !CreateTaskAction(definition.Get())) {
        return false;
    }

    ComPtr<IRegisteredTask> registeredTask;
    const _variant_t empty;
    return SUCCEEDED(session.Folder()->RegisterTaskDefinition(
        _bstr_t(APP_SKIPUAC_NAME), definition.Get(), TASK_CREATE_OR_UPDATE,
        empty, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, &registeredTask));
}

bool DisableSkipUAC() {
    TaskSchedulerSession session;
    if (!IsElevated() || !session.Initialize()) {
        return false;
    }

    return SUCCEEDED(session.Folder()->DeleteTask(_bstr_t(APP_SKIPUAC_NAME), 0));
}

bool RunWithSkipUAC() {
    TaskSchedulerSession session;
    ComPtr<IRegisteredTask> task;

    if (!session.Initialize() || !OpenOwnTask(session, task)) {
        return false;
    }

    VARIANT_BOOL isEnabled = VARIANT_FALSE;
    task->get_Enabled(&isEnabled);
    if (isEnabled == VARIANT_FALSE) {
        return false;
    }

    // Whatever we were started with has to reach the elevated instance too
    const std::wstring arguments = GetCommandLineArguments();
    _variant_t params;
    if (!arguments.empty()) {
        params = arguments.c_str();
    }

    ComPtr<IRunningTask> runningTask;
    if (FAILED(task->RunEx(params, TASK_RUN_IGNORE_CONSTRAINTS, 0, nullptr, &runningTask))
        || !runningTask) {
        return false;
    }

    return WaitForTaskStart(runningTask.Get());
}

} // namespace UAC
