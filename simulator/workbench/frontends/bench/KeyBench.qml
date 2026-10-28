// 前端只画这一块面贴，并通过 platform 跟仿真台说话。
// 读 keyLamps、waterLamps、digits、digitBrightness、digitUnit、powerOn、gearName、childLock、keyMask。
// 写 keyDown(id)、keyUp(id)。0 净化，1 加湿，2 定时，3 灯光，4 风速，5 电源。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Item {
    implicitWidth: 760
    implicitHeight: 520

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        Text {
            text: "按键调试"
            color: "#292c35"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 22
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Rectangle {
                Layout.preferredWidth: 240
                Layout.preferredHeight: 120
                radius: 8
                color: "#070b10"
                Text {
                    anchors.centerIn: parent
                    text: platform.digits
                    color: "#e9fff4"
                    font.family: "Consolas"
                    font.pixelSize: 64
                    opacity: platform.digitBrightness > 0 ? platform.digitBrightness / 255 : 0.16
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: platform.runStateText + (platform.runState === 2 ? " · " + platform.drySeconds + " 秒" : "")
                    color: "#30343e"
                    font.pixelSize: 18
                }
                Text {
                    text: ["", "H", "%", "PM2.5"][platform.digitUnit] || ""
                    color: "#007aff"; font.pixelSize: 16
                    opacity: platform.digitBrightness > 0 ? platform.digitBrightness / 255 : 0.16
                }
                Text { text: platform.gearName; color: "#007aff"; font.pixelSize: 16 }
                Text {
                    text: platform.childLock ? "童锁开" : "童锁关"
                    color: "#30343e"
                    font.pixelSize: 14
                }
                Text {
                    text: "亮度 " + platform.digitBrightness
                    color: "#7a7e89"
                    font.pixelSize: 13
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            rowSpacing: 10
            columnSpacing: 10
            Repeater {
                model: ["Purify", "Humidity", "Timer", "Light", "Speed", "Power"]
                Rectangle {
                    id: key
                    objectName: "benchKey" + index
                    activeFocusOnTab: true
                    property bool keyboardHeld: false
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData
                    Accessible.onPressAction: { platform.keyDown(index); platform.keyUp(index) }
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.preferredHeight: 92
                    radius: 8
                    color: (platform.keyMask & (1 << index)) !== 0 ? "#e0eeff" : "#f5f6f9"
                    border.width: activeFocus ? 2 : platform.keyLamps.length > index && platform.keyLamps[index] >= 255
                                  ? 3
                                  : (platform.keyLamps.length > index && platform.keyLamps[index] > 0 ? 2 : 1)
                    border.color: activeFocus ? "#007aff" : platform.keyLamps.length > index && platform.keyLamps[index] > 0
                                  ? "#7eb8ff"
                                  : "#e2e4eb"
                    opacity: platform.keyLamps.length > index && platform.keyLamps[index] === 128 ? 0.72 : 1
                    Column {
                        anchors.centerIn: parent
                        spacing: 4
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData
                            color: "#30343e"
                            font.family: "Segoe UI"
                            font.pixelSize: 16
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: platform.keyLamps.length > index ? platform.keyLamps[index] : 0
                            color: "#8a8f9b"
                            font.pixelSize: 12
                        }
                    }
                    Keys.onPressed: event => {
                        if (event.key===Qt.Key_Space || event.key===Qt.Key_Return) {
                            if (!event.isAutoRepeat && !keyboardHeld) { keyboardHeld=true; platform.keyDown(index) }
                            event.accepted=true
                        }
                    }
                    Keys.onReleased: event => {
                        if (event.key===Qt.Key_Space || event.key===Qt.Key_Return) {
                            if (!event.isAutoRepeat) { keyboardHeld=false; platform.keyUp(index) }
                            event.accepted=true
                        }
                    }
                    onActiveFocusChanged: {
                        if (!activeFocus && keyboardHeld) { keyboardHeld=false; platform.keyUp(index) }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: { key.forceActiveFocus(Qt.MouseFocusReason); platform.keyDown(index) }
                        onReleased: platform.keyUp(index)
                        onCanceled: platform.keyUp(index)
                    }
                }
            }
        }

        Row {
            spacing: 8
            Repeater {
                model: 4
                Rectangle {
                    required property int index
                    width: 52
                    height: 14
                    radius: 3
                    color: platform.waterColor===1 ? "#ff9f43" : "#80e8ac"
                    opacity: platform.waterLamps.length > index && platform.waterLamps[index] > 0
                              ? platform.waterLamps[index] / 255
                              : 0.15
                }
            }
        }
    }
}
