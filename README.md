# CBoomer

A C port of Boomer by Tsoding - a zoomer application for Linux that lets you explore your screen with smooth zooming and a flashlight effect

## Features

- Fullscreen screenshot viewer with smooth zoom and pan
- Flashlight effect that follows your cursor
- Configurable controls and behavior
- Clean, modular C code with single-header libraries

## Controls

| Control                         | Action                   |
|---------------------------------|--------------------------|
| <kbd>Esc</kbd>                  | Quit                     |
| <kbd>1</kbd>                    | Reset camera             |
| <kbd>2</kbd>                    | Toggle flashlight        |
| <kbd>=</kbd> or <kbd>+</kbd>    | Zoom in                  |
| <kbd>-</kbd> or <kbd>_</kbd>    | Zoom out                 |
| Drag with left mouse            | Pan the image            |
| Scroll wheel                    | Zoom in/out              |
| <kbd>Ctrl</kbd> + Scroll wheel  | Change flashlight radius |

## Project Structure

- cboomer.c - Main application logic
- config.h - Configuration (single-header)
- la.h - Linear algebra (single-header)
- screenshot.h - Screenshot capture (single-header)