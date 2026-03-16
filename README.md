# CBoomer

![](./demo.gif)

A C port of [Boomer](https://github.com/tsoding/boomer) by Tsoding - a zoomer application for Linux. This port focuses on the core functionality (screenshot viewing, zoom, pan, flashlight) and does not yet implement live window tracking (honestly, I've never used it)

## Features

- Fullscreen screenshot viewer with smooth zoom and pan
- Flashlight effect that follows your cursor
- Configurable controls and behavior
- Clean, modular C code with single-header libraries

## Dependencies

- X11 development libraries (libX11, libXext, libXrandr)
- OpenGL development libraries (libGL, libGLX)
- GLEW (OpenGL Extension Wrangler)

Example for Void linux:

```
sudo xbps-install -S libglvnd-devel libX11-devel libXrandr-devel glew-devel
```

## Quick Start

```
make
./cboomer
```

## Default Controls

| Control                         | Action                   |
|---------------------------------|--------------------------|
| <kbd>Esc</kbd>                  | Quit                     |
| <kbd>1</kbd>                    | Reset camera             |
| <kbd>2</kbd>                    | Toggle flashlight        |
| <kbd>=</kbd>                    | Zoom in                  |
| <kbd>-</kbd>                    | Zoom out                 |
| Drag with left mouse            | Pan the image            |
| Scroll wheel                    | Zoom in/out              |
| <kbd>Ctrl</kbd> + Scroll wheel  | Change flashlight radius |

You can modify controls in `config.h`

## Project Structure

- cboomer.c - Main application logic
- config.h - Configuration
- la.h - Linear algebra
- screenshot.h - Screenshot capture