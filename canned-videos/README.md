# Canned Sega Videos

Canned videos for use in an emulator or streaming server can be downloaded and
installed with these scripts.

Linux or macOS, installed to `~/.local/share/Kinetoscope-Emulation/`:

```sh
./install.sh
```

Windows, installed to "C:\Users\USERNAME\AppData\Local\Kinetoscope-Emulation":

```ps1
./install.ps1
```

## Installing canned videos without the full source code:

Linux or macOS:

```sh
curl https://raw.githubusercontent.com/joeyparrish/kinetoscope/main/canned-videos/install.sh | bash
```

Windows:

```ps1
Invoke-WebRequest https://raw.githubusercontent.com/joeyparrish/kinetoscope/main/canned-videos/install.ps1 -OutFile install-canned-videos.ps1
.\install-canned-videos.ps1
```
