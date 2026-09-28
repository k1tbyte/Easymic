# Settings UI check on MinSizeRel. Stop any running EasyLauncher before this probe.
$ErrorActionPreference = 'Stop'
$dir = Join-Path $PSScriptRoot '..\..\cmake-build-minsizerel'
$exe = Join-Path $dir 'EasyLauncher.exe'
$cfg = Join-Path $dir 'config.json'
$backup = Join-Path $dir 'config.json.keyboard-settings-backup'
if (Test-Path $backup) { throw "Backup already exists: $backup" }
if (Get-CimInstance Win32_Process -Filter "Name='EasyLauncher.exe'") { throw 'Stop EasyLauncher before the probe' }

Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System; using System.Diagnostics; using System.Runtime.InteropServices; using System.Text;
public static class SettingsProbe {
  delegate bool EnumProc(IntPtr hwnd, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr data);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr hwnd, StringBuilder text, int size);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr hwnd, StringBuilder text, int size);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string name);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr parent, int id);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr hwnd);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr w, StringBuilder l);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
  public static IntPtr Message(IntPtr hwnd, uint msg, int w, IntPtr l) { return SendMessageW(hwnd, msg, (IntPtr)w, l); }
  public static string Text(IntPtr hwnd) { var s=new StringBuilder(256); GetWindowTextW(hwnd,s,s.Capacity); return s.ToString(); }
  public static IntPtr Window(int pid, string cls) {
    IntPtr found=IntPtr.Zero;
    EnumWindows((hwnd,unused) => { uint owner; GetWindowThreadProcessId(hwnd,out owner);
      var name=new StringBuilder(80); GetClassNameW(hwnd,name,name.Capacity);
      if (owner==pid && name.ToString()==cls) { found=hwnd; return false; } return true;
    }, IntPtr.Zero);
    return found;
  }
  public static IntPtr Child(IntPtr parent, string cls) { return FindWindowExW(parent,IntPtr.Zero,cls,null); }
  public static void Type(IntPtr edit, string text) {
    SendMessageW(edit,0xB1,IntPtr.Zero,(IntPtr)(-1)); SendMessageW(edit,0x303,IntPtr.Zero,IntPtr.Zero);
    foreach (char c in text) SendMessageW(edit,0x102,(IntPtr)(int)c,IntPtr.Zero);
  }
  public static string ComboItem(IntPtr combo, int index) { var s=new StringBuilder(256); SendMessageW(combo,0x148,(IntPtr)index,s); return s.ToString(); }
  public static void Select(IntPtr page, int id, int index) {
    var combo=GetDlgItem(page,id);
    Message(combo,0x14E,index,IntPtr.Zero);
    Message(page,0x111,(1<<16)|id,combo);
  }
}
'@

function Wait-Window([int]$ownerId, [string]$class) {
    foreach ($try in 1..50) {
        $found = [SettingsProbe]::Window($ownerId, $class)
        if ($found -ne [IntPtr]::Zero) { return $found }
        Start-Sleep -Milliseconds 100
    }
    throw "No $class window for $ownerId"
}
function Assert([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}
function Open-KeyboardPage([IntPtr]$settings) {
    $tree = [SettingsProbe]::GetDlgItem($settings, 151)
    $group = [SettingsProbe]::GetDlgItem($settings, 152)
    $item = [SettingsProbe]::Message($tree, 0x110A, 0, [IntPtr]::Zero)
    while ($item -ne [IntPtr]::Zero) {
        [SettingsProbe]::Message($tree, 0x110B, 9, $item) | Out-Null
        if ([SettingsProbe]::Text($group) -eq 'Keyboard') {
            $page = [SettingsProbe]::Child($group, '#32770')
            Assert ($page -ne [IntPtr]::Zero) 'Keyboard page missing'
            return $page
        }
        $item = [SettingsProbe]::Message($tree, 0x110A, 1, $item)
    }
    throw 'Keyboard page not in settings tree'
}

$hadConfig = Test-Path $cfg
if ($hadConfig) { Copy-Item $cfg $backup }
$p = $null
$form = $null
try {
    $seed = @{ Core = @{ Updates = $false }; Keyboard = @{ Learned = @{ Always = @('ye'); Never = @() } } }
    [IO.File]::WriteAllText($cfg, ($seed | ConvertTo-Json -Depth 6))
    $p = Start-Process $exe -PassThru
    $main = Wait-Window $p.Id 'MainWindowClass'
    Assert ([SettingsProbe]::PostMessageW($main, 0x111, [IntPtr]40010, [IntPtr]::Zero)) 'Cannot open settings'
    $settings = Wait-Window $p.Id '#32770'
    Start-Sleep -Milliseconds 1500
    $page = Open-KeyboardPage $settings

    $packA = [SettingsProbe]::GetDlgItem($page, 1032)
    $packB = [SettingsProbe]::GetDlgItem($page, 1064)
    Assert ([SettingsProbe]::ComboItem($packA, 1) -eq 'en.pack') 'English pack not offered'
    Assert ([SettingsProbe]::ComboItem($packB, 1) -eq 'ru.pack') 'Russian pack not offered'
    [SettingsProbe]::Select($page, 1032, 1)
    [SettingsProbe]::Select($page, 1016, 1)
    Assert ([SettingsProbe]::ComboItem($packA, 1) -eq 'ru.pack') 'Pack list did not refresh after changing layout'
    Assert ([SettingsProbe]::ComboItem($packA, 2) -eq 'Missing or incompatible: en.pack') 'Selected incompatible pack was hidden'
    Assert (-not [SettingsProbe]::IsWindowEnabled([SettingsProbe]::GetDlgItem($page, 1080))) 'Autocorrect accepted an incompatible pack'
    [SettingsProbe]::Select($page, 1016, 0)
    Assert ([SettingsProbe]::Message($packA, 0x147, 0, [IntPtr]::Zero).ToInt32() -eq 1) 'Pack selection lost after changing layout back'
    [SettingsProbe]::Select($page, 1064, 1)

    $frequency = [SettingsProbe]::GetDlgItem($page, 1128)
    Assert ([SettingsProbe]::Message($frequency, 0xF0, 0, [IntPtr]::Zero).ToInt32() -eq 1) 'Frequency analysis is not on by default'
    Assert ([SettingsProbe]::IsWindowEnabled([SettingsProbe]::GetDlgItem($page, 1144))) 'Threshold disabled with frequency analysis on'
    [SettingsProbe]::Message($frequency, 0xF5, 0, [IntPtr]::Zero) | Out-Null
    Assert (-not [SettingsProbe]::IsWindowEnabled([SettingsProbe]::GetDlgItem($page, 1144))) 'Threshold enabled without frequency analysis'

    [SettingsProbe]::Message($page, 0x115, 7, [IntPtr]::Zero) | Out-Null
    $panel = [SettingsProbe]::Child($page, 'EasyLauncher.KeyboardLists')
    Assert ($panel -ne [IntPtr]::Zero) 'Excluded apps panel missing'
    $appCombo = [SettingsProbe]::GetDlgItem($panel, 201)
    $form = New-Object System.Windows.Forms.Form -Property @{ Text = 'Running app picker probe' }
    $form.Show()
    [SettingsProbe]::Message($panel, 0x111, (7 -shl 16) -bor 201, $appCombo) | Out-Null
    $running = @()
    $count = [SettingsProbe]::Message($appCombo, 0x146, 0, [IntPtr]::Zero).ToInt32()
    for ($i = 0; $i -lt $count; $i++) { $running += [SettingsProbe]::ComboItem($appCombo, $i) }
    Assert ($running -contains 'powershell.exe') 'Running app not offered in dropdown'
    $edit = [SettingsProbe]::Child($appCombo, 'Edit')
    Assert ($edit -ne [IntPtr]::Zero) 'Editable app combo is missing its edit field'
    $list = [SettingsProbe]::GetDlgItem($panel, 200)
    $add = [SettingsProbe]::GetDlgItem($panel, 202)
    [SettingsProbe]::Type($edit,'NOTEPAD.EXE') | Out-Null
    [SettingsProbe]::Message($panel, 0x111, 202, $add) | Out-Null
    $count = [SettingsProbe]::Message($list, 0x1004, 0, [IntPtr]::Zero).ToInt32()
    Assert ($count -eq 1) 'Manual app not added'
    [SettingsProbe]::Type($edit,'notepad.exe') | Out-Null
    [SettingsProbe]::Message($panel, 0x111, 202, $add) | Out-Null
    Assert ([SettingsProbe]::Message($list, 0x1004, 0, [IntPtr]::Zero).ToInt32() -eq 1) 'Duplicate app added'
    [SettingsProbe]::Type($edit,'chrome.exe') | Out-Null
    [SettingsProbe]::Message($panel, 0x111, 202, $add) | Out-Null
    Assert ([SettingsProbe]::Message($list, 0x1004, 0, [IntPtr]::Zero).ToInt32() -eq 2) 'Second app not added'

    $words = [SettingsProbe]::GetDlgItem($panel, 204)
    $word = [SettingsProbe]::GetDlgItem($panel, 205)
    $never = [SettingsProbe]::GetDlgItem($panel, 207)
    Assert ([SettingsProbe]::Message($words, 0x1004, 0, [IntPtr]::Zero).ToInt32() -eq 1) 'Learned word not listed'
    [SettingsProbe]::Type($word, 'OFC,') | Out-Null
    [SettingsProbe]::Message($panel, 0x111, 207, $never) | Out-Null
    Assert ([SettingsProbe]::Message($words, 0x1004, 0, [IntPtr]::Zero).ToInt32() -eq 2) 'Never word not added'
    [SettingsProbe]::Type($word, 'ofc') | Out-Null
    [SettingsProbe]::Message($panel, 0x111, 207, $never) | Out-Null
    Assert ([SettingsProbe]::Message($words, 0x1004, 0, [IntPtr]::Zero).ToInt32() -eq 2) 'Duplicate word added'

    [SettingsProbe]::PostMessageW($settings, 0x111, [IntPtr]1, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 500
    $saved = Get-Content $cfg -Raw | ConvertFrom-Json
    Assert ($saved.Keyboard.PackA -eq 'en.pack' -and $saved.Keyboard.PackB -eq 'ru.pack') 'Pack selections were not saved'
    Assert ($saved.Keyboard.Exclude -eq 'chrome.exe, notepad.exe') 'App exclusions were not saved'
    Assert ($saved.Keyboard.FrequencyAnalysis -eq $false) 'Frequency analysis was not saved'
    Assert ("$($saved.Keyboard.Learned.Always)|$($saved.Keyboard.Learned.Never)" -eq 'ye|ofc') 'Learned words were not saved'

    Assert ([SettingsProbe]::PostMessageW($main, 0x111, [IntPtr]40010, [IntPtr]::Zero)) 'Cannot reopen settings'
    $settings = Wait-Window $p.Id '#32770'
    $page = Open-KeyboardPage $settings
    $panel = [SettingsProbe]::Child($page, 'EasyLauncher.KeyboardLists')
    $list = [SettingsProbe]::GetDlgItem($panel, 200)
    Assert ([SettingsProbe]::Message($list, 0x1004, 0, [IntPtr]::Zero).ToInt32() -eq 2) 'Saved exclusions not listed'
    Assert ([SettingsProbe]::Message([SettingsProbe]::GetDlgItem($page, 1032), 0x147, 0, [IntPtr]::Zero).ToInt32() -eq 1) 'Saved pack not selected'
    [SettingsProbe]::Message($page, 0x115, 7, [IntPtr]::Zero) | Out-Null
    [SettingsProbe]::Message($list, 0x201, 0, [IntPtr]((8 -shl 16) -bor 12)) | Out-Null
    [SettingsProbe]::Message($list, 0x202, 0, [IntPtr]((8 -shl 16) -bor 12)) | Out-Null
    Assert ([SettingsProbe]::Message($list, 0x100C, -1, [IntPtr]2).ToInt32() -eq 0) 'Cannot select an excluded app'
    $remove = [SettingsProbe]::GetDlgItem($panel, 203)
    [SettingsProbe]::Message($panel, 0x111, 203, $remove) | Out-Null
    Assert ([SettingsProbe]::Message($list, 0x1004, 0, [IntPtr]::Zero).ToInt32() -eq 1) 'Excluded app was not removed'
    $words = [SettingsProbe]::GetDlgItem($panel, 204)
    [SettingsProbe]::Message($words, 0x201, 0, [IntPtr]((8 -shl 16) -bor 12)) | Out-Null
    [SettingsProbe]::Message($words, 0x202, 0, [IntPtr]((8 -shl 16) -bor 12)) | Out-Null
    [SettingsProbe]::Message($panel, 0x111, 208, [SettingsProbe]::GetDlgItem($panel, 208)) | Out-Null
    Assert ([SettingsProbe]::Message($words, 0x1004, 0, [IntPtr]::Zero).ToInt32() -eq 1) 'Learned word was not removed'
    [SettingsProbe]::Select($page, 1032, 0)
    [SettingsProbe]::PostMessageW($settings, 0x111, [IntPtr]2, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 500
    $saved = Get-Content $cfg -Raw | ConvertFrom-Json
    Assert ($saved.Keyboard.PackA -eq 'en.pack') 'Cancel persisted the pack edit'
    Assert ("$($saved.Keyboard.Learned.Always)|$($saved.Keyboard.Learned.Never)" -eq 'ye|ofc') 'Cancel persisted the learned word removal'
    'keyboard settings probe passed'
} finally {
    if ($form) { $form.Close() }
    if ($p -and -not $p.HasExited) {
        $main = [SettingsProbe]::Window($p.Id, 'MainWindowClass')
        if ($main -ne [IntPtr]::Zero) { [SettingsProbe]::PostMessageW($main, 0x111, [IntPtr]40009, [IntPtr]::Zero) | Out-Null }
        if (-not $p.WaitForExit(8000)) { Stop-Process -Id $p.Id -Force }
    }
    if ($hadConfig) { Move-Item $backup $cfg -Force } else { Remove-Item $cfg -ErrorAction SilentlyContinue }
}
