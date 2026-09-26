# Live hotkey probe against the MinSizeRel build: .\tests\HotkeyProbe.ps1
# Stop any running EasyLauncher first (single instance). Injects F13-F20 and LCtrl into a text box
# of its own; its own LL hook sits below ours, so it sees what was swallowed. F20 converts the last
# word between the first two installed layouts, which have to be en-US and ru-RU.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$dir = Join-Path $PSScriptRoot '..\cmake-build-minsizerel'
$exe = Join-Path $dir 'EasyLauncher.exe'
$cfg = Join-Path $dir 'config.json'
$backup = Join-Path $dir 'config.json.probe-backup'
$appLog = Join-Path $dir 'easylauncher.log'
$log = Join-Path $env:TEMP 'el-probe.log'
Remove-Item $log -ErrorAction SilentlyContinue

Add-Type -TypeDefinition @'
using System; using System.Collections.Generic; using System.Diagnostics; using System.Runtime.InteropServices; using System.Text;
public static class Probe {
  [StructLayout(LayoutKind.Sequential)] struct KBDLLHOOKSTRUCT { public uint vk, scan, flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit, Size = 40)] struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] struct MSG { public IntPtr hwnd; public uint message; public IntPtr w, l; public uint time; public int x, y; }
  delegate IntPtr HookProc(int code, IntPtr w, IntPtr l);
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern IntPtr SetWindowsHookEx(int id, HookProc proc, IntPtr mod, uint thread);
  [DllImport("user32.dll")] static extern bool UnhookWindowsHookEx(IntPtr h);
  [DllImport("user32.dll")] static extern IntPtr CallNextHookEx(IntPtr h, int code, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [DllImport("user32.dll")] static extern bool PeekMessage(out MSG msg, IntPtr hwnd, uint min, uint max, uint remove);
  [DllImport("user32.dll")] static extern bool TranslateMessage(ref MSG msg);
  [DllImport("user32.dll")] static extern IntPtr DispatchMessage(ref MSG msg);
  [DllImport("kernel32.dll")] static extern IntPtr GetModuleHandle(string name);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] static extern IntPtr GetKeyboardLayout(uint thread);
  [DllImport("user32.dll")] static extern IntPtr LoadKeyboardLayout(string klid, uint flags);
  [DllImport("user32.dll")] static extern IntPtr ActivateKeyboardLayout(IntPtr hkl, uint flags);

  static HookProc _proc = Hook; static IntPtr _hook;
  public static List<string> Seen = new List<string>();
  static IntPtr Hook(int code, IntPtr w, IntPtr l) {
    if (code == 0) {
      var k = (KBDLLHOOKSTRUCT)Marshal.PtrToStructure(l, typeof(KBDLLHOOKSTRUCT));
      int m = w.ToInt32();
      Seen.Add(((m == 0x100 || m == 0x104) ? "+" : "-") + k.vk.ToString("X2"));
    }
    return CallNextHookEx(IntPtr.Zero, code, w, l);
  }
  public static void Install() { _hook = SetWindowsHookEx(13, _proc, GetModuleHandle(null), 0); if (_hook == IntPtr.Zero) throw new Exception("hook failed"); }
  public static void Remove() { UnhookWindowsHookEx(_hook); }
  public static void Pump(int ms) {
    var sw = Stopwatch.StartNew(); MSG msg;
    while (sw.ElapsedMilliseconds < ms) {
      while (PeekMessage(out msg, IntPtr.Zero, 0, 0, 1)) { TranslateMessage(ref msg); DispatchMessage(ref msg); }
      System.Threading.Thread.Sleep(1);
    }
  }
  public static void Key(ushort vk, bool down) {
    var i = new INPUT[1]; i[0].type = 1; i[0].ki.wVk = vk; i[0].ki.dwFlags = down ? 0u : 2u;
    if (SendInput(1, i, Marshal.SizeOf(typeof(INPUT))) != 1) throw new Exception("SendInput failed");
    Pump(15);
  }
  public static void Tap(ushort vk) { Key(vk, true); Key(vk, false); }
  public static void Type(string keys) { foreach (char c in keys) Tap((ushort)c); }
  // One SendInput for all of them, so the ones after the first land while its hold is up
  public static void Burst(params ushort[] vks) {
    var i = new INPUT[vks.Length * 2];
    for (int k = 0; k < vks.Length; k++) {
      i[2 * k].type = 1; i[2 * k].ki.wVk = vks[k];
      i[2 * k + 1].type = 1; i[2 * k + 1].ki.wVk = vks[k]; i[2 * k + 1].ki.dwFlags = 2;
    }
    if (SendInput((uint)i.Length, i, Marshal.SizeOf(typeof(INPUT))) != i.Length) throw new Exception("SendInput failed");
    Pump(15);
  }
  public static string Layout() { return (GetKeyboardLayout(0).ToInt64() & 0xFFFF).ToString("X4"); }
  public static IntPtr CurrentLayout() { return GetKeyboardLayout(0); }
  public static void UseLayout(string klid) { ActivateKeyboardLayout(LoadKeyboardLayout(klid, 0), 0); }
  public static void RestoreLayout(IntPtr hkl) { ActivateKeyboardLayout(hkl, 0); }
  public static string ForegroundExe() { uint pid; GetWindowThreadProcessId(GetForegroundWindow(), out pid); return Process.GetProcessById((int)pid).ProcessName.ToLowerInvariant() + ".exe"; }
  public static int Post(int pid, string cls, uint message, int w) {
    int posted = 0;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p); var s = new StringBuilder(64); GetClassName(h, s, 64);
      if (p == pid && s.ToString() == cls) { PostMessage(h, message, (IntPtr)w, IntPtr.Zero); posted++; }
      return true;
    }, IntPtr.Zero);
    return posted;
  }
  public static int Exit(int pid) { return Post(pid, "MainWindowClass", 0x111, 40009); }
}
'@

if (Test-Path $cfg) { Copy-Item $cfg $backup -Force }
$appLogLines = if (Test-Path $appLog) { (Get-Content $appLog).Count } else { 0 }

# A window of our own in the foreground, so the per-app case does not depend on what the user
# happens to have focused, and the conversion types into nothing of theirs
$form = New-Object System.Windows.Forms.Form -Property @{ Text = "EasyLauncher probe $PID"; Width = 400; Height = 200; TopMost = $true }
$box = New-Object System.Windows.Forms.TextBox -Property @{ Multiline = $true; Dock = 'Fill' }
$form.Controls.Add($box)
$form.Show()
$shell = New-Object -ComObject WScript.Shell
$exe = (Get-Process -Id $PID).ProcessName.ToLowerInvariant() + '.exe'
$fg = $null
foreach ($try in 1..30) {
    $shell.AppActivate($form.Text) | Out-Null
    [Probe]::Pump(100)
    $fg = [Probe]::ForegroundExe()
    if ($fg -eq $exe) { break }
}
if ($fg -ne $exe) { $form.Close(); "PROBE FAILED: the probe window never took the foreground (foreground=$fg)"; exit 1 }
$box.Focus() | Out-Null
$userLayout = [Probe]::CurrentLayout()

function Run($tag) { "cmd /c echo $tag>>$log" }
$bindings = @(
    @{ Name = 'app'; ActionId = 'launcher.run'; Args = (Run 'app'); Trigger = @{ Keys = 'F18'; App = $fg } },
    @{ Name = 'other'; ActionId = 'launcher.run'; Args = (Run 'other'); Trigger = @{ Keys = 'F19'; App = 'nonexistent.exe' } },
    @{ Name = 'f13'; ActionId = 'launcher.run'; Args = (Run 'f13'); Trigger = @{ Keys = 'F13'; Block = $true } },
    @{ Name = 'f14x1'; ActionId = 'launcher.run'; Args = (Run 'f14x1'); Trigger = @{ Keys = 'F14'; Presses = 1 } },
    @{ Name = 'f14x2'; ActionId = 'launcher.run'; Args = (Run 'f14x2'); Trigger = @{ Keys = 'F14'; Presses = 2 } },
    @{ Name = 'f15tap'; ActionId = 'launcher.run'; Args = (Run 'f15tap'); Trigger = @{ Keys = 'F15'; OnRelease = $true; TapOnly = $true } },
    @{ Name = 'ctrlf17'; ActionId = 'launcher.run'; Args = (Run 'ctrlf17'); Trigger = @{ Keys = 'CTRL + F17' } },
    @{ Name = 'convert'; ActionId = 'kbd.convert_word'; Trigger = @{ Keys = 'F20' } }
)
$json = @{ Core = @{ Updates = $false }; Bindings = $bindings; Version = 4 } | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText($cfg, $json)

$p = $null
try {
    [Probe]::Install()
    $p = Start-Process $exe -PassThru
    [Probe]::Pump(2500)

    [Probe]::Tap(0x81); [Probe]::Pump(300)                       # F18, scoped to the foreground app
    [Probe]::Tap(0x82); [Probe]::Pump(300)                       # F19, scoped to another app
    [Probe]::Tap(0x7C); [Probe]::Pump(400)                       # F13, blocked
    [Probe]::Tap(0x7D); [Probe]::Pump(500)                       # F14 once
    [Probe]::Tap(0x7D); [Probe]::Pump(40); [Probe]::Tap(0x7D); [Probe]::Pump(500)   # F14 twice
    [Probe]::Tap(0x7E); [Probe]::Pump(300)                       # F15 tapped
    [Probe]::Key(0x7E, $true); [Probe]::Tap(0x7F); [Probe]::Key(0x7E, $false); [Probe]::Pump(300)  # F15 held over F16
    [Probe]::Key(0xA2, $true); [Probe]::Tap(0x80); [Probe]::Key(0xA2, $false); [Probe]::Pump(300)  # LCtrl + F17
    [Probe]::Key(0x7C, $true); [Probe]::Key(0x7C, $true); [Probe]::Key(0x7C, $false)                 # F13 with autorepeat
    [Probe]::Pump(3000)
    $hotkeysSeen = [Probe]::Seen -join ' '

    # Conversion: what the box shows and the layout its thread is in after each step
    $conversion = @()
    $step = { param($name) [Probe]::Pump(400); $script:conversion += "$name=$($box.Text -replace "`r`n", '|')@$([Probe]::Layout())" }
    [Probe]::UseLayout('00000409'); [Probe]::Pump(100)
    [Probe]::Type('GHBDTN'); [Probe]::Tap(0x83); & $step 'convert'
    [Probe]::Tap(0x83); & $step 'back'
    [Probe]::Tap(0x83); [Probe]::Tap(0x20); [Probe]::Tap(0x83); & $step 'space'
    [Probe]::Tap(0x0D); [Probe]::Type('GHBDTN'); [Probe]::Burst(0x83, 0x56, 0x42, 0x48); & $step 'during'
    [Probe]::RestoreLayout($userLayout)
    [Probe]::Seen.Clear()

    [Probe]::Seen.Add('|settings')
    "settings opened: " + [Probe]::Post($p.Id, 'MainWindowClass', 0x111, 40010)
    [Probe]::Pump(1500)
    [Probe]::Tap(0x7C); [Probe]::Pump(500)                       # F13 while the settings are open
    "settings closed: " + [Probe]::Post($p.Id, '#32770', 0x10, 0)
    [Probe]::Pump(1500)
    [Probe]::Seen.Add('|closed')
    [Probe]::Tap(0x7C); [Probe]::Pump(2500)                      # F13 after restore

    $posted = [Probe]::Exit($p.Id)
    $exited = $p.WaitForExit(8000)
    "exit posted=$posted exited=$exited code=$(if ($exited) { $p.ExitCode } else { 'n/a' })"
} finally {
    [Probe]::Remove()
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force; 'killed' }
    [Probe]::RestoreLayout($userLayout)
    $form.Close()
    if (Test-Path $backup) { Move-Item $backup $cfg -Force } else { Remove-Item $cfg -ErrorAction SilentlyContinue }
}

"foreground=$fg"
$fired = (Get-Content $log -ErrorAction SilentlyContinue | ForEach-Object { $_.Trim() } | Sort-Object) -join ','
$seen = "$hotkeysSeen " + ([Probe]::Seen -join ' ')
$converted = $conversion -join ' '
"fired: $fired"
"probe saw: $seen"
"conversion: $converted"
if (Test-Path $appLog) { "app log:"; Get-Content $appLog | Select-Object -Skip $appLogLines }

$expectFired = 'app,ctrlf17,f13,f13,f13,f14x1,f14x2,f15tap'
$expectSeen = '+81 -81 +82 -82 +7D -7D +7D -7D +7D -7D +7E -7E +7E +7F -7F -7E +A2 +80 -80 -A2 |settings +7C -7C |closed'
# Built from code points: PowerShell 5.1 reads a script without a BOM as ANSI
$privet = -join [char[]](0x43F, 0x440, 0x438, 0x432, 0x435, 0x442)
$mir = -join [char[]](0x43C, 0x438, 0x440)
$expectConverted = "convert=$privet@0419 back=ghbdtn@0409 space=ghbdtn @0409 during=ghbdtn |$privet$mir@0419"
if ($fired -ne $expectFired -or $seen -ne $expectSeen -or $converted -ne $expectConverted) { 'PROBE FAILED'; exit 1 }
'probe passed'