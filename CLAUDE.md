# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**fotoframe** is a lightweight C++/Qt 6 slideshow application that reads photo collections from a digikam SQLite database (`digikam4.db`). It is display-only — it never writes to the digikam database. The `digikam/` directory is a git submodule checked out for database schema reference only; no digikam code is compiled into fotoframe.

## Repository Layout

```
fotoframe/        — the application (CMake project, Qt 6 Quick + QML)
digikam/          — git submodule: digikam source, schema/DB reference only
```

All application work happens under `fotoframe/`.

## Build

The project uses CMake with the Qt 6.8 MinGW 64-bit kit on Windows. Qt Creator manages the build directory automatically:

```
build dir: fotoframe/build/Desktop_Qt_6_8_3_MinGW_64_bit-Debug/
```

To build from the command line (from the `fotoframe/` directory):
```bash
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=<Qt6_dir>
cmake --build build
```

The executable is `appfotoframe` (or `appfotoframe.exe` on Windows).

## Architecture

### Planned module layout (under `fotoframe/`)

```
config/
  DigiKamConfig        — reads digikamrc (QSettings INI) to locate digikam4.db
database/
  DigiKamDatabase      — opens digikam4.db read-only (Qt6::Sql / QSQLITE driver)
  Photo                — plain value type: id, filePath, creationDate, rating,
                         orientation, imageSize, faceRects (QList<QRectF>)
  PhotoQuery           — all SQL: byTag(), onThisDay(), byAlbum(), random()
models/
  PhotoListModel       — QAbstractListModel exposed to QML
  TagTreeModel         — QAbstractItemModel for tag hierarchy
slideshow/
  SlideshowController  — playlist, preload triggers, scope config
  ImagePreloader       — background thread decoding (QImageReader → QImage)
  PanZoomCalculator    — computes (panStart, panEnd, slideDirection) per image pair
qml/
  Main.qml
  SlideshowView.qml    — A/B flip Image items, slide + pan-zoom animations
  PanZoomImage.qml     — single image + NumberAnimation on x/y/scale
  CollectionBrowser.qml
  OnThisDayView.qml
```

### digikam database schema (key tables)

All data is read from `digikam4.db` (SQLite):

- **AlbumRoots** (`id`, `specificPath`) — physical root directories
- **Albums** (`id`, `albumRoot`, `relativePath`) — subdirectories
- **Images** (`id`, `album`, `name`, `status`, `category`) — image files
- **ImageInformation** (`imageid`, `rating`, `creationDate`, `orientation`, `width`, `height`) — EXIF-derived metadata
- **Tags** (`id`, `pid`, `name`) — hierarchical tags; YOLO auto-tags live under `"auto/"` subtree
- **ImageTags** (`imageid`, `tagid`) — many-to-many
- **ImageTagProperties** (`imageid`, `tagid`, `property`, `value`) — face bounding boxes stored here with `property = 'tagRegion'`, value is XML: `<rect x="X" y="Y" width="W" height="H"/>` in original image pixel coordinates
- **ImagePositions** — GPS data

Full file path for an image = `AlbumRoots.specificPath + Albums.relativePath + '/' + Images.name`.

digikam config file (Windows): `%APPDATA%\digikam\digikamrc` (KConfig INI format).
Relevant keys: `[Database Settings] / Database Type` and `Database Name` (the path to `digikam4.db`).

### Rendering / performance

The overriding constraint is **smooth 60 fps pan-zoom with zero jitter**.

- Image decoding runs on a `QThreadPool` worker (`ImagePreloader`) — always 1–2 images ahead
- Decoded `QImage` is served via a custom `QQuickImageProvider` that never decodes (cache hand-off only)
- Pan/zoom animation uses QML `NumberAnimation` with `easing.type: Easing.Linear` on `x`, `y`, `scale` — pure scene graph, zero CPU per frame
- Two `Image` items (A/B) are used for transitions; opacity crossfade runs concurrently with the slide

### Slide direction algorithm

`PanZoomCalculator` computes slide direction per image pair using a layered priority:
1. Face centroid gravity: direction from centroid of current image's faces → next image's faces
2. Aspect ratio constraint: portrait → vertical axis only; landscape → horizontal axis only
3. YOLO semantic tags (from DB): `auto/sky`, `auto/mountain` → horizontal; etc.
4. Event continuity: photos < 30 min apart → maintain direction
5. Fallback: cycle L → U → R → D

The pan-zoom choreography is coupled to the slide: image A's pan ends at the slide-exit edge; image B's pan starts at the entry edge, moves toward its subject centroid.

## Coding Conventions

- C++17, Qt 6 idioms throughout
- Clean code, industry-standard conventions
- No KDE Frameworks dependency in fotoframe (digikam uses KDE; fotoframe does not)
- QML files follow Qt Quick best practices: bindings over imperative code, `layer.enabled` for compositing
