pragma ComponentBehavior: Bound
import QtQuick
import AD.Theme

// The notices, newest at the bottom, each fading after a few seconds.
Column {
    id: strip

    property int lifetimeMs: 4200
    property int maxVisible: 5

    spacing: 6
    width: 320

    function show(text, kind) {
        if (toasts.count >= maxVisible)
            toasts.remove(0)
        toasts.append({ message: text, level: kind || "info", stamp: ++strip.counter })
    }
    function dismiss(stamp) {
        for (let i = 0; i < toasts.count; ++i)
            if (toasts.get(i).stamp === stamp) { toasts.remove(i); return }
    }
    property int counter: 0

    ListModel { id: toasts }

    add: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 }
        NumberAnimation { property: "x"; from: 40; to: 0; duration: 220; easing.type: Easing.OutCubic }
    }
    move: Transition {
        NumberAnimation { properties: "y"; duration: 200; easing.type: Easing.OutCubic }
    }

    Repeater {
        model: toasts
        Toast {
            required property string message
            required property string level
            required property int stamp
            text: message
            kind: level
            Timer {
                interval: strip.lifetimeMs
                running: true
                onTriggered: strip.dismiss(parent.stamp)
            }
        }
    }
}
