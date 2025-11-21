//
// Created by kitbyte on 20.11.2025.
//
#include "UACService.hpp"

#include <windows.h>
#include <taskschd.h>
#include <shellapi.h>
#include <comdef.h>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsupp.lib")

namespace UAC {

namespace {

    constexpr wchar_t APP_SKIPUAC_NAME[] = L"Easymic_SkipUAC";
    constexpr wchar_t APP_AUTHOR[] = L"Easymic";
    constexpr wchar_t APP_DESCRIPTION[] = L"Easymic UAC bypass task";
    constexpr ULONG TASK_START_ATTEMPTS = 6;
    constexpr DWORD TASK_START_WAIT_MS = 250;

    // RAII wrapper for COM initialization
    class COMInitializer {
    public:
        COMInitializer() : m_needsUninit(false) {
            m_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            m_needsUninit = SUCCEEDED(m_hr) && m_hr != RPC_E_CHANGED_MODE;
        }

        ~COMInitializer() {
            if (m_needsUninit) {
                CoUninitialize();
            }
        }

        bool IsValid() const {
            return SUCCEEDED(m_hr) || m_hr == RPC_E_CHANGED_MODE;
        }

        COMInitializer(const COMInitializer&) = delete;
        COMInitializer& operator=(const COMInitializer&) = delete;

    private:
        HRESULT m_hr;
        bool m_needsUninit;
    };

    // RAII wrapper for task scheduler interfaces
    class TaskSchedulerSession {
    public:
        TaskSchedulerSession() : m_taskService(nullptr), m_taskFolder(nullptr) {}

        ~TaskSchedulerSession() {
            if (m_taskFolder) m_taskFolder->Release();
            if (m_taskService) m_taskService->Release();
        }

        bool Initialize() {
            if (!m_com.IsValid()) {
                return false;
            }

            HRESULT hr = CoCreateInstance(
                CLSID_TaskScheduler,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_ITaskService,
                reinterpret_cast<void**>(&m_taskService)
            );

            if (FAILED(hr)) return false;

            _variant_t empty;
            hr = m_taskService->Connect(empty, empty, empty, empty);
            if (FAILED(hr)) return false;

            hr = m_taskService->GetFolder(_bstr_t(L"\\"), &m_taskFolder);
            return SUCCEEDED(hr);
        }

        ITaskService* GetService() const { return m_taskService; }
        ITaskFolder* GetFolder() const { return m_taskFolder; }

        TaskSchedulerSession(const TaskSchedulerSession&) = delete;
        TaskSchedulerSession& operator=(const TaskSchedulerSession&) = delete;

    private:
        COMInitializer m_com;
        ITaskService* m_taskService;
        ITaskFolder* m_taskFolder;
    };

    // RAII helper for handles
    class HandleGuard {
    public:
        explicit HandleGuard(HANDLE handle) : m_handle(handle) {}
        ~HandleGuard() { if (m_handle) CloseHandle(m_handle); }

        HandleGuard(const HandleGuard&) = delete;
        HandleGuard& operator=(const HandleGuard&) = delete;

    private:
        HANDLE m_handle;
    };

    // RAII helper for COM interfaces
    template<typename T>
    class COMGuard {
    public:
        explicit COMGuard(T* ptr) : m_ptr(ptr) {}
        ~COMGuard() { if (m_ptr) m_ptr->Release(); }

        COMGuard(const COMGuard&) = delete;
        COMGuard& operator=(const COMGuard&) = delete;

    private:
        T* m_ptr;
    };

    // RAII helper for BSTR
    class BSTRGuard {
    public:
        explicit BSTRGuard(BSTR bstr) : m_bstr(bstr) {}
        ~BSTRGuard() { if (m_bstr) SysFreeString(m_bstr); }

        BSTRGuard(const BSTRGuard&) = delete;
        BSTRGuard& operator=(const BSTRGuard&) = delete;

    private:
        BSTR m_bstr;
    };

    // Helper functions
    std::wstring GetCurrentExecutablePath() {
        wchar_t path[MAX_PATH];
        DWORD result = GetModuleFileNameW(nullptr, path, MAX_PATH);

        if (result == 0 || result == MAX_PATH) {
            return L"";
        }

        return std::wstring(path);
    }

    HRESULT ValidateTaskExecutable(IRegisteredTask* registeredTask) {
        if (!registeredTask) return E_INVALIDARG;

        ITaskDefinition* taskDefinition = nullptr;
        HRESULT hr = registeredTask->get_Definition(&taskDefinition);
        if (FAILED(hr)) return hr;

        COMGuard<ITaskDefinition> tdGuard(taskDefinition);

        IActionCollection* actionCollection = nullptr;
        hr = taskDefinition->get_Actions(&actionCollection);
        if (FAILED(hr)) return hr;

        COMGuard<IActionCollection> acGuard(actionCollection);

        IAction* action = nullptr;
        hr = actionCollection->get_Item(1, &action);
        if (FAILED(hr)) return hr;

        COMGuard<IAction> actionGuard(action);

        IExecAction* execAction = nullptr;
        hr = action->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&execAction));
        if (FAILED(hr)) return hr;

        COMGuard<IExecAction> execGuard(execAction);

        BSTR path = nullptr;
        hr = execAction->get_Path(&path);
        if (FAILED(hr)) return hr;

        BSTRGuard pathGuard(path);

        std::wstring currentPath = GetCurrentExecutablePath();
        if (currentPath.empty()) return E_FAIL;

        // Compare paths (case-insensitive)
        if (_wcsicmp(path, currentPath.c_str()) != 0) {
            return E_FAIL; // Task points to different executable
        }

        return S_OK;
    }

    bool ConfigureTaskDefinition(ITaskDefinition* taskDefinition) {
        HRESULT hr;

        // Set registration info
        IRegistrationInfo* regInfo = nullptr;
        hr = taskDefinition->get_RegistrationInfo(&regInfo);
        if (SUCCEEDED(hr)) {
            COMGuard<IRegistrationInfo> regGuard(regInfo);
            regInfo->put_Author(_bstr_t(APP_AUTHOR));
            regInfo->put_Description(_bstr_t(APP_DESCRIPTION));
        }

        // Set principal to run with highest privileges
        IPrincipal* principal = nullptr;
        hr = taskDefinition->get_Principal(&principal);
        if (FAILED(hr)) return false;

        COMGuard<IPrincipal> principalGuard(principal);

        hr = principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
        if (FAILED(hr)) return false;

        hr = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        if (FAILED(hr)) return false;

        // Set task settings
        ITaskSettings* settings = nullptr;
        hr = taskDefinition->get_Settings(&settings);
        if (SUCCEEDED(hr)) {
            COMGuard<ITaskSettings> settingsGuard(settings);
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
        IActionCollection* actionCollection = nullptr;
        HRESULT hr = taskDefinition->get_Actions(&actionCollection);
        if (FAILED(hr)) return false;

        COMGuard<IActionCollection> acGuard(actionCollection);

        IAction* action = nullptr;
        hr = actionCollection->Create(TASK_ACTION_EXEC, &action);
        if (FAILED(hr)) return false;

        COMGuard<IAction> actionGuard(action);

        IExecAction* execAction = nullptr;
        hr = action->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&execAction));
        if (FAILED(hr)) return false;

        COMGuard<IExecAction> execGuard(execAction);

        std::wstring exePath = GetCurrentExecutablePath();
        if (exePath.empty()) return false;

        execAction->put_Path(_bstr_t(exePath.c_str()));
        return true;
    }

    std::wstring GetCommandLineArguments() {
        std::wstring arguments;
        int numArgs = 0;
        LPWSTR* argList = CommandLineToArgvW(GetCommandLineW(), &numArgs);

        if (argList) {
            for (int i = 1; i < numArgs; i++) {
                if (i > 1) arguments += L" ";
                arguments += argList[i];
            }
            LocalFree(argList);
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

} // anonymous namespace

// Public API implementation

bool IsElevated() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }

    HandleGuard tokenGuard(hToken);

    TOKEN_ELEVATION elevation;
    DWORD dwSize = sizeof(elevation);

    if (!GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
        return false;
    }

    return elevation.TokenIsElevated != FALSE;
}

bool CanElevate() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }

    HandleGuard tokenGuard(hToken);

    TOKEN_ELEVATION_TYPE elevationType;
    DWORD dwSize = sizeof(elevationType);

    if (!GetTokenInformation(hToken, TokenElevationType, &elevationType, sizeof(elevationType), &dwSize)) {
        return false;
    }

    return (elevationType == TokenElevationTypeLimited ||
            elevationType == TokenElevationTypeFull);
}

bool RequestElevation() {
    if (IsElevated()) {
        return true; // Already elevated
    }

    if (!CanElevate()) {
        return false; // User cannot elevate
    }

    std::wstring exePath = GetCurrentExecutablePath();
    if (exePath.empty()) {
        return false;
    }

    HINSTANCE result = ShellExecuteW(
        nullptr,
        L"runas",
        exePath.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );

    if (reinterpret_cast<INT_PTR>(result) > 32) {
        ExitProcess(0);
        return true;
    }

    return false;
}

bool IsSkipUACAvailable() {
    return IsElevated();
}

bool IsSkipUACEnabled() {
    TaskSchedulerSession session;
    if (!session.Initialize()) {
        return false;
    }

    IRegisteredTask* registeredTask = nullptr;
    _bstr_t taskName(APP_SKIPUAC_NAME);
    HRESULT hr = session.GetFolder()->GetTask(taskName, &registeredTask);

    if (FAILED(hr) || !registeredTask) {
        return false;
    }

    COMGuard<IRegisteredTask> taskGuard(registeredTask);

    return SUCCEEDED(ValidateTaskExecutable(registeredTask));
}

bool EnableSkipUAC() {
    if (!IsElevated()) {
        return false;
    }

    TaskSchedulerSession session;
    if (!session.Initialize()) {
        return false;
    }

    ITaskDefinition* taskDefinition = nullptr;
    HRESULT hr = session.GetService()->NewTask(0, &taskDefinition);
    if (FAILED(hr)) {
        return false;
    }

    COMGuard<ITaskDefinition> tdGuard(taskDefinition);

    if (!ConfigureTaskDefinition(taskDefinition)) {
        return false;
    }

    if (!CreateTaskAction(taskDefinition)) {
        return false;
    }

    // Register the task
    IRegisteredTask* registeredTask = nullptr;
    _variant_t empty;
    hr = session.GetFolder()->RegisterTaskDefinition(
        _bstr_t(APP_SKIPUAC_NAME),
        taskDefinition,
        TASK_CREATE_OR_UPDATE,
        empty,
        empty,
        TASK_LOGON_INTERACTIVE_TOKEN,
        empty,
        &registeredTask
    );

    if (registeredTask) {
        registeredTask->Release();
    }

    return SUCCEEDED(hr);
}

bool DisableSkipUAC() {
    if (!IsElevated()) {
        return false;
    }

    TaskSchedulerSession session;
    if (!session.Initialize()) {
        return false;
    }

    _bstr_t taskName(APP_SKIPUAC_NAME);
    HRESULT hr = session.GetFolder()->DeleteTask(taskName, 0);

    return SUCCEEDED(hr);
}

bool RunWithSkipUAC() {
    TaskSchedulerSession session;
    if (!session.Initialize()) {
        return false;
    }

    IRegisteredTask* registeredTask = nullptr;
    _bstr_t taskName(APP_SKIPUAC_NAME);
    HRESULT hr = session.GetFolder()->GetTask(taskName, &registeredTask);
    if (FAILED(hr)) {
        return false;
    }

    COMGuard<IRegisteredTask> taskGuard(registeredTask);

    // Verify task points to current executable
    hr = ValidateTaskExecutable(registeredTask);
    if (FAILED(hr)) {
        return false;
    }

    // Check if task is enabled
    VARIANT_BOOL isEnabled = VARIANT_FALSE;
    registeredTask->get_Enabled(&isEnabled);
    if (isEnabled == VARIANT_FALSE) {
        return false;
    }

    // Build arguments
    std::wstring arguments = GetCommandLineArguments();

    // Run the task
    _variant_t params;
    if (!arguments.empty()) {
        params = arguments.c_str();
    }

    IRunningTask* runningTask = nullptr;
    hr = registeredTask->RunEx(
        params,
#ifdef _MSC_VER
        TASK_RUN_IGNORE_CONSTRAINTS,
#else
        0x2,
#endif
        0,
        nullptr,
        &runningTask
    );

    if (FAILED(hr) || !runningTask) {
        return false;
    }

    COMGuard<IRunningTask> runningGuard(runningTask);

    return WaitForTaskStart(runningTask);
}

} // namespace UAC