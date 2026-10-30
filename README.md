# Battle.net and World of Warcraft on FreeBSD (native Wine)

Two small patches to Wine that get the Blizzard Battle.net launcher working
on FreeBSD with native Wine, without the Linux compatibility layer
(linuxulator). With them, the launcher logs in and installs and launches
World of Warcraft Classic.

Tested on:

- FreeBSD 15.1-RELEASE, amd64
- `emulators/wine-devel` 11.20 from ports (WoW64 build)
- Battle.net launcher with Chromium Embedded Framework 108.0.5359.125
- WoW Classic (Anniversary) with DXVK, AMD Radeon 880M (radv)
- Hyprland, with the launcher running under Xwayland

## Symptoms without the patches

- The Battle.net login window shows its title bar and settings button, but the
  login page stays blank, or black windows keep popping up.
- The launcher log
  (`users/<you>/AppData/Local/Battle.net/Logs/battle.net-*.log`) repeats
  `Render process was terminated and reloaded automatically.` every few seconds.
- Wine prints `Unhandled exception 0x80000003 ... libcef+0x16d00e1`.
- Once the crash is fixed, the login page can still sit on a spinner forever.
  At that point the browser's renderer process is in state `TX` (stopped and
  traced by `wineserver`). `winedbg` can't attach to processes at all
  (`Can't attach process ...: error 5`).

The usual Battle.net tips don't help with any of this: turning off hardware
acceleration, `--disable-gpu`, disabling DXVK for the launcher, or switching
between X11 and Wayland.

## Fix 1: report written copy-on-write pages as read-write (`ntdll`)

[`patches/patch-dlls_ntdll_unix_virtual.c`](patches/patch-dlls_ntdll_unix_virtual.c)

The launcher's login page is Chromium, via CEF. Chromium keeps some data in a
"protected memory" section of `libcef.dll`. That section is writable while
Chromium starts up and is then sealed read-only. Sealing calls
`VirtualProtect(PAGE_READONLY)`, and Chromium crashes on purpose (`int3`) unless
the *previous* protection was `PAGE_READWRITE`:

```c
DWORD old;
if (!VirtualProtect(page, size, PAGE_READONLY, &old)) IMMEDIATE_CRASH();
if (old != PAGE_READWRITE)                       IMMEDIATE_CRASH(); /* libcef+0x16d00e1 */
```

Writable sections of a DLL image are copy-on-write. On Windows, a page that has
been written becomes a private `PAGE_READWRITE` page. Wine doesn't track whether
a copy-on-write page has been written, so it keeps reporting `PAGE_WRITECOPY`
and the check fails. This isn't FreeBSD-specific; upstream Wine fails it on
every OS. wine-staging and Proton-GE carry a fuller fix (they track which pages
have been copied), which is why the launcher works with them on Linux.

The patch only changes the *old* protection that `NtProtectVirtualMemory`
returns. Write-copy is reported as the matching read-write value
(`PAGE_WRITECOPY` becomes `PAGE_READWRITE`, and `PAGE_EXECUTE_WRITECOPY`
becomes `PAGE_EXECUTE_READWRITE`). `VirtualQuery` is unchanged. It's a
workaround rather than real tracking: a page that was never written is also
reported as read-write.

## Fix 2: handle FreeBSD's ptrace attach stop (`wineserver`)

[`patches/patch-server_ptrace.c`](patches/patch-server_ptrace.c)

On FreeBSD, `wineserver` uses `ptrace` to read and write another process's
memory and to get or set thread contexts. Chromium's sandbox relies on these
operations when it sets up its renderer process.

Before each of these operations, `suspend_for_ptrace()` calls `PT_ATTACH` and
waits for the target to stop with SIGSTOP. If the target stops for a different
signal first, Wine re-delivers that signal with `PT_CONTINUE` and keeps waiting
for SIGSTOP. On FreeBSD, the target can report a pending signal (such as Wine's
own SIGUSR1) as its first stop, and after that no SIGSTOP comes. This happens
with the default `debug.ptrace_attach_transparent=1`. Wine then waits until its
watchdog fires and tries to detach while the process is running, which fails
with `EBUSY`. The operation returns `STATUS_ACCESS_DENIED`, and the target can
be left stopped and traced.

On FreeBSD only, the patch treats any stop that follows `PT_ATTACH` as the
attach stop, since the process is stopped either way. It remembers the signal
and delivers it with `PT_DETACH`, so the program still receives it. FreeBSD's
`PT_DETACH` already throws away the pending attach SIGSTOP.

This also fixes `winedbg` failing to attach on FreeBSD.

### Optional: mount procfs

When `/proc/<pid>/mem` exists, `wineserver` reads and writes other processes'
memory with a single `pread`/`pwrite`. Otherwise it uses one `ptrace` call per
8 bytes. Neither patch needs procfs (this setup works without it), but mounting
it makes these reads much faster:

```sh
echo 'proc	/proc	procfs	rw	0	0' | sudo tee -a /etc/fstab
sudo mount /proc
```

No sysctl changes are needed.

## Building

`build.sh` sparse-clones the ports tree, copies the patches into
`emulators/wine-devel/files/`, builds and stages the port as your normal user,
and copies the staged tree to `~/wine-wc`. It doesn't touch the installed
`wine-devel` package.

Install the runtime libraries (the `wine-devel` package pulls them in) and the
build tools:

```sh
sudo pkg install wine-devel winetricks bison gmake autoconf automake flex gcc14 \
    binutils llvm19 sdl2 evdev-proto xorgproto pkgconf desktop-file-utils
./build.sh
```

`WORK=` (build directory, default `~/src/freebsd-wine-battlenet`) and `DEST=`
(install directory, default `~/wine-wc`) override the defaults. The patches
were made against wine-devel 11.20. If the port has moved on and a patch no
longer applies, the changes are small enough to port by hand.

## Setting up Battle.net

```sh
install -m 755 launcher/battlenet ~/.local/bin/battlenet

# 64-bit (WoW64) prefix with DXVK
export WINEPREFIX=~/Games/battlenet
~/wine-wc/bin/wineboot -u
WINE=~/wine-wc/bin/wine winetricks -q dxvk corefonts

# install Battle.net with the official installer
fetch -o /tmp/Battle.net-Setup.exe \
    'https://downloader.battle.net/download/getInstaller?os=win&installer=Battle.net-Setup.exe'
battlenet /tmp/Battle.net-Setup.exe
```

After that, run `battlenet`, or install the menu entry:

```sh
install -m 644 launcher/battlenet.desktop ~/.local/share/applications/
```

The entry uses the icon name `battlenet`. Copy one of the PNGs Wine extracts
into `~/.local/share/icons/hicolor/<size>/apps/` to `battlenet.png`, or edit
the `Icon=` line.

What the launcher script sets:

- It forces the X11 driver (Xwayland). Under winewayland the launcher window
  stayed blank.
- It disables `locationapi` (a common Battle.net workaround) and
  `winemenubuilder`, because the menu entries Wine generates would run the
  unpatched system `wine`.

On a HiDPI screen with Xwayland scaling turned off, set Wine's DPI to match, for
example 144 for 1.5× scaling:

```sh
WINEPREFIX=~/Games/battlenet ~/wine-wc/bin/wine reg add 'HKCU\Control Panel\Desktop' /v LogPixels /t REG_DWORD /d 144 /f
```

## License

The patches change Wine and are licensed like Wine, under LGPL-2.1-or-later.
The scripts in this repository are in the public domain.
