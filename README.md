# SnipFix

Exact colours in Windows screenshots with HDR on. Works with the built-in Snipping
Tool (Win+Shift+S, PrtScn, its New button) and with Win+PrtScn / Alt+PrtScn.
11 KB, no installer, no C runtime, idles at zero CPU.

## The problem

With HDR on, Windows shows SDR apps at the "SDR content brightness" level (say 240
nits; scRGB 1.0 = 80 nits, so SDR white = 3.0). Normal 8-bit windows are boosted by
the compositor, and screen capture gets their original pixels: exact.

Chromium and Electron windows (Edge, Chrome, Discord, VS Code, Steam, ...) switch
their whole window to an FP16 scRGB backbuffer as soon as they show any image whose
colour space is not exactly sRGB: a Display P3 photo, an HDR image, even a PNG with
`gAMA`+`cHRM` chunks (Metacritic's ESRB rating logo is one). See
`ui/display/win/screen_win.cc` ("Use HDR color spaces only when there is WCG or HDR
content on the screen") and `cc/paint/paint_image.cc`. They then multiply their SDR
colours by 3 themselves, and every capture path (GDI, Win+PrtScn, Snipping Tool with
its corrector off) clips at 1.0: `#00CE7A` becomes `#00FFCA`, `#808080` becomes
`#D2D2D2`. Snipping Tool's "HDR screenshot color corrector" tone-maps every capture
instead (white becomes `#E1E1E1`).

## The fix

For the moment a screenshot is taken, SnipFix sets SDR white to 80 nits (the
multiplier becomes 1) and sends `WM_DISPLAYCHANGE` to Chromium windows: they read the
level only at start-up and on that message, and repaint in ~30 ms. Then it puts the
level back.

* Snipping Tool: its overlay window (`SnipOverlayRootWindow`) is created ~150 ms
  before the screen is grabbed. SnipFix lowers on creation and restores 50 ms after
  the overlay is shown (the frame is frozen by then). Any way of starting a snip works.
* Win+PrtScn, Alt+PrtScn (and PrtScn when it doesn't open snipping): a keyboard hook
  lowers before the key reaches Windows and restores 250 ms later.
* The screen dims for about a quarter of a second.
* Never left dimmed: a 2 s safety timer; the original level is kept in
  `HKCU\Software\SnipFix\Pending` while lowered and restored on the next start if
  SnipFix was killed in between.
* Nothing happens with HDR off or SDR white already at 80 nits.

Setting SDR white uses `DisplayConfigSetDeviceInfo` with the private type
`0xFFFFFFEE`, the call behind the Settings slider (also used by Twinkle Tray). A
Windows update could change it; SnipFix would then simply stop helping.

Leave Snipping Tool's "HDR screenshot color corrector" **off**.

Real HDR content (a game, an HDR video) still clips in an SDR screenshot, as before.

## Build and test

```sh
CC=x86_64-w64-mingw32-clang ./build.sh   # build/SnipFix.exe
./build.sh test                          # unit tests of the state machine
```

`tests/live_test.ps1` (PowerShell 7) tests a running SnipFix with the real Snipping
Tool: a test page with reference colours and a `gAMA`+`cHRM` PNG in a separate Edge
instance, every capture path, the dimming time, restoring, and crash recovery.
`-NoSnipFix` runs it as a control without SnipFix (everything on the FP16 window
fails).

---

# SnipFix (по-русски)

Точные цвета скриншотов при включённом HDR. Работает со штатными «Ножницами»
(Win+Shift+S, PrtScn, кнопка «Создать») и с Win+PrtScn / Alt+PrtScn.

Окна на Chromium (Edge, Chrome, Discord, VS Code, Steam) при любой картинке не в
sRGB переходят в HDR-формат и сами осветляют обычные цвета в 240/80 = 3 раза; захват
экрана это обрезает — скриншот пересвечен. SnipFix на время снимка (~0,25 с) ставит
яркость SDR 80 нит, сообщает окнам Chromium, что экран изменился, и возвращает вашу
яркость. Экран на мгновение темнеет. Корректор HDR в «Ножницах» должен быть
выключен. Значок в трее: автозапуск и выход.
