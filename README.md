# YouTube Audio

A terminal-based YouTube audio player with a TUI built using FTXUI. Search for videos via yt-dlp, stream audio using FFmpeg, and play it back through SDL2.

## Build

```bash
g++ -std=c++20 -O2 -Wall -Wextra \
  src/tui.cpp src/search.cpp src/audio.cpp \
  -o tui \
  -lftxui-component -lftxui-dom -lftxui-screen \
  -lavformat -lavcodec -lavutil -lswresample \
  -lSDL2
```

## Run

```bash
./tui
```
