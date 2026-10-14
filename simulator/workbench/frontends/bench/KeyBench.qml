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
            text: "按键台"
            color: "#dbe7f5"
            font.family: "Segoe UI"
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
                    text: platform.powerOn ? "开机" : "关机"
                    color: "#f4f7fb"
                    font.pixelSize: 18
                }
                Text { text: platform.gearName; color: "#9ecbff"; font.pixelSize: 16 }
                Text {
                    text: platform.childLock ? "童锁开" : "童锁关"
                    color: "#f4f7fb"
                    font.pixelSize: 14
                }
                Text {
                    text: "亮度 " + platform.digitBrightness
                    color: "#9aa3ad"
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
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.preferredHeight: 92
                    radius: 8
                    color: (platform.keyMask & (1 << index)) !== 0 ? "#1d4e89" : "#1a1e25"
                    border.width: platform.keyLamps.length > index && platform.keyLamps[index] >= 255
                                  ? 3
                                  : (platform.keyLamps.length > index && platform.keyLamps[index] > 0 ? 2 : 1)
                    border.color: platform.keyLamps.length > index && platform.keyLamps[index] > 0
                                  ? "#f2f2f2"
                                  : "#343a44"
                    opacity: platform.keyLamps.length > index && platform.keyLamps[index] === 128 ? 0.72 : 1
                    Column {
                        anchors.centerIn: parent
                        spacing: 4
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData
                            color: "#f4f7fb"
                            font.family: "Segoe UI"
                            font.pixelSize: 16
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: platform.keyLamps.length > index ? platform.keyLamps[index] : 0
                            color: "#9fb4c8"
                            font.pixelSize: 12
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: platform.keyDown(index)
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
                    color: "#3aa0ff"
                    opacity: platform.waterLamps.length > index && platform.waterLamps[index] > 0
                              ? platform.waterLamps[index] / 255
                              : 0.15
                }
            }
        }
    }
}
