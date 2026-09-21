import QtQuick 1.1

// Silica-like metrics for the Android build. Values are the Sailfish defaults
// at a 540 px reference width, scaled to the device, so the shared table
// layout keeps the proportions it has on Sailfish OS.
QtObject {
    // Qt.application.screens is QtQuick 2. Both Harmattan devices this runs
    // on are 480x854, so the reference width is simply that.
    property real screenWidth: 480
    property real screenHeight: 854
    property real ratio: screenWidth / 540

    property real paddingSmall: 6 * ratio
    property real paddingMedium: 12 * ratio
    property real paddingLarge: 24 * ratio
    property real horizontalPageMargin: 24 * ratio

    property real itemSizeExtraSmall: 70 * ratio
    property real itemSizeSmall: 80 * ratio
    property real itemSizeMedium: 100 * ratio
    property real itemSizeLarge: 110 * ratio
    property real itemSizeExtraLarge: 135 * ratio
    property real itemSizeHuge: 180 * ratio

    property real fontSizeTiny: 16 * ratio
    property real fontSizeExtraSmall: 20 * ratio
    property real fontSizeSmall: 24 * ratio
    property real fontSizeMedium: 28 * ratio
    property real fontSizeLarge: 36 * ratio
    property real fontSizeExtraLarge: 44 * ratio
    property real fontSizeHuge: 60 * ratio

    property color primaryColor: "#ffffff"
    property color secondaryColor: "#b3ffffff"
    property color highlightColor: "#ffd27a"
    property color secondaryHighlightColor: "#c9ad6a"
    property color tableColor: "#0b5d36"
    property color barColor: "#084a2b"
    property color errorColor: "#ff4d4d"
}
