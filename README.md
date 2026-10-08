# OpenStreetMap renderer and viewer

This repository contains an OpenStreetMap renderer based on [Blend2d](https://blend2d.com). It loads maps in XML (`.osm`) format and renders the entire pat or its region into a Blend2d image.

The viewer provided displays the rendered map via [SDL3](https://github.com/libsdl-org/SDL) and [ImGui](https://github.com/ocornut/imgui).

![screenshot](./screenshot.png)

## Build and run

All dependencies are included in this repository.

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/viewer/viewer map.osm
```

To export the image without opening a window:

```sh
./build/viewer/viewer map.osm --snapshot map-preview.png
```
