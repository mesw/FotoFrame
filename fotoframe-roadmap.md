# FotoFrame — Roadmap and Requirements

---

## 1. Vision

FotoFrame is a Qt 6 / C++ application that turns a dedicated display — a TV, a wall-mounted monitor, a digital photo frame — into a living window onto a personal photo and video collection. Rather than cycling through images one at a time in a static grid, FotoFrame renders a physics-driven floating canvas where photos and short videos drift, overlap, and cycle continuously at 60 fps. The result is ambient and alive: glance at it and you see a moment from a decade ago; glance again and a trip from last summer has drifted into view.

The driving design principle is that a good photo display should surface the right memory at the right time without requiring any active management. FotoFrame achieves this by reading directly from a digiKam SQLite database — consuming the face tags, GPS data, ratings, and hierarchical tags that a photographer has already built up — and using that metadata to make intelligent, contextual decisions about what to show, how to layer it, and how to animate it.

FotoFrame is personal software that will be released as open source. It is intentionally scoped to a single data source (digiKam) and a single display paradigm (the floating canvas). It does not compete with full photo management tools; it is the display layer for people who already invest in carefully curating their libraries.

---

## 2. Target Users

**Primary user:** A photographer or photo-enthusiast running digiKam on Windows or Linux, with a curated library that includes tagged faces, GPS data, and ratings. They have a spare display — a TV connected to a home server or HTPC, a Raspberry Pi driving a wall panel — and want it to show their photos without any day-to-day management.

**Secondary user:** A household member (partner, family) who interacts with the canvas passively (glancing at it) or actively (tapping to rate, tag, or save a favourite) without needing to understand the underlying library.

**Context of use:**
- Always-on ambient display in a living room, kitchen, or hallway
- Display is typically viewed from a distance (2–5 m) in living conditions — not operated like a desktop app
- Interaction is occasional and intentional: tap to go fullscreen on a video, long-press to flag a photo, use curation mode to process a backlog
- The application starts automatically and runs unattended

---

## 3. Core Requirements

### 3.1 Database Integration

- Open `digikam4.db` (SQLite) read-only for display purposes; gamified labelling writes back to digiKam (see §3.5).
- Auto-detect the database path by reading the digiKam config file:
  - Windows: `%APPDATA%\digikam\digikamrc` (KConfig INI format), key `[Database Settings] / Database Name`
  - Linux: `~/.config/digikamrc` or equivalent XDG config path
- Prompt the user to locate the database manually if auto-detection fails.
- Construct full file paths as: `AlbumRoots.specificPath + Albums.relativePath + '/' + Images.name`
- Read and expose the following data per image:
  - File path, creation date, orientation, dimensions
  - Rating (from `ImageInformation`)
  - Tags (from `Tags` / `ImageTags`), including the `auto/` YOLO subtree
  - Face bounding boxes (from `ImageTagProperties` where `property = 'tagRegion'`; value is XML `<rect x y width height/>` in original pixel coordinates)
  - GPS / location data (from `ImagePositions`)
- Handle schema changes gracefully: query only well-known tables and columns; log and skip unknown or missing fields rather than crashing.
- Refresh the in-memory photo pool periodically (or on a file-system watch event) so newly imported photos appear without restarting the application.

### 3.2 Floating Canvas Display Engine

- Render a fullscreen floating canvas at a target of **60 fps** with no visible jitter.
- Photos and videos appear as floating items that drift slowly across the screen, overlapping naturally.
- Items drift off the screen edge and are removed; new items enter from the edges to replace them.
- Image decoding runs on a `QThreadPool` worker (`ImagePreloader`), always 1–2 items ahead of display. Decoded frames are served through a custom `QQuickImageProvider` (cache hand-off only; no decoding on the render thread).
- Pan-zoom animation uses QML `NumberAnimation` with `Easing.Linear` on `x`, `y`, and `scale` — pure scene graph, zero CPU per frame.
- Two `Image` items (A/B pattern) are used for transitions; opacity crossfade runs concurrently with the slide.

**Layering:**
- Images with no detected faces render in the background layer.
- Images with detected faces render in the foreground layer.

**Physics engine:**
- Items have mass; mass is proportional to the total face area in the image (sum of face bounding box areas). Images with no faces have a baseline low mass.
- Items collide and nudge; the physics model should feel loose and organic, not rigid.
- Overlap reduction logic prevents faces from being obscured by overlapping items (heads should not be cut off by item edges or occluded by higher-z items). Exact algorithm TBD — candidates include z-sort-by-face-density and push-apart impulses when face regions overlap.
- The number of simultaneous items on screen is a tuning parameter, to be determined by performance testing on target hardware.

**Pan-zoom choreography (per the `PanZoomCalculator` module):**
- Slide direction is chosen using a layered priority:
  1. Face centroid gravity: direction from the centroid of current image's faces toward next image's faces
  2. Aspect ratio constraint: portrait images prefer the vertical axis; landscape prefer horizontal
  3. YOLO semantic tags: e.g. `auto/sky`, `auto/mountain` favour a horizontal slide
  4. Event continuity: photos taken within 30 minutes of each other maintain direction
  5. Fallback: cycle Left → Up → Right → Down
- Image A's pan ends at the slide-exit edge; image B's pan starts at the entry edge and moves toward its subject centroid.

### 3.3 Smart Display Modes

Smart modes are contextual filters and weightings applied to the photo pool. They are not mutually exclusive; the system may blend multiple modes simultaneously via weighting.

| Mode | Behaviour |
|---|---|
| **This Day Over the Years** | Surface photos taken on today's date (day + month) across all years in the library |
| **Seasonal** | Surface photos whose tags match the current meteorological season (e.g. `snow`, `winter`, `beach`, `summer`, `autumn`, `foliage`, `spring`) |
| **Upcoming Season** | As the next season approaches (configurable lead-in window), begin transitioning the pool toward next-season content |
| **People Focus** | Feature a specific person or family grouping, identified by digiKam face tags |
| **Places / Trips** | Cluster and surface photos from the same GPS location cluster or digiKam album that represents a trip |
| **Recently Added** | Weight newly imported photos heavily so they surface before getting buried |
| **Anniversaries** | Detect recurring annual dates in the library (same day/month across multiple years) and surface them on or near that date |

- The active mode (or mode blend) is readable from the display and switchable via interaction or config.
- Seasonal tag matching is driven by digiKam tags; the mapping from season to tag keywords should be user-configurable.

### 3.4 Video Support

- Videos in the digiKam library are sourced and displayed alongside photos.
- Videos float in the canvas as small frames with no audio and no playback controls visible.
- Tapping or clicking a floating video expands it to fullscreen with audio and playback controls.
- Returning from fullscreen video returns to the floating canvas.
- Short clips are preferred as floating items. Handling of long videos (e.g. loop a clip, show thumbnail only, skip entirely) is TBD — see Open Questions.
- Video decoding and playback use the Qt Multimedia module (Qt 6).

### 3.5 Interaction and Gamification

- The canvas is interactive via touch or mouse.
- **Pick / favourite:** The user can tap a floating item to flag it as a favourite.
- **Always-on labelling:** Gamified rating and favouriting are always available while the canvas is running — there is no separate mode to enter. The interaction trigger for labelling is TBD (candidates: long-press on an item, a persistent floating action button, a gesture) — see Open Questions.
- Write-back: ratings, tags, and picks applied in FotoFrame are written back to `digikam4.db` so that digiKam remains the single source of truth for the library. FotoFrame treats the database as read-only for display queries but holds a write connection scoped exclusively to user-initiated labelling actions.
- **Conveyor belt metaphor:** The canvas is a continuous stream; the user acts on items as they pass. The interaction model should reinforce this — it should feel fluid, not like interrupting a slideshow.
- No gamification mechanic is fully specified yet; candidates include streaks (X photos labelled today), surfacing "unlabelled faces" as a mini-game, and progress indicators for albums not yet reviewed.

### 3.6 Configuration

- At minimum, configuration includes: path to `digikam4.db` (auto-detected or user-specified), active display mode(s), and seasonal tag keyword mappings.
- Configuration is stored in a plain text file (INI or TOML); format TBD.
- A settings UI is not required for early releases; direct editing of the config file is acceptable initially.
- No KDE Frameworks dependency in FotoFrame. Config file parsing uses Qt facilities only (`QSettings` or a lightweight TOML parser).

### 3.7 Platform

- **Supported platforms:** Windows (primary), Linux.
- Build system: CMake. Qt 6.8+, MinGW 64-bit on Windows.
- The application targets fullscreen exclusive display on a dedicated screen; multi-monitor awareness (launch on a specific display) is required.
- No installer is required for early releases; portable distribution is acceptable.
- Open source release: licence TBD (candidates: GPL-2.0+ to match Qt licensing norms, or MIT/Apache-2.0 for broader reuse).

---

## 4. Release Plan

Releases are feature-driven. No dates are assigned. Each version has a clear theme; later features depend on earlier ones.

---

### v0.1 — Database Foundation

**Theme:** Read photos from digiKam and display them. Nothing else.

- Open `digikam4.db` read-only via Qt SQL / QSQLITE driver
- Auto-detect database path from `digikamrc` on Windows and Linux; prompt if not found
- Construct full file paths from `AlbumRoots` + `Albums` + `Images`
- Read image metadata: path, date, rating, orientation, dimensions
- Read face bounding boxes from `ImageTagProperties` (parse XML `<rect/>` format)
- Read tags from `Tags` / `ImageTags`
- `PhotoListModel` exposed to QML; simple grid or list view for verification
- `ImagePreloader`: background-thread decoding via `QThreadPool`, custom `QQuickImageProvider`
- Basic fullscreen window, correct display on target monitor
- CMake build verified on Windows (MinGW) and Linux

**Exit criterion:** Application opens, reads a real digiKam library, and displays a scrollable list of decoded photos.

---

### v0.2 — Pan-Zoom Slideshow

**Theme:** Smooth, face-aware Ken Burns slideshow — the foundation of all animation work.

- A/B `Image` item pattern with opacity crossfade
- `PanZoomCalculator`: full slide-direction algorithm (face centroid, aspect ratio, YOLO tags, event continuity, fallback)
- `PanZoomImage.qml`: `NumberAnimation` on `x`, `y`, `scale` with `Easing.Linear`
- `SlideshowController`: playlist management, preload triggers, configurable display duration
- Consistent 60 fps verified; frame timing logged in debug builds
- `OnThisDayView.qml`: basic "This Day Over the Years" query and display

**Exit criterion:** A smooth, jitter-free pan-zoom slideshow runs for an extended period with no frame drops on target hardware.

---

### v0.3 — Floating Canvas

**Theme:** Replace the slideshow with the multi-item floating canvas.

- Multiple items rendered simultaneously on a `Canvas` or `Item`-based scene
- Slow drift animation per item (velocity, direction)
- Items exit at screen edges and are replaced by new items entering from edges
- Background / foreground layering: no-face images behind, face images in front
- Tunable item count; initial value determined by performance testing
- Basic collision detection and separation impulses (items nudge apart)
- Face overlap avoidance: z-ordering and push-apart logic to prevent face occlusion
- Mass proportional to face area; heavier items drift more slowly and push lighter ones

**Exit criterion:** Canvas runs with multiple simultaneous items, faces stay in foreground and unobscured, 60 fps maintained.

---

### v0.4 — Smart Display Modes

**Theme:** Contextual curation — the right photos at the right time.

- **This Day Over the Years** — refined and promoted from v0.2 prototype
- **Seasonal** — current-season detection; tag keyword mapping in config
- **Upcoming Season** — configurable lead-in window; blend toward next season
- **Recently Added** — weight boost for photos imported within a configurable window
- **Anniversaries** — detect recurring dates; surface on or near the date
- Mode blending: weighted combination of multiple active modes
- Active mode displayed as a small overlay label (dismissible)
- Modes configurable via config file

**Exit criterion:** Each mode demonstrably changes which photos surface; blending works correctly; config round-trips cleanly.

---

### v0.5 — Video Support

**Theme:** Short video clips as first-class floating items.

- Videos sourced from digiKam library alongside photos
- Videos float in canvas as small frames; Qt Multimedia for decoding; no audio, no controls while floating
- Tap / click a floating video → fullscreen playback with audio and basic controls (play/pause, seek, close)
- Return from fullscreen → resume floating canvas
- Long video handling implemented per decision in Open Questions
- Performance: video decoding must not degrade canvas frame rate

**Exit criterion:** Videos appear naturally in the canvas; tapping one plays it fullscreen; returning to canvas is seamless.

---

### v0.6 — Interaction Layer

**Theme:** The canvas becomes interactive.

- Touch and mouse input handling on floating items
- Tap to flag as favourite; long-press or chosen trigger to open quick-tag / rate popover
- Ratings, tags, and picks written back to `digikam4.db` (scoped write connection)
- Visual feedback on interaction (item highlight, confirmation animation)
- Tap floating video → fullscreen (from v0.5, now integrated with general interaction model)
- "People Focus" and "Places / Trips" modes added (require interaction to select person/place)
- Interaction model validated on touch display

**Exit criterion:** All tap and long-press interactions work reliably on both touch and mouse; labels written in FotoFrame are visible in digiKam on next launch.

---

### v0.7 — Gamification and Labelling

**Theme:** Make reviewing and labelling photos rewarding, without interrupting the ambient display.

- Always-on labelling: rating and favouriting available at any time via the chosen interaction trigger (TBD — see Open Questions)
- Gamification mechanic (TBD from Open Questions): at minimum, a count of items labelled in the current session
- "Unlabelled faces" surfacing: prioritise images with detected faces that have no name tag
- Curation session history: log what was rated/tagged and when

**Exit criterion:** A user can process a backlog of untagged photos from the canvas and verify the results appear correctly in digiKam.

---

### v0.8 — Configuration and Polish

**Theme:** Make it easy to set up and run unattended.

- Settings UI (in-app) for: database path, display mode selection, seasonal tag mappings, item count, transition speed
- Startup wizard for first-run database detection
- Multi-monitor support: select which display FotoFrame uses
- Autostart configuration helpers (Windows Task Scheduler / Linux systemd unit or `.desktop` autostart)
- Error handling and recovery: missing files, corrupt database, missing Qt Multimedia codec
- Performance profiling pass; frame time overlay in debug builds
- Logging to file for unattended operation diagnostics

**Exit criterion:** A new user can install and configure FotoFrame without editing any files manually; the application runs unattended for 24+ hours without issues.

---

### v1.0 — Open Source Release

**Theme:** Public release quality.

- All v0.x features stable and tested on Windows and Linux
- Licence file committed; repository structure cleaned for public consumption
- `README.md` with build instructions, dependency list, digiKam compatibility notes
- `CONTRIBUTING.md` and issue templates
- Release binary for Windows (portable ZIP); Linux build instructions provided
- Known issues documented; no P0/P1 bugs open
- `CLAUDE.md` updated to reflect final architecture

---

## 5. Relationship to digiKam

FotoFrame is a companion application to digiKam, not a fork or replacement. The relationship between the two is intentional and worth stating explicitly.

**Read-only display, read-write labelling.** FotoFrame opens `digikam4.db` read-only for all display and querying purposes. The one exception is user-initiated labelling: when a user rates, tags, or picks a photo from the floating canvas, FotoFrame writes that action back to `digikam4.db` using a scoped write connection. This design keeps digiKam as the single source of truth for the library. There is no FotoFrame-local database to synchronise, no sidecar format to import — labels applied in FotoFrame appear in digiKam immediately, as if they had been applied there directly.

**Complementary scope.** digiKam is a full-featured photo management application: import, organise, edit, export, face recognition, geolocation, and more. FotoFrame does none of that. Its entire scope is the display layer: take a well-curated digiKam library and make it beautiful and alive on a dedicated ambient screen. The two applications serve different moments — digiKam is where you sit down and manage your library; FotoFrame is what runs on the wall while you live your life.

**Potential upstream contribution.** Once FotoFrame is released as open source, there is a natural opportunity to engage with the digiKam project. The face-aware pan-zoom logic (`PanZoomCalculator`), the physics-driven canvas, and the gamified curation UX are non-trivial pieces of work that could be valuable to the wider digiKam ecosystem — either as a contributed module, a plugin, or a formally recognised companion project. This is not a commitment, but it is a desirable outcome: keeping the ecosystem coherent means making it easy for digiKam users to discover and adopt FotoFrame, and for improvements in either project to flow back to the other.

---

## 6. Open Questions

The following items are intentionally unresolved and should be decided before or during the relevant release.

**Interaction trigger for always-on labelling**
Gamified favouriting and labelling are always available (no separate mode to enter), but the interaction trigger is undecided. Candidates: long-press on a floating item; a persistent floating action button (always visible or appearing on canvas touch/hover); a swipe gesture (e.g. swipe-up to like, swipe-down to dismiss). The trigger must work on both touch displays and mouse, be discoverable, and not interfere with passive viewing. Decision needed before v0.6.

**Long video handling**
Videos longer than a configurable threshold (e.g. > 3 minutes) have unclear behaviour in the floating canvas. Options: loop a random clip segment; show a static thumbnail; skip entirely; let the user configure per-video. Decision needed before v0.5.

**Number of simultaneous canvas items**
The target item count on-screen at once is not yet determined — it depends on GPU capability on target hardware (Raspberry Pi, integrated GPU, discrete GPU) and the cost of physics simulation per item. Needs measurement before v0.3 is called complete.

**Physics engine scope**
Full collision resolution with mass and impulse is ambitious. The minimum viable physics is: slow drift + edge cycling + z-layer separation. Whether to invest in a proper 2D physics library (e.g. Box2D via a Qt binding) or implement a lightweight custom simulation needs a decision before v0.3.

**Overlap avoidance algorithm**
Face occlusion prevention could be implemented as: (a) z-sort by face density only; (b) push-apart impulses when face bounding regions overlap in screen space; (c) layout-aware placement at item spawn time. These have different complexity/quality trade-offs. Decision needed before v0.3.

**Configuration file format**
INI (`QSettings`) is available with no new dependencies; TOML is more human-readable and handles nested structures cleanly but requires a third-party parser. Decision needed before v0.8 but should be settled early to avoid migration.

**Gamification mechanics**
The "make labelling fun" goal is stated but no mechanic is designed. Candidates: daily label streak, progress bar per album, surfacing a "mystery photo" (a face with no name tag) as a daily challenge. Needs product design before v0.7.

**Open source licence**
GPL-2.0-or-later aligns with Qt's open source licensing but restricts downstream use. MIT/Apache-2.0 is more permissive but may be incompatible with statically linking GPL Qt components. Licence decision needed before v1.0; should be investigated early.

**Linux autostart / kiosk mode**
Linux deployment scenarios vary widely (X11 vs Wayland, desktop environment vs bare compositor). The scope of officially supported Linux configurations needs to be scoped before v0.8 polish work begins.
