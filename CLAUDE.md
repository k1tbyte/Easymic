# Easymic

Native WinAPI C++23 tray app: global mic control via low-level keyboard/mouse hooks.
Priorities in order: input latency, binary size, simplicity.

## Build

```powershell
.\build.ps1                      # MinSizeRel -> cmake-build-minsizerel/Easymic.exe
.\build.ps1 -Config Debug -Run
.\build.ps1 -Clean
```

The script imports the MSVC environment itself (`cl.exe` is never in the global PATH).
Toolchain: global cmake + ninja, MSVC from Visual Studio, resources via `rc.exe` from the Windows SDK.

`UPX not found` during a release build is only a warning - the binary is just left uncompressed
(`winget install UPX.UPX` to enable the POST_BUILD compression step).

## Layout

- `src/Lib/HotkeyManager.*` - LL hooks, key masks, action dispatch. Hook procs must stay O(1):
  actions run on the worker thread, never inside the proc.
- `src/Audio/` - WASAPI device/session control and event handlers.
- `src/View/`, `src/ViewModel/` - Win32 windows and their view models. Settings pages are
  DIALOGEX resources in `src/Resources/Resource.rc`, loaded by `CreateDialogParamW`.
- `vendor/glaze` - submodule, BEVE config serialization (`AppConfig`).
