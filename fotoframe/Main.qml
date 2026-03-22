import QtQuick

Window {
    id: root
    width: 1280
    height: 720
    visible: true
    title: qsTr("FotoFrame")
    color: "black"

    // ---------------------------------------------------------------
    // Test images – set these four paths to images on your machine.
    // Use forward slashes on Windows: "file:///C:/Users/You/pic.jpg"
    // ---------------------------------------------------------------
    readonly property var images: [
        "file:///C:/Users/MiCHi/Documents/Slideshow-Test/_PTB19_CUBE_20388.jpg",
        "file:///C:/Users/MiCHi/Documents/Slideshow-Test/_PTB19_CUBE_20398.jpg",
        "file:///C:/Users/MiCHi/Documents/Slideshow-Test/PTB19_FISH_023713.jpg",
        "file:///C:/Users/MiCHi/Documents/Slideshow-Test/_PTB19_BIRD_14613.jpg"
    ]

    // ---------------------------------------------------------------
    // Timing (milliseconds)
    // ---------------------------------------------------------------
    readonly property int displayMs:    6000   // time each image is shown
    readonly property int transitionMs: 1200   // slide duration
    readonly property int panMs:        displayMs + transitionMs  // Ken Burns span

    // ---------------------------------------------------------------
    // Ken Burns presets: [fromX, fromY, fromScale, toX, toY, toScale]
    //
    // x/y are pixel offsets applied to the image item (which is the
    // same size as the slot) with transformOrigin at its center.
    // The image is scaled from that center, so valid pan range is:
    //   |offsetX| ≤ (scale − 1) × slotWidth  / 2
    //   |offsetY| ≤ (scale − 1) × slotHeight / 2
    // For a 1280×720 slot:
    //   scale 1.25 → ±160 px wide, ±90 px tall
    //   scale 1.30 → ±192 px wide, ±108 px tall
    // ---------------------------------------------------------------
    readonly property var kbPresets: [
        [   0,    0,  1.00,  120,   60,  1.30 ],   // zoom in,  drift right+down
        [  80,  -50,  1.25,  -80,   50,  1.25 ],   // pan left+down, constant zoom
        [   0,    0,  1.10,  -80,  -40,  1.30 ],   // zoom in,  drift left+up
        [ -80,   50,  1.25,   80,  -50,  1.25 ],   // pan right+up, constant zoom
    ]

    // ---------------------------------------------------------------
    // Slide direction vectors: where the current image exits.
    //   (-1,  0) → exits left,  next enters from right
    //   ( 0, -1) → exits up,    next enters from below
    //   ( 1,  0) → exits right, next enters from left
    //   ( 0,  1) → exits down,  next enters from above
    // ---------------------------------------------------------------
    readonly property var dirs: [
        Qt.point(-1,  0),
        Qt.point( 0, -1),
        Qt.point( 1,  0),
        Qt.point( 0,  1),
    ]

    // ---------------------------------------------------------------
    // Mutable state
    // ---------------------------------------------------------------
    property int  currentIdx:    0
    property int  dirIdx:        0
    property bool aIsFront:      true
    property bool transitioning: false

    // ---------------------------------------------------------------
    // Apply a Ken Burns preset to an image and start its animations.
    // Sets the image to the start position immediately, then runs the
    // drift over panMs so the motion is already underway when the
    // slot slides into view.
    // ---------------------------------------------------------------
    function applyKenBurns(img, animX, animY, animS, presetIdx) {
        var p     = kbPresets[presetIdx % kbPresets.length]
        img.x     = p[0]
        img.y     = p[1]
        img.scale = p[2]
        animX.from = p[0]; animX.to = p[3]; animX.start()
        animY.from = p[1]; animY.to = p[4]; animY.start()
        animS.from = p[2]; animS.to = p[5]; animS.start()
    }

    // ---------------------------------------------------------------
    // Slide to the next image.
    // ---------------------------------------------------------------
    function doTransition() {
        if (transitioning) return
        transitioning = true

        var ni  = (currentIdx + 1) % images.length
        var dir = dirs[dirIdx % dirs.length]
        dirIdx++

        var outSlot = aIsFront ? slotA : slotB
        var inSlot  = aIsFront ? slotB : slotA
        var inImg   = aIsFront ? imgB  : imgA
        var inAnimX = aIsFront ? kbBx  : kbAx
        var inAnimY = aIsFront ? kbBy  : kbAy
        var inAnimS = aIsFront ? kbBs  : kbAs

        // Place incoming slot off-screen on the entry side, then load.
        inSlot.x = -dir.x * root.width
        inSlot.y = -dir.y * root.height
        inSlot.z = 1
        outSlot.z = 0
        inImg.source = images[ni]
        applyKenBurns(inImg, inAnimX, inAnimY, inAnimS, ni)

        // Outgoing slot exits in dir; incoming slot arrives at (0,0).
        outXAnim.target = outSlot; outXAnim.from = outSlot.x; outXAnim.to =  dir.x * root.width
        outYAnim.target = outSlot; outYAnim.from = outSlot.y; outYAnim.to =  dir.y * root.height
        inXAnim.target  = inSlot;  inXAnim.from  = inSlot.x;  inXAnim.to  = 0
        inYAnim.target  = inSlot;  inYAnim.from  = inSlot.y;  inYAnim.to  = 0
        slideAnim.start()

        currentIdx = ni
        aIsFront   = !aIsFront
    }

    // ---------------------------------------------------------------
    // Scene: two full-screen clipped slots (A on top initially).
    // clip:true on each slot keeps the scaled/panned image from
    // bleeding into the adjacent slot during a transition.
    // ---------------------------------------------------------------
    Item {
        id: scene
        anchors.fill: parent
        focus: true

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_F || event.key === Qt.Key_F11) {
                root.visibility = (root.visibility === Window.FullScreen)
                    ? Window.Windowed : Window.FullScreen
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                Qt.quit()
                event.accepted = true
            }
        }

        // Slot B – starts off-screen to the right
        Item {
            id: slotB
            width: root.width; height: root.height
            x: root.width
            clip: true

            Image {
                id: imgB
                x: 0; y: 0
                width: parent.width; height: parent.height
                fillMode:        Image.PreserveAspectCrop
                smooth:          true
                asynchronous:    true
                transformOrigin: Item.Center
            }
        }

        // Slot A – visible at start
        Item {
            id: slotA
            width: root.width; height: root.height
            x: 0
            clip: true

            Image {
                id: imgA
                x: 0; y: 0
                width: parent.width; height: parent.height
                fillMode:        Image.PreserveAspectCrop
                smooth:          true
                asynchronous:    true
                transformOrigin: Item.Center
            }
        }
    }

    // ---------------------------------------------------------------
    // Ken Burns animations – linear drift, one set per slot.
    // Duration spans display + transition so the pan continues
    // smoothly while the slot is sliding in or out.
    // ---------------------------------------------------------------
    NumberAnimation { id: kbAx; target: imgA; property: "x";     duration: root.panMs; easing.type: Easing.Linear }
    NumberAnimation { id: kbAy; target: imgA; property: "y";     duration: root.panMs; easing.type: Easing.Linear }
    NumberAnimation { id: kbAs; target: imgA; property: "scale"; duration: root.panMs; easing.type: Easing.Linear }
    NumberAnimation { id: kbBx; target: imgB; property: "x";     duration: root.panMs; easing.type: Easing.Linear }
    NumberAnimation { id: kbBy; target: imgB; property: "y";     duration: root.panMs; easing.type: Easing.Linear }
    NumberAnimation { id: kbBs; target: imgB; property: "scale"; duration: root.panMs; easing.type: Easing.Linear }

    // ---------------------------------------------------------------
    // Slide transition – four NumberAnimations in parallel with
    // dynamically assigned targets (set before each start() call).
    // ---------------------------------------------------------------
    ParallelAnimation {
        id: slideAnim
        NumberAnimation { id: outXAnim; property: "x"; duration: root.transitionMs; easing.type: Easing.InOutCubic }
        NumberAnimation { id: outYAnim; property: "y"; duration: root.transitionMs; easing.type: Easing.InOutCubic }
        NumberAnimation { id: inXAnim;  property: "x"; duration: root.transitionMs; easing.type: Easing.InOutCubic }
        NumberAnimation { id: inYAnim;  property: "y"; duration: root.transitionMs; easing.type: Easing.InOutCubic }
        onStopped: {
            root.transitioning = false
            // The decoder now has the full displayMs window to finish.
            // Preload the image after next into the idle (off-screen) slot.
            var preloadIdx = (root.currentIdx + 1) % root.images.length
            var idleImg    = root.aIsFront ? imgB : imgA
            idleImg.source = root.images[preloadIdx]
        }
    }

    // ---------------------------------------------------------------
    // Auto-advance timer
    // ---------------------------------------------------------------
    Timer {
        interval: root.displayMs
        repeat:   true
        running:  true
        onTriggered: root.doTransition()
    }

    // ---------------------------------------------------------------
    // Bootstrap
    // ---------------------------------------------------------------
    Component.onCompleted: {
        scene.forceActiveFocus()
        imgA.source = images[0]
        applyKenBurns(imgA, kbAx, kbAy, kbAs, 0)
        // Preload image[1] immediately so the decoder has the full
        // first display window (6 s) before the first transition.
        imgB.source = images[1 % images.length]
    }
}
