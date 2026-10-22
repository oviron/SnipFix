# Live test of a running SnipFix with the real Snipping Tool (PowerShell 7).
# Needs: HDR on, SDR content brightness above 80 nits, Microsoft Edge, Snipping Tool with its
# "HDR screenshot color corrector" OFF and auto-save to the Screenshots folder ON (defaults).
# Opens a test page with reference colours and a PNG carrying gAMA+cHRM (what puts a Chromium
# window into FP16 output) in a separate Edge instance, takes screenshots every way Windows
# offers, and checks the colours. Don't touch the mouse or keyboard while it runs.
param([string]$Exe = (Join-Path $PSScriptRoot '..\build\SnipFix.exe'), [switch]$NoSnipFix)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type -Language CSharp @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class T {
  [StructLayout(LayoutKind.Sequential)] public struct LUID { public uint Lo; public int Hi; }
  [StructLayout(LayoutKind.Sequential)] public struct PATH_SOURCE { public LUID adapterId; public uint id, modeInfoIdx, statusFlags; }
  [StructLayout(LayoutKind.Sequential)] public struct RATIONAL { public uint n, d; }
  [StructLayout(LayoutKind.Sequential)] public struct PATH_TARGET { public LUID adapterId; public uint id, modeInfoIdx, outputTechnology, rotation, scaling; public RATIONAL refreshRate; public uint scanLineOrdering; public int targetAvailable; public uint statusFlags; }
  [StructLayout(LayoutKind.Sequential)] public struct PATH { public PATH_SOURCE src; public PATH_TARGET tgt; public uint flags; }
  [StructLayout(LayoutKind.Sequential)] public struct MODE { public uint infoType, id; public LUID adapterId; [MarshalAs(UnmanagedType.ByValArray, SizeConst=48)] public byte[] data; }
  [StructLayout(LayoutKind.Sequential)] public struct GETW { public uint type, size; public LUID adapterId; public uint id, level; }
  [StructLayout(LayoutKind.Sequential, Pack=4)] public struct SETW { public uint type, size; public LUID adapterId; public uint id, level; public byte finalValue; }
  [DllImport("user32.dll")] static extern int GetDisplayConfigBufferSizes(uint f, out uint np, out uint nm);
  [DllImport("user32.dll")] static extern int QueryDisplayConfig(uint f, ref uint np, [Out] PATH[] p, ref uint nm, [Out] MODE[] m, IntPtr t);
  [DllImport("user32.dll")] static extern int DisplayConfigGetDeviceInfo(ref GETW r);
  [DllImport("user32.dll")] static extern int DisplayConfigSetDeviceInfo(ref SETW r);
  static PATH_TARGET Target() { uint np, nm; GetDisplayConfigBufferSizes(2, out np, out nm); var p = new PATH[np]; var m = new MODE[nm]; QueryDisplayConfig(2, ref np, p, ref nm, m, IntPtr.Zero); return p[0].tgt; }
  public static double Nits() { var t = Target(); var r = new GETW { type = 11, size = (uint)Marshal.SizeOf(typeof(GETW)), adapterId = t.adapterId, id = t.id }; DisplayConfigGetDeviceInfo(ref r); return r.level * 80.0 / 1000; }
  public static int SetNits(double nits) { var t = Target(); var r = new SETW { type = 0xFFFFFFEE, size = (uint)Marshal.SizeOf(typeof(SETW)), adapterId = t.adapterId, id = t.id, level = (uint)Math.Round(nits * 1000 / 80), finalValue = 1 }; return DisplayConfigSetDeviceInfo(ref r); }
  public static uint TargetId() { return Target().id; }
  [StructLayout(LayoutKind.Sequential)] public struct KI { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct IN { public uint type; public KI ki; public long pad; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, IN[] i, int cb);
  public static void Key(ushort vk, bool up) { var i = new IN[1]; i[0].type = 1; i[0].ki.vk = vk; i[0].ki.flags = (up ? 2u : 0u) | ((vk == 0x5B || vk == 0x2C) ? 1u : 0u); SendInput(1, i, Marshal.SizeOf(typeof(IN))); }
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr FindWindowW(string c, IntPtr n);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  public static bool OverlayVisible() { bool v = false; EnumWindows((h, l) => { var c = new StringBuilder(64); GetClassNameW(h, c, 64); if (c.ToString() == "SnipOverlayRootWindow" && IsWindowVisible(h)) { v = true; return false; } return true; }, IntPtr.Zero); return v; }
  public static IntPtr FindTitle(string t) { IntPtr r = IntPtr.Zero; EnumWindows((h, l) => { var s = new StringBuilder(256); GetWindowTextW(h, s, 256); if (IsWindowVisible(h) && s.ToString().StartsWith(t)) { r = h; return false; } return true; }, IntPtr.Zero); return r; }
}
'@
[T]::SetProcessDPIAware() | Out-Null
$fail = 0
function Check($ok, $what) { if ($ok) { "  ok   $what" } else { "  FAIL $what"; $script:fail++ } }
$shots = 'D:\Images\Screenshots'
if (-not (Test-Path $shots)) { $shots = Join-Path ([Environment]::GetFolderPath('MyPictures')) 'Screenshots' }
$orig = [T]::Nits()
"SDR white: $orig nits"
if ($orig -le 80) { 'SDR white is 80 nits or HDR is off: nothing for SnipFix to do, nothing to test.'; exit 2 }

# --- test page: reference blocks + a PNG with gAMA/cHRM (Chromium treats it as wide gamut)
$dir = Join-Path ([IO.Path]::GetTempPath()) ('snipfix-test-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory $dir | Out-Null
function Chunk([string]$type, [byte[]]$data) {
  $t = [Text.Encoding]::ASCII.GetBytes($type); $len = [BitConverter]::GetBytes([uint32]$data.Length); [Array]::Reverse($len)
  $crcIn = $t + $data; $crc = [uint32]::MaxValue
  foreach ($b in $crcIn) { $crc = $crc -bxor $b; for ($k = 0; $k -lt 8; $k++) { $crc = if ($crc -band 1) { ($crc -shr 1) -bxor 0xEDB88320 } else { $crc -shr 1 } } }
  $c = [BitConverter]::GetBytes([uint32]($crc -bxor [uint32]::MaxValue)); [Array]::Reverse($c)
  return $len + $t + $data + $c
}
function BE([uint32[]]$v) { $o = @(); foreach ($x in $v) { $b = [BitConverter]::GetBytes($x); [Array]::Reverse($b); $o += $b }; return [byte[]]$o }
$raw = New-Object IO.MemoryStream
foreach ($y in 1..40) { $raw.WriteByte(0); foreach ($x in 1..40) { $raw.WriteByte(0x40); $raw.WriteByte(0x80); $raw.WriteByte(0xC0) } }
$z = New-Object IO.MemoryStream; $zs = New-Object IO.Compression.ZLibStream($z, [IO.Compression.CompressionLevel]::Optimal); $zs.Write($raw.ToArray(), 0, $raw.Length); $zs.Close()
$png = [byte[]](0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A)
$png += Chunk 'IHDR' ((BE 40, 40) + [byte[]](8, 2, 0, 0, 0))
$png += Chunk 'gAMA' (BE 45455)
$png += Chunk 'cHRM' (BE 31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000)
$png += Chunk 'IDAT' $z.ToArray()
$png += Chunk 'IEND' ([byte[]]@())
[IO.File]::WriteAllBytes("$dir\wide.png", $png)
$blocks = '#00CE7A', '#FFBD3F', '#FF6874', '#808080', '#262626'
function Page($name, $img) {
  $divs = ($blocks | ForEach-Object { "<div style=`"background:$_`"></div>" }) -join ''
  "<!doctype html><html><head><meta charset=utf-8><title>$name</title><style>html,body{margin:0;background:#fff}.r{display:flex;padding:40px}.r div{width:160px;height:120px}</style></head><body><div class=r>$divs</div>$img</body></html>" | Set-Content "$dir\$name.html" -Encoding utf8
}
Page 'SnipFixWide' '<img src="wide.png" style="margin:40px">'
Page 'SnipFixPlain' ''

# A fresh Edge profile dims the page under a first-run dialog. The "First Run" sentinel
# marks it as done; failing that, one throwaway launch does.
New-Item -ItemType Directory "$dir\edge" | Out-Null
New-Item -ItemType File "$dir\edge\First Run" | Out-Null
$script:warm = $false
function OpenPage($name) {
  if (-not $script:warm) {
    Start-Process msedge -ArgumentList "--user-data-dir=`"$dir\edge`"", '--no-first-run', '--no-default-browser-check', "--app=file:///$($dir.Replace('\','/'))/$name.html"
    Start-Sleep 6; CloseEdge; $script:warm = $true
  }
  Start-Process msedge -ArgumentList "--user-data-dir=`"$dir\edge`"", '--no-first-run', '--no-default-browser-check', "--app=file:///$($dir.Replace('\','/'))/$name.html", '--window-size=1300,600'
  for ($k = 0; $k -lt 30; $k++) { Start-Sleep -Milliseconds 300; $h = [T]::FindTitle($name); if ($h -ne [IntPtr]::Zero) { break } }
  Start-Sleep 2
  [T]::Key(0xA4, $false); [T]::Key(0xA4, $true); [T]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 500
  return $h
}
function CloseEdge { Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" | Where-Object { $_.CommandLine -like "*$dir*" } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }; Start-Sleep 2 }

# locate the blocks on screen (GDI): returns @{ x0; y0; w } in physical pixels
function FindBlocks {
  $vs = [Windows.Forms.SystemInformation]::VirtualScreen
  $bmp = New-Object Drawing.Bitmap $vs.Width, $vs.Height; $g = [Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($vs.X, $vs.Y, 0, 0, $bmp.Size); $g.Dispose()
  try {
    for ($y = 0; $y -lt $bmp.Height; $y += 3) { for ($x = 0; $x -lt $bmp.Width; $x += 3) {
      $c = $bmp.GetPixel($x, $y)
      if (($c.R -eq 0x80 -or $c.R -eq 0xD2) -and $c.G -eq $c.R -and $c.B -eq $c.R) {  # grey block, exact or 3x
        $ref = $c.ToArgb(); $l = $x; while ($bmp.GetPixel($l - 1, $y).ToArgb() -eq $ref) { $l-- }; $r = $x; while ($bmp.GetPixel($r + 1, $y).ToArgb() -eq $ref) { $r++ }
        $t = $y; while ($bmp.GetPixel($x, $t - 1).ToArgb() -eq $ref) { $t-- }
        $w = $r - $l + 1; if ($w -lt 60) { continue }
        return @{ x0 = $l - 3 * $w + $vs.X; y0 = $t + $vs.Y; w = $w; over = ($c.R -eq 0xD2) } } } }
  } finally { $bmp.Dispose() }
  return $null
}
function MeasureBlocks($bmp, $ox, $oy, $w) {   # colours at the block centres; ox/oy = where block 0 starts inside $bmp
  $res = @(); for ($i = 0; $i -lt 5; $i++) { $c = $bmp.GetPixel([int]($ox + $i * $w + $w / 2), [int]($oy + 30)); $res += ('#{0:X2}{1:X2}{2:X2}' -f $c.R, $c.G, $c.B) }; return $res
}
function Close([string]$a, [string]$b) { $x = [Convert]::ToInt32($a.Substring(1), 16); $y = [Convert]::ToInt32($b.Substring(1), 16)
  for ($s = 0; $s -le 16; $s += 8) { if ([Math]::Abs((($x -shr $s) -band 255) - (($y -shr $s) -band 255)) -gt 1) { return $false } }; return $true }
function CheckColours($got, $what) {
  $bad = @(); for ($i = 0; $i -lt 5; $i++) { if (-not (Close $got[$i] $blocks[$i])) { $bad += "$($blocks[$i])->$($got[$i])" } }
  Check ($bad.Count -eq 0) ("$what" + $(if ($bad.Count) { ': ' + ($bad -join ' ') } else { '' }))
}
# watch the SDR level while a capture runs: how long was it lowered, and is it back?
$watch = $null
function StartWatch { $script:watch = [PowerShell]::Create(); [void]$script:watch.AddScript({ param($tid) $sw = [Diagnostics.Stopwatch]::StartNew(); $low = -1; $back = -1
    while ($sw.ElapsedMilliseconds -lt 4000) { $n = [T]::Nits(); if ($n -le 80.5 -and $low -lt 0) { $low = $sw.ElapsedMilliseconds }; if ($low -ge 0 -and $n -gt 80.5) { $back = $sw.ElapsedMilliseconds; break }; Start-Sleep -Milliseconds 5 }
    @($low, $back) }); $script:async = $script:watch.BeginInvoke() }
function StopWatch { $r = $script:watch.EndInvoke($script:async); $script:watch.Dispose(); return $r }

function NewShot($t0) { for ($k = 0; $k -lt 60; $k++) { $f = Get-ChildItem $shots -File -Filter *.png | Where-Object { $_.LastWriteTime -ge $t0 } | Select-Object -First 1; if ($f) { Start-Sleep -Milliseconds 400; return $f }; Start-Sleep -Milliseconds 150 }; return $null }
function Drag($bl) {
  $x0 = $bl.x0 + 6; $y0 = $bl.y0 + 6; $x1 = $bl.x0 + 5 * $bl.w - 6; $y1 = $bl.y0 + 70
  [T]::SetCursorPos($x0, $y0) | Out-Null; Start-Sleep -Milliseconds 150; [T]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 150
  foreach ($s in 1..12) { [T]::SetCursorPos([int]($x0 + ($x1 - $x0) * $s / 12), [int]($y0 + ($y1 - $y0) * $s / 12)) | Out-Null; Start-Sleep -Milliseconds 30 }
  Start-Sleep -Milliseconds 150; [T]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero)
}
function SnipVia($how, $bl, $what, $retry = 1) {
  $t0 = Get-Date; StartWatch; Start-Sleep -Milliseconds 100
  switch ($how) {
    'winshifts' { [T]::Key(0x5B, $false); [T]::Key(0xA0, $false); [T]::Key(0x53, $false); [T]::Key(0x53, $true); [T]::Key(0xA0, $true); [T]::Key(0x5B, $true) }
    'app'       { Start-Process 'ms-screenclip:' }
    'prtscn'    { [T]::Key(0x2C, $false); [T]::Key(0x2C, $true) }
  }
  for ($k = 0; $k -lt 300 -and -not [T]::OverlayVisible(); $k++) { Start-Sleep -Milliseconds 10 }
  Start-Sleep -Milliseconds 600   # select the region with normal brightness back
  Drag $bl
  $w = StopWatch
  $f = NewShot $t0
  if (-not $f) { Check $false "${what}: no snip saved"; return }
  $b = [Drawing.Bitmap]::FromFile($f.FullName)
  $want = 5 * $bl.w - 12
  if ([Math]::Abs($b.Width - $want) -gt $want * 0.1 -and $retry -gt 0) {   # the drag missed: a test hiccup, not SnipFix
    "  (snip was $($b.Width) px wide instead of ~${want}: dragging again)"
    $b.Dispose(); Remove-Item $f.FullName -Force
    [T]::Key(0x1B, $false); [T]::Key(0x1B, $true); Start-Sleep 1
    return SnipVia $how $bl $what ($retry - 1)
  }
  $got = MeasureBlocks $b 0 0 ($b.Width / $want * $bl.w); $b.Dispose(); Remove-Item $f.FullName -Force
  CheckColours $got $what
  Report $w $what
}
function Report($w, $what) {
  if ($NoSnipFix) { return }
  Check ($w[0] -ge 0 -and $w[1] -gt $w[0]) ("${what}: SDR white lowered for {0} ms, restored" -f $(if ($w[1] -gt 0) { $w[1] - $w[0] } else { '?' }))
  Check ([Math]::Abs([T]::Nits() - $orig) -lt 1) "${what}: SDR white back to $orig nits"
}

$sf = $null
try {
  if (-not $NoSnipFix) {
    Get-Process SnipFix -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep 1
    $sf = Start-Process $Exe -PassThru; Start-Sleep 1
    Check (-not $sf.HasExited) "SnipFix running (pid $($sf.Id))"
  }

  "1. Edge window in FP16 mode (wide-gamut PNG on the page)"
  $h = OpenPage 'SnipFixWide'
  $bl = FindBlocks
  if (-not $bl) { Check $false 'reference blocks not found on screen'; throw 'no blocks' }
  Check $bl.over 'the window really is in FP16 mode (on-screen GDI read is 3x)'
  SnipVia 'winshifts' $bl 'Win+Shift+S'
  [T]::SetForegroundWindow($h) | Out-Null; Start-Sleep 1
  SnipVia 'app' $bl 'Snipping Tool started without a hotkey'
  [T]::SetForegroundWindow($h) | Out-Null; Start-Sleep 1
  SnipVia 'prtscn' $bl 'PrtScn'
  [T]::SetForegroundWindow($h) | Out-Null; Start-Sleep 1

  "2. Win+PrtScn (full screen to a file, no overlay)"
  $t0 = Get-Date; StartWatch; Start-Sleep -Milliseconds 100
  [T]::Key(0x5B, $false); [T]::Key(0x2C, $false); [T]::Key(0x2C, $true); [T]::Key(0x5B, $true)
  $w = StopWatch; $f = NewShot $t0
  if ($f) { $vs = [Windows.Forms.SystemInformation]::VirtualScreen; $b = [Drawing.Bitmap]::FromFile($f.FullName)
    $got = MeasureBlocks $b ($bl.x0 - $vs.X) ($bl.y0 - $vs.Y + 10) $bl.w; $b.Dispose(); Remove-Item $f.FullName -Force; CheckColours $got 'Win+PrtScn' } else { Check $false 'Win+PrtScn: no file' }
  Report $w 'Win+PrtScn'

  "3. Alt+PrtScn (active window to the clipboard)"
  [T]::SetForegroundWindow($h) | Out-Null; Start-Sleep 1
  StartWatch; Start-Sleep -Milliseconds 100
  [T]::Key(0xA4, $false); [T]::Key(0x2C, $false); [T]::Key(0x2C, $true); [T]::Key(0xA4, $true)
  $w = StopWatch; Start-Sleep -Milliseconds 300
  $img = [Windows.Forms.Clipboard]::GetImage()   # PowerShell 7 runs in STA on Windows
  if ($img) {
    $b = New-Object Drawing.Bitmap $img; $found = $null
    for ($y = 0; $y -lt $b.Height -and -not $found; $y += 2) { for ($x = 0; $x -lt $b.Width - $bl.w; $x += 2) { $c = $b.GetPixel($x, $y)
      # a grey run as wide as a block (the title bar has small grey bits)
      if (($c.R -eq 0x80 -or $c.R -eq 0xD2) -and $c.G -eq $c.R -and $c.B -eq $c.R -and $b.GetPixel($x + [int]($bl.w * 0.8), $y).ToArgb() -eq $c.ToArgb()) { $found = @($x, $y); break } } }
    "  clipboard image {0}x{1}, grey block found at {2}" -f $b.Width, $b.Height, ($found -join ',')
    if ($found) { $l = $found[0]; while ($l -gt 0 -and $b.GetPixel($l - 1, $found[1]).ToArgb() -eq $b.GetPixel($found[0], $found[1]).ToArgb()) { $l-- }
      if ($l - 3 * $bl.w -ge 0) { $got = MeasureBlocks $b ($l - 3 * $bl.w) ($found[1] + 2) $bl.w; CheckColours $got 'Alt+PrtScn' } else { Check $false "Alt+PrtScn: grey block at x=$l is too close to the left edge" } }
    else { Check $false 'Alt+PrtScn: blocks not found in the clipboard image' }
    $b.Dispose()
  } else { Check $false 'Alt+PrtScn: no image in the clipboard' }
  Report $w 'Alt+PrtScn'
  CloseEdge

  "4. Normal window (no wide-gamut image): unchanged"
  $h = OpenPage 'SnipFixPlain'
  $bl = FindBlocks
  if ($bl) { Check (-not $bl.over) 'the plain window is not in FP16 mode'; SnipVia 'winshifts' $bl 'Win+Shift+S on a plain window' } else { Check $false 'blocks not found' }
  CloseEdge

  if (-not $NoSnipFix) {
    "5. Crash safety: SnipFix killed while SDR white is lowered"
    Stop-Process -Id $sf.Id -Force; Start-Sleep 1
    $saved = [byte[]]::new(24)   # Target { LUID adapter; UINT32 id; UINT32 level } as SnipFix writes it
    [BitConverter]::GetBytes([uint32][T]::TargetId()).CopyTo($saved, 8)
    [BitConverter]::GetBytes([uint32]([Math]::Round($orig * 1000 / 80))).CopyTo($saved, 12)
    New-Item 'HKCU:\Software\SnipFix' -Force | Out-Null
    Set-ItemProperty 'HKCU:\Software\SnipFix' -Name Pending -Value $saved -Type Binary
    [T]::SetNits(80) | Out-Null
    $sf = Start-Process $Exe -PassThru; Start-Sleep 2
    Check ([Math]::Abs([T]::Nits() - $orig) -lt 1) "restart puts SDR white back to $orig nits (reads $([T]::Nits()))"
    Check ($null -eq (Get-ItemProperty 'HKCU:\Software\SnipFix' -Name Pending -ErrorAction SilentlyContinue)) 'pending marker cleared'
  }
} finally {
  if ([Math]::Abs([T]::Nits() - $orig) -ge 1) { [T]::SetNits($orig) | Out-Null; "  (test restored SDR white to $orig nits)" }
  CloseEdge
  Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
}
if ($fail) { "FAILED: $fail"; exit 1 } else { "ALL PASSED" }
