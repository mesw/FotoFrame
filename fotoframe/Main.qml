import QtQuick
import QtQuick.Controls

Window {
    id: root
    width: 1280
    height: 720
    visible: true
    title: qsTr("FotoFrame")
    color: "black"

    // ── Photo data from C++ ───────────────────────────────────────────────
    // photoLibrary is a DigiKamLibrary instance set as a context property.
    //   photoLibrary.allTags       — QStringList, all tags with images (sorted)
    //   photoLibrary.selectedTags  — QStringList, read/write
    //   photoLibrary.photos        — QStringList, file:// URLs sorted by date
    //   photoLibrary.aspectRatios  — QList<qreal>, parallel to photos
    //   photoLibrary.totalAspectSum — qreal, sum of all aspect ratios
    //   photoLibrary.statusMessage — QString, error text or ""

    readonly property var    images:       photoLibrary.photos
    readonly property var    aspectRatios: photoLibrary.aspectRatios

    // ── River view settings ───────────────────────────────────────────────
    property real speed:   150   // px/sec
    property int  numRows: 4     // 1–10

    // ── Derived geometry ──────────────────────────────────────────────────
    readonly property real slotHeight: height / numRows
    // loopWidth = total display width of all photos end-to-end at slotHeight
    readonly property real loopWidth:  slotHeight * photoLibrary.totalAspectSum
    readonly property real staggerPx:  slotHeight * 2.5
    // Extra items to keep the right edge covered after per-row stagger offset
    readonly property int  extraItems: 20
    readonly property int  stripItems: Math.max(images.length, 1) + extraItems

    // ── Scroll state ──────────────────────────────────────────────────────
    property real scrollOffset: 0

    // ── Frame loop ────────────────────────────────────────────────────────
    FrameAnimation {
        id: frameLoop
        running: root.images.length > 0 && root.loopWidth > 0
        property int frameCount: 0
        onTriggered: {
            root.scrollOffset = (root.scrollOffset + root.speed * frameTime) % root.loopWidth
            frameCount++
        }
    }

    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: {
            fpsLabel.text = "FPS: " + frameLoop.frameCount
            frameLoop.frameCount = 0
        }
    }

    // ── River rows ────────────────────────────────────────────────────────
    Item {
        anchors.fill: parent
        clip: true

        // Empty-state overlay
        Text {
            anchors.centerIn: parent
            visible: root.images.length === 0
            text: photoLibrary.statusMessage !== ""
                  ? photoLibrary.statusMessage
                  : "Select one or more tags to start the river view."
            color: "#888888"
            font.pixelSize: 18
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            width: parent.width * 0.6
        }

        Repeater {
            model: root.numRows

            delegate: Item {
                required property int index

                y: index * root.slotHeight
                width: root.width
                height: root.slotHeight
                clip: true

                Row {
                    height: parent.height
                    x: -(root.scrollOffset + index * root.staggerPx)

                    Repeater {
                        model: root.stripItems

                        Image {
                            required property int index

                            readonly property int imgIdx:
                                root.images.length > 0 ? index % root.images.length : 0

                            height: root.slotHeight
                            width:  root.images.length > 0
                                    ? root.slotHeight * root.aspectRatios[imgIdx]
                                    : root.slotHeight

                            source: root.images.length > 0 ? root.images[imgIdx] : ""

                            // Full image, no cropping.
                            // Width is already set to the correct aspect-ratio value,
                            // so PreserveAspectFit == exact fit with no bars.
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            asynchronous: true
                            cache: true
                        }
                    }
                }
            }
        }
    }

    // ── Controls overlay ─────────────────────────────────────────────────
    Rectangle {
        id: controlBar
        anchors.bottom: parent.bottom
        width: parent.width
        height: 80
        color: Qt.rgba(0, 0, 0, 0.65)

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 24
            spacing: 32

            // ── Tag selector ─────────────────────────────────────────────
            Column {
                spacing: 2
                Text {
                    text: photoLibrary.selectedTags.length === 0
                          ? "Tags: none"
                          : "Tags: " + photoLibrary.selectedTags.length + " selected"
                    color: "white"
                    font.pixelSize: 12
                }
                Button {
                    text: "Choose tags \u25be"
                    onClicked: tagPopup.visible ? tagPopup.close() : tagPopup.open()
                }
            }

            // ── Speed ─────────────────────────────────────────────────────
            Column {
                spacing: 2
                Text {
                    text: "Speed: " + root.speed.toFixed(0) + " px/s"
                    color: "white"
                    font.pixelSize: 12
                }
                Slider {
                    width: 200
                    from: 30; to: 800
                    value: root.speed
                    onMoved: root.speed = value
                }
            }

            // ── Row count ─────────────────────────────────────────────────
            Column {
                spacing: 2
                Text {
                    text: "Rows: " + root.numRows
                    color: "white"
                    font.pixelSize: 12
                }
                Slider {
                    width: 150
                    from: 1; to: 10; stepSize: 1
                    value: root.numRows
                    onMoved: root.numRows = Math.round(value)
                }
            }
        }

        // FPS indicator
        Text {
            id: fpsLabel
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 24
            text: "FPS: --"
            color: "#00ff66"
            font.pixelSize: 18
            font.bold: true
            font.family: "Courier New"
        }
    }

    // ── Tag picker popup ─────────────────────────────────────────────────
    Popup {
        id: tagPopup
        // Position just above the control bar
        x: 24
        y: root.height - controlBar.height - implicitHeight - 8
        width: 320
        height: Math.min(480, root.height - controlBar.height - 32)
        padding: 0
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        background: Rectangle {
            color: Qt.rgba(0.1, 0.1, 0.1, 0.95)
            border.color: "#444"
            radius: 4
        }

        // Search field + list
        Column {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            TextField {
                id: tagSearch
                width: parent.width
                placeholderText: "Filter tags…"
                color: "white"
                background: Rectangle { color: "#222"; radius: 3 }
            }

            ListView {
                id: tagList
                width: parent.width
                height: tagPopup.height - tagSearch.height - 22
                clip: true

                model: photoLibrary.allTags.filter(
                    function(t) {
                        return tagSearch.text.length === 0
                            || t.toLowerCase().indexOf(tagSearch.text.toLowerCase()) >= 0
                    }
                )

                ScrollBar.vertical: ScrollBar {}

                delegate: CheckDelegate {
                    required property string modelData
                    width: tagList.width
                    text: modelData
                    checked: photoLibrary.selectedTags.indexOf(modelData) >= 0
                    contentItem: Text {
                        leftPadding: 8
                        text: parent.text
                        color: "white"
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.hovered ? "#333" : "transparent"
                    }
                    onToggled: {
                        var sel = photoLibrary.selectedTags.slice()
                        var idx = sel.indexOf(modelData)
                        if (idx >= 0) sel.splice(idx, 1)
                        else sel.push(modelData)
                        photoLibrary.selectedTags = sel
                    }
                }
            }
        }
    }

    // ── Keyboard: F/F11 fullscreen, Escape quit ───────────────────────────
    Item {
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
    }
}
