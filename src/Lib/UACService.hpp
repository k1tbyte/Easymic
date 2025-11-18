#ifndef EASYMIC_UACSERVICE_HPP
#define EASYMIC_UACSERVICE_HPP

#include <windows.h>
#include <taskschd.h>
#include <shellapi.h>
#include <comdef.h>
#include <string>
#include <memory>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsupp.lib")

#define APP_SKIPUAC_NAME L"Easymic_SkipUAC"

namespace UAC {

    /**
     * Comprehensive User Account Control (UAC) service
     * Handles elevation checking, process elevation, and UAC bypass functionality
     * Uses RAII patterns and follows SOLID principles
     */
    class Service {
    private:
        // RAII wrapper for COM initialization
        class COMInitializer {
        public:
            COMInitializer() : m_needsUninit(false) {
                m_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                // S_FALSE means COM was already initialized
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

            // Non-copyable, non-movable
            COMInitializer(const COMInitializer&) = delete;
            COMInitializer& operator=(const COMInitializer&) = delete;
            COMInitializer(COMInitializer&&) = delete;
            COMInitializer& operator=(COMInitializer&&) = delete;

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

            // Non-copyable, non-movable
            TaskSchedulerSession(const TaskSchedulerSession&) = delete;
            TaskSchedulerSession& operator=(const TaskSchedulerSession&) = delete;
            TaskSchedulerSession(TaskSchedulerSession&&) = delete;
            TaskSchedulerSession& operator=(TaskSchedulerSession&&) = delete;

        private:
            COMInitializer m_com;
            ITaskService* m_taskService;
            ITaskFolder* m_taskFolder;
        };

        // Helper methods
        static std::wstring GetCurrentExecutablePath() {
            wchar_t path[MAX_PATH];
            DWORD result = GetModuleFileNameW(nullptr, path, MAX_PATH);

            if (result == 0 || result == MAX_PATH) {
                return L"";
            }

            return std::wstring(path);
        }

        static bool ValidateElevationRequired(const std::string& operation) {
            if (IsElevated()) {
                return true;
            }
            return false;
        }

        static HRESULT ValidateTaskExecutable(IRegisteredTask* registeredTask) {
            if (!registeredTask) return E_INVALIDARG;

            ITaskDefinition* taskDefinition = nullptr;
            HRESULT hr = registeredTask->get_Definition(&taskDefinition);
            if (FAILED(hr)) return hr;

            // Use RAII for cleanup
            struct TaskDefinitionCleanup {
                ITaskDefinition* td;
                ~TaskDefinitionCleanup() { if (td) td->Release(); }
            } cleanup{taskDefinition};

            IActionCollection* actionCollection = nullptr;
            hr = taskDefinition->get_Actions(&actionCollection);
            if (FAILED(hr)) return hr;

            struct ActionCollectionCleanup {
                IActionCollection* ac;
                ~ActionCollectionCleanup() { if (ac) ac->Release(); }
            } actionCleanup{actionCollection};

            IAction* action = nullptr;
            hr = actionCollection->get_Item(1, &action);
            if (FAILED(hr)) return hr;

            struct ActionCleanup {
                IAction* a;
                ~ActionCleanup() { if (a) a->Release(); }
            } actionItemCleanup{action};

            IExecAction* execAction = nullptr;
            hr = action->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&execAction));
            if (FAILED(hr)) return hr;

            struct ExecActionCleanup {
                IExecAction* ea;
                ~ExecActionCleanup() { if (ea) ea->Release(); }
            } execCleanup{execAction};

            BSTR path = nullptr;
            hr = execAction->get_Path(&path);
            if (FAILED(hr)) return hr;

            struct BSTRCleanup {
                BSTR b;
                ~BSTRCleanup() { if (b) SysFreeString(b); }
            } pathCleanup{path};

            std::wstring currentPath = GetCurrentExecutablePath();
            if (currentPath.empty()) return E_FAIL;

            // Compare paths (case-insensitive)
            if (_wcsicmp(path, currentPath.c_str()) != 0) {
                return E_FAIL; // Task points to different executable
            }

            return S_OK;
        }

    public:
        Service() = delete; // Static service class

        // Core UAC status checking
        static bool IsElevated() {
            HANDLE hToken = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
                return false;
            }

            struct TokenCleanup {
                HANDLE token;
                ~TokenCleanup() { if (token) CloseHandle(token); }
            } cleanup{hToken};

            TOKEN_ELEVATION elevation;
            DWORD dwSize = sizeof(elevation);

            if (!GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
                return false;
            }

            return elevation.TokenIsElevated != FALSE;
        }

        static bool CanElevate() {
            HANDLE hToken = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
                return false;
            }

            struct TokenCleanup {
                HANDLE token;
                ~TokenCleanup() { if (token) CloseHandle(token); }
            } cleanup{hToken};

            TOKEN_ELEVATION_TYPE elevationType;
            DWORD dwSize = sizeof(elevationType);

            if (!GetTokenInformation(hToken, TokenElevationType, &elevationType, sizeof(elevationType), &dwSize)) {
                return false;
            }

            // Can elevate if user is admin or has UAC enabled
            return (elevationType == TokenElevationTypeLimited ||
                   elevationType == TokenElevationTypeFull);
        }

        // Process elevation
        static bool RequestElevation() {
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

            // Use ShellExecuteW with "runas" verb to request elevation
            HINSTANCE result = ShellExecuteW(
                nullptr,
                L"runas",
                exePath.c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL
            );

            // If elevation was attempted successfully, close current instance
            if (reinterpret_cast<INT_PTR>(result) > 32) {
                ExitProcess(0);
                return true;
            }

            return false;
        }

        // UAC bypass functionality (requires elevation to set up)
        static bool IsSkipUACAvailable() {
            return IsElevated();
        }

        static bool IsSkipUACEnabled() {
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

            struct TaskCleanup {
                IRegisteredTask* task;
                ~TaskCleanup() { if (task) task->Release(); }
            } cleanup{registeredTask};

            // Verify the task points to current executable
            return SUCCEEDED(ValidateTaskExecutable(registeredTask));
        }

        static bool EnableSkipUAC() {
            if (!ValidateElevationRequired("EnableSkipUAC")) {
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

            struct TaskDefinitionCleanup {
                ITaskDefinition* td;
                ~TaskDefinitionCleanup() { if (td) td->Release(); }
            } cleanup{taskDefinition};

            bool success = false;
            do {
                // Set registration info
                IRegistrationInfo* regInfo = nullptr;
                hr = taskDefinition->get_RegistrationInfo(&regInfo);
                if (SUCCEEDED(hr)) {
                    regInfo->put_Author(_bstr_t(L"Easymic"));
                    regInfo->put_Description(_bstr_t(L"Easymic UAC bypass task"));
                    regInfo->Release();
                }

                // Set principal to run with highest privileges
                IPrincipal* principal = nullptr;
                hr = taskDefinition->get_Principal(&principal);
                if (FAILED(hr)) break;

                // Critical: Set to run with highest privileges
                hr = principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
                if (SUCCEEDED(hr)) {
                    hr = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
                }
                principal->Release();
                if (FAILED(hr)) break;

                // Set task settings
                ITaskSettings* settings = nullptr;
                hr = taskDefinition->get_Settings(&settings);
                if (SUCCEEDED(hr)) {
                    settings->put_StartWhenAvailable(VARIANT_TRUE);
                    settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
                    settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
                    settings->put_AllowDemandStart(VARIANT_TRUE);
                    settings->put_Enabled(VARIANT_TRUE);
                    settings->put_Hidden(VARIANT_FALSE);
                    settings->put_MultipleInstances(TASK_INSTANCES_PARALLEL);
                    settings->Release();
                }

                // Create action
                IActionCollection* actionCollection = nullptr;
                hr = taskDefinition->get_Actions(&actionCollection);
                if (FAILED(hr)) break;

                IAction* action = nullptr;
                hr = actionCollection->Create(TASK_ACTION_EXEC, &action);
                actionCollection->Release();
                if (FAILED(hr)) break;

                IExecAction* execAction = nullptr;
                hr = action->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&execAction));
                action->Release();
                if (FAILED(hr)) break;

                // Set executable path
                std::wstring exePath = GetCurrentExecutablePath();
                if (exePath.empty()) {
                    execAction->Release();
                    break;
                }

                execAction->put_Path(_bstr_t(exePath.c_str()));
                execAction->Release();

                // Register the task
                IRegisteredTask* registeredTask = nullptr;
                _variant_t empty;
                hr = session.GetFolder()->RegisterTaskDefinition(
                    _bstr_t(APP_SKIPUAC_NAME),
                    taskDefinition,
                    TASK_CREATE_OR_UPDATE,
                    empty,  // No user specified - use current user
                    empty,  // No password
                    TASK_LOGON_INTERACTIVE_TOKEN,
                    empty,
                    &registeredTask
                );

                if (registeredTask) {
                    registeredTask->Release();
                }

                success = SUCCEEDED(hr);

            } while (false);

            return success;
        }

        static bool DisableSkipUAC() {
            if (!ValidateElevationRequired("DisableSkipUAC")) {
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

        static bool RunWithSkipUAC() {
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

            struct TaskCleanup {
                IRegisteredTask* task;
                ~TaskCleanup() { if (task) task->Release(); }
            } cleanup{registeredTask};

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

            // Run the task with proper parameters
            _variant_t params;
            if (!arguments.empty()) {
                params = arguments.c_str();
            }

            IRunningTask* runningTask = nullptr;
            hr = registeredTask->RunEx(
                params,
                TASK_RUN_IGNORE_CONSTRAINTS,
                0,
                nullptr,
                &runningTask
            );

            if (FAILED(hr) || !runningTask) {
                return false;
            }

            struct RunningTaskCleanup {
                IRunningTask* task;
                ~RunningTaskCleanup() { if (task) task->Release(); }
            } runningCleanup{runningTask};

            // Wait for task to start (max 1.5 seconds)
            bool success = false;
            ULONG attempts = 6;
            do {
                runningTask->Refresh();
                TASK_STATE state;
                if (SUCCEEDED(runningTask->get_State(&state))) {
                    if (state == TASK_STATE_DISABLED) {
                        break;
                    }
                    if (state == TASK_STATE_RUNNING) {
                        success = true;
                        break;
                    }
                }
                Sleep(250);
            } while (--attempts);

            return success;
        }
    };

} // namespace UAC

#endif // EASYMIC_UACSERVICE_HPP
