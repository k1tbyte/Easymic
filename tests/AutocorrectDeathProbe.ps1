# Autocorrect repro (docs/LAYOUT.md, diagnosis): types the reported session into a window of its
# own - a Russian line, Alt+Shift through every installed layout, the same words again - and prints
# a per-word timeline of the box text, the layout and the app's decision log.
# Same rules as HotkeyProbe.ps1: it stops any running EasyLauncher, runs its own with a backed-up
# config, and the machine must stay idle while it types.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$dir = Join-Path $PSScriptRoot '..\cmake-build-minsizerel'
$exe = Join-Path $dir 'EasyLauncher.exe'
$cfg = Join-Path $dir 'config.json'
$backup = Join-Path $dir 'config.json.probe-backup'
$appLog = Join-Path $dir 'easylauncher.log'

Add-Type -TypeDefinition @'
using System; using System.Collections.Generic; using System.Diagnostics; using System.Runtime.InteropServices; using System.Text;
public static class Probe {
  [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit, Size = 40)] struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] struct MSG { public IntPtr hwnd; public uint message; public IntPtr w, l; public uint time; public int x, y; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [DllImport("user32.dll")] static extern bool PeekMessage(out MSG msg, IntPtr hwnd, uint min, uint max, uint remove);
  [DllImport("user32.dll")] static extern bool TranslateMessage(ref MSG msg);
  [DllImport("user32.dll")] static extern IntPtr DispatchMessage(ref MSG msg);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] static extern IntPtr GetKeyboardLayout(uint thread);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr LoadKeyboardLayout(string klid, uint flags);
  [DllImport("user32.dll")] static extern IntPtr ActivateKeyboardLayout(IntPtr hkl, uint flags);
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
  public static string Layout() { return (GetKeyboardLayout(0).ToInt64() & 0xFFFF).ToString("X4"); }
  public static IntPtr CurrentLayout() { return GetKeyboardLayout(0); }
  public static void UseLayout(string klid) { ActivateKeyboardLayout(LoadKeyboardLayout(klid, 0), 0); }
  public static void RestoreLayout(IntPtr hkl) { ActivateKeyboardLayout(hkl, 0); }
  public static string ForegroundExe() { uint pid; GetWindowThreadProcessId(GetForegroundWindow(), out pid); return Process.GetProcessById((int)pid).ProcessName.ToLowerInvariant() + ".exe"; }
  public static IntPtr ForegroundHwnd() { return GetForegroundWindow(); }
  public static int Post(int pid, string cls, uint message, int w) {
    return PostMessage(FindWindow(pid, cls), message, (IntPtr)w, IntPtr.Zero) ? 1 : 0;
  }
  static IntPtr FindWindow(int pid, string cls) {
    IntPtr found = IntPtr.Zero; EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p); var s = new StringBuilder(64); GetClassName(h, s, 64);
      if (found == IntPtr.Zero && p == (uint)pid && s.ToString() == cls) found = h;
      return true;
    }, IntPtr.Zero);
    return found;
  }
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
}
'@

Get-Process EasyLauncher -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
if (Test-Path $cfg) { Copy-Item $cfg $backup -Force }
$logCount = if (Test-Path $appLog) { (Get-Content $appLog).Count } else { 0 }

$form = New-Object System.Windows.Forms.Form -Property @{ Text = "AC death probe $PID"; Width = 500; Height = 250; TopMost = $true }
$box = New-Object System.Windows.Forms.TextBox -Property @{ Multiline = $true; Dock = 'Fill' }
$form.Controls.Add($box)
$form.Show()
$shell = New-Object -ComObject WScript.Shell
$probeExe = (Get-Process -Id $PID).ProcessName.ToLowerInvariant() + '.exe'
$fg = $null
foreach ($try in 1..30) {
    $shell.AppActivate($form.Text) | Out-Null
    [Probe]::Pump(100)
    $fg = [Probe]::ForegroundExe()
    if ($fg -eq $probeExe) { break }
}
if ($fg -ne $probeExe) { $form.Close(); "PROBE FAILED: never took the foreground (fg=$fg)"; exit 1 }
foreach ($try in 1..20) {
    $form.Activate()
    [Probe]::Pump(100)
    if ($box.Focused -or $box.Focus()) { break }
}
if (-not $box.Focused) { $form.Close(); 'PROBE FAILED: the text box never took focus'; exit 1 }
$userLayout = [Probe]::CurrentLayout()

$json = @{ Core = @{ Updates = $false }
           Keyboard = @{ AutoCorrect = 'Space'; LogDecisions = $true }
           Bindings = @(@{ Name = 'convert'; ActionId = 'kbd.convert_word'; Trigger = @{ Keys = 'F20' } }) } | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText($cfg, $json)

$timeline = New-Object System.Collections.Generic.List[string]
$p = $null
try {
    $p = Start-Process $exe -PassThru
    [Probe]::Pump(2500)

    function Snap([string]$name) {
        [Probe]::Pump(250)
        $all = @(Get-Content $appLog -Encoding UTF8 -ErrorAction SilentlyContinue)
        for ($i = $script:logCount; $i -lt $all.Count; $i++) {
            $timeline.Add('    log: ' + ($all[$i] -replace '^\[[^\]]+\] \[[^\]]+\] ', ''))
        }
        $script:logCount = [Math]::Max($script:logCount, $all.Count)
        $timeline.Add("$name | text='$($box.Text -replace "`r`n", '|')' | layout=$([Probe]::Layout())")
    }
    function Word([string]$name, [uint16[]]$vks) {
        foreach ($v in $vks) { [Probe]::Tap($v) }
        [Probe]::Tap(0x20)
        [Probe]::Pump(700)
        Snap $name
    }
    function AltShift() {
        [Probe]::Key(0xA4, $true); [Probe]::Key(0xA0, $true); [Probe]::Pump(60)
        [Probe]::Key(0xA0, $false); [Probe]::Key(0xA4, $false); [Probe]::Pump(250)
    }

    # --- line 1: RU fingers on the RU layout: "nu schas mozhno glyanut kak budto"
    [Probe]::UseLayout('00000419'); [Probe]::Pump(150); Snap 'start-ru'
    Word 'nu'      @([uint16[]](0x59, 0x45))
    Word 'schas'   @([uint16[]](0x4F, 0x46, 0x43))
    Word 'mozhno'  @([uint16[]](0x56, 0x4A, 0xBA, 0x59, 0x4A))
    Word 'glyanut' @([uint16[]](0x55, 0x4B, 0x5A, 0x59, 0x45, 0x4E, 0x4D))
    Word 'kak'     @([uint16[]](0x52, 0x46, 0x52))
    Word 'budto'   @([uint16[]](0xBC, 0x45, 0x4C, 0x4E, 0x4A))
    [Probe]::Tap(0x0D); [Probe]::Pump(300); Snap 'enter'

    # --- the user's manual switch, then line 2: same sentence, EN layout
    AltShift
    Snap "altshift->en ($([Probe]::Layout()))"
    Word 'ye'      @([uint16[]](0x59, 0x45))
    Word 'yfdthyjt' @([uint16[]](0x59, 0x46, 0x44, 0x54, 0x48, 0x59, 0x4A, 0x54))
    Word 'vj;yj'   @([uint16[]](0x56, 0x4A, 0xBA, 0x59, 0x4A))
    Word 'ukzyenm' @([uint16[]](0x55, 0x4B, 0x5A, 0x59, 0x45, 0x4E, 0x4D))

    # --- back to RU, the hand-typed line 3, then the solo words
    AltShift
    Snap "altshift->ru ($([Probe]::Layout()))"
    Word 'nu2'      @([uint16[]](0x59, 0x45))
    Word 'navernoe' @([uint16[]](0x59, 0x46, 0x44, 0x54, 0x48, 0x59, 0x4A, 0x54))
    Word 'mozhno2'  @([uint16[]](0x56, 0x4A, 0xBA, 0x59, 0x4A))
    Word 'glyanut2' @([uint16[]](0x55, 0x4B, 0x5A, 0x59, 0x45, 0x4E, 0x4D))
    [Probe]::Tap(0x0D); [Probe]::Pump(200)
    AltShift
    Snap "altshift->en2 ($([Probe]::Layout()))"
    Word 'yfd-solo1' @([uint16[]](0x59, 0x46, 0x44, 0x54, 0x48, 0x59, 0x4A, 0x54))
    Word 'yfd-solo2' @([uint16[]](0x59, 0x46, 0x44, 0x54, 0x48, 0x59, 0x4A, 0x54))

    # --- revive attempt: settings open/close is a Suspend/Restore cycle for the stage
    [Probe]::Post($p.Id, 'MainWindowClass', 0x111, 40010) | Out-Null
    [Probe]::Pump(1500)
    [Probe]::Post($p.Id, '#32770', 0x10, 0) | Out-Null
    [Probe]::Pump(1500)
    Snap 'after-settings-cycle'
    Word 'yfd-after-settings' @([uint16[]](0x59, 0x46, 0x44, 0x54, 0x48, 0x59, 0x4A, 0x54))
} finally {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    [Probe]::RestoreLayout($userLayout)
    $form.Close()
    if (Test-Path $backup) { Move-Item $backup $cfg -Force } else { Remove-Item $cfg -ErrorAction SilentlyContinue }
}

''
$timeline
