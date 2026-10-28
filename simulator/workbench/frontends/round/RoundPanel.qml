// 面贴使用 640 × 640 坐标，等比例缩放；按键和状态来自 APL。
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Item {
    id: root
    implicitWidth: 640
    implicitHeight: 640
    readonly property bool showAll: platform.supplied && platform.powerupShow
    readonly property bool displayOn: platform.supplied && platform.digitBrightness > 0

    function iconOpacity(index) {
        return platform.supplied && platform.iconLamps.length > index
                ? 0.14 + 0.86 * platform.iconLamps[index] / 255 : 0.14
    }

    // 矢量描线，避免 emoji 和系统字体导致图标外观不一致。
    component Icon: Canvas {
        property string kind: "power"
        property color strokeColor: "#e9edef"
        width: 56; height: 56
        onKindChanged: requestPaint()
        onStrokeColorChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"); c.reset()
            c.scale(width / 64, height / 64)
            c.strokeStyle = strokeColor; c.fillStyle = strokeColor
            c.lineWidth = 2.8; c.lineCap = "round"; c.lineJoin = "round"
            function line(a,b,d,e) { c.beginPath(); c.moveTo(a,b); c.lineTo(d,e); c.stroke() }
            function circle(x,y,r) { c.beginPath(); c.arc(x,y,r,0,Math.PI*2); c.stroke() }
            function box(x,y,w,h,r) {
                c.beginPath(); c.moveTo(x+r,y); c.lineTo(x+w-r,y)
                c.quadraticCurveTo(x+w,y,x+w,y+r); c.lineTo(x+w,y+h-r)
                c.quadraticCurveTo(x+w,y+h,x+w-r,y+h); c.lineTo(x+r,y+h)
                c.quadraticCurveTo(x,y+h,x,y+h-r); c.lineTo(x,y+r)
                c.quadraticCurveTo(x,y,x+r,y); c.stroke()
            }
            switch (kind) {
            case "purify":
                c.beginPath(); c.moveTo(18,17); c.lineTo(18,12)
                c.quadraticCurveTo(18,5,25,5); c.lineTo(48,5)
                c.quadraticCurveTo(55,5,55,12); c.lineTo(55,38)
                c.quadraticCurveTo(55,45,48,45); c.stroke(); box(7,18,39,39,9); break
            case "humidity":
                c.beginPath(); c.moveTo(32,4)
                c.bezierCurveTo(22,15,11,29,11,39); c.bezierCurveTo(11,66,53,66,53,39)
                c.bezierCurveTo(53,29,42,15,32,4); c.closePath(); c.stroke()
                c.beginPath(); c.arc(32,39,12,0.1,1.5); c.stroke(); break
            case "timer": circle(32,32,27); line(32,6,32,32); line(32,32,58,32); break
            case "light":
                circle(32,32,11)
                for (let i=0;i<8;i++) {
                    const a=i*Math.PI/4
                    line(32+Math.cos(a)*20,32+Math.sin(a)*20,32+Math.cos(a)*28,32+Math.sin(a)*28)
                }
                break
            case "fan": box(24,6,16,52,8); box(6,24,52,16,8); break
            case "power":
                c.beginPath(); c.arc(32,35,25,-1.02,Math.PI+1.02); c.stroke(); line(32,3,32,31); break
            case "lock":
                box(16,31,32,24,4); c.beginPath(); c.moveTo(23,31); c.lineTo(23,21)
                c.arc(32,21,9,Math.PI,0); c.lineTo(41,31); c.stroke(); break
            case "wifi":
                for (let r=11;r<=29;r+=9) { c.beginPath(); c.arc(32,48,r,-2.3,-0.84); c.stroke() }
                c.beginPath(); c.arc(32,48,2.5,0,Math.PI*2); c.fill(); break
            case "filter":
            case "water":
                c.beginPath(); c.ellipse(28,15,16,6,0,0,Math.PI*2); c.stroke()
                line(12,15,12,46); line(44,15,44,37)
                c.beginPath(); c.ellipse(28,46,16,6,0,0,Math.PI); c.stroke()
                if (kind==="filter") {
                    line(21,24,21,45); line(30,24,30,45); circle(46,47,10)
                    line(41,47,45,51); line(45,51,52,43)
                } else {
                    for (let y=31;y<=47;y+=8) { c.beginPath(); c.moveTo(34,y); c.quadraticCurveTo(44,y+5,56,y); c.stroke() }
                }
                break
            case "anion": circle(24,28,15); line(15,28,33,28); circle(49,43,7); break
            case "droplet":
                c.beginPath(); c.moveTo(32,5); c.bezierCurveTo(10,27,8,43,19,53)
                c.bezierCurveTo(48,70,66,39,32,5); c.stroke()
                c.font="26px sans-serif"; c.fillText("%",20,45); break
            default:
                const n=kind==="wave3"?3:kind==="wave2"?2:1
                for (let i=0;i<n;i++) {
                    const y=32+(i-(n-1)/2)*15
                    c.beginPath(); c.moveTo(10,y); c.bezierCurveTo(24,y-12,40,y+12,54,y); c.stroke()
                }
            }
        }
    }

    component Digit: Canvas {
        property string glyph: " "
        property int brightness: platform.supplied ? platform.digitBrightness : 0
        width: 64; height: 108
        onGlyphChanged: requestPaint()
        onBrightnessChanged: requestPaint()
        onPaint: {
            const c=getContext("2d"); c.reset()
            const maps={"0":63,"1":6,"2":91,"3":79,"4":102,"5":109,"6":125,"7":7,"8":127,"9":111,"-":64,"O":63,"F":113,"C":57,"L":56}
            const mask=maps[glyph]||0
            const segments=[
                [[10,2],[53,2],[58,6],[52,10],[12,10],[6,6]],
                [[58,10],[62,15],[62,45],[58,50],[54,45],[54,16]],
                [[58,57],[62,62],[62,93],[58,98],[54,92],[54,62]],
                [[12,98],[52,98],[58,102],[53,106],[10,106],[6,102]],
                [[6,57],[10,62],[10,92],[6,98],[2,93],[2,62]],
                [[6,10],[10,16],[10,45],[6,50],[2,45],[2,15]],
                [[12,50],[52,50],[58,54],[52,58],[12,58],[6,54]]
            ]
            for (let i=0;i<7;i++) {
                const lit=(mask&(1<<i)) && brightness>0
                c.globalAlpha=lit?brightness/255:1
                c.fillStyle=lit?"#f5f8fa":"#101315"
                const p=segments[i]; c.beginPath(); c.moveTo(p[0][0],p[0][1])
                for (let j=1;j<p.length;j++) c.lineTo(p[j][0],p[j][1])
                c.closePath(); c.fill()
            }
        }
    }

    component PanelKey: Item {
        id: key
        required property int keyId
        objectName: "panelKey" + keyId
        required property string iconName
        property string label: ""
        property string description: ""
        readonly property int lamp: platform.keyLamps.length>keyId ? platform.keyLamps[keyId] : 0
        readonly property bool held: (platform.keyMask & (1<<keyId))!==0
        property bool keyboardHeld: false
        width: 100; height: 94
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: description
        Accessible.onPressAction: { platform.keyDown(keyId); platform.keyUp(keyId) }
        Rectangle {
            anchors.fill: parent; radius: 20
            color: key.held ? "#142327" : mouse.containsMouse ? "#0b1215" : "transparent"
            border.width: key.activeFocus || key.held ? 1 : 0
            border.color: "#46cbd8"
        }
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter; y: 8; kind: key.iconName
            opacity: platform.supplied ? 0.5+0.5*key.lamp/255 : 0.25
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; y: 68; text: key.label
            color: "#e9edef"; opacity: platform.supplied ? 0.8 : 0.25
            font.family: "Segoe UI"; font.pixelSize: 14; font.italic: true
        }
        MouseArea {
            id: mouse
            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onPressed: { key.forceActiveFocus(Qt.MouseFocusReason); platform.keyDown(key.keyId) }
            onReleased: platform.keyUp(key.keyId)
            onCanceled: platform.keyUp(key.keyId)
        }
        ToolTip.visible: mouse.containsMouse && !held
        ToolTip.delay: 700
        ToolTip.text: description
        Keys.onPressed: event => {
            if (event.key===Qt.Key_Space || event.key===Qt.Key_Return) {
                if (!event.isAutoRepeat && !keyboardHeld) { keyboardHeld=true; platform.keyDown(keyId) }
                event.accepted=true
            }
        }
        Keys.onReleased: event => {
            if (event.key===Qt.Key_Space || event.key===Qt.Key_Return) {
                if (!event.isAutoRepeat) { keyboardHeld=false; platform.keyUp(keyId) }
                event.accepted=true
            }
        }
        onActiveFocusChanged: {
            if (!activeFocus && keyboardHeld) { keyboardHeld=false; platform.keyUp(keyId) }
        }
    }

    Item {
        id: face
        objectName: "panelFace"
        width: 640; height: 640
        anchors.centerIn: parent
        scale: Math.max(0,Math.min(root.width,root.height)-20)/640
        Rectangle { anchors.fill: parent; radius: 320; color: "#020303"; border.color: "#353b3d"; border.width: 1 }
        PanelKey { x: 270; y: 14; keyId: 0; iconName: "purify"; label: "Wifi(3s)"; description: "净化开关；长按 3 秒配网" }
        PanelKey { x: 44; y: 141; keyId: 1; iconName: "humidity"; label: "Humidity"; description: "加湿开关；一体机长按 3 秒设置恒湿" }
        PanelKey { x: 496; y: 141; keyId: 2; iconName: "timer"; label: "Lock(3s)"; description: "短按定时；长按 3 秒切换童锁" }
        PanelKey { x: 44; y: 404; keyId: 3; iconName: "light"; label: "Light"; description: "面板灯光开关；熄屏后首次触摸唤醒；长按 3 秒睡眠" }
        PanelKey { x: 496; y: 404; keyId: 4; iconName: "fan"; label: "Turbo(3s)"; description: "短按切换风速；长按 3 秒进入 Turbo" }
        PanelKey { x: 270; y: 538; keyId: 5; iconName: "power"; description: "电源开关；风干时短按重新开机，长按 3 秒结束风干；童锁开启时锁定" }
        Grid {
            x: 232; y: 193; columns: 15; rowSpacing: 7; columnSpacing: 6
            Repeater {
                model: 30
                Rectangle {
                    width: 6; height: 6; radius: 3
                    color: root.showAll && platform.ambientRed+platform.ambientGreen+platform.ambientBlue===0
                           ? "#00d5de" : Qt.rgba(platform.ambientRed/255,platform.ambientGreen/255,platform.ambientBlue/255,1)
                    opacity: platform.supplied && platform.ambientBrightness>0 ? platform.ambientBrightness/255 : 0
                }
            }
        }
        Row {
            x: 210; y: 240; spacing: 16
            Digit { glyph: platform.digits.charAt(0) }
            Digit { glyph: platform.digits.charAt(1) }
            Digit { glyph: platform.digits.charAt(2) }
        }
        Column {
            x: 443; y: 234; spacing: 3
            Repeater {
                model: ["H","%","PM2.5"]
                Text {
                    required property string modelData
                    required property int index
                    text: modelData; color: "#e9edef"
                    font.family: "Segoe UI"; font.pixelSize: index===2 ? 17 : 32
                    opacity: root.showAll || (root.displayOn && platform.digitUnit===index+1) ? 1 : 0.14
                }
            }
        }
        // 图标亮度完全来自 APL HMI。
        Icon { x: 151; y: 185; width: 34; height: 34; kind: "anion"; opacity: root.iconOpacity(4) }
        Icon { x: 151; y: 229; width: 34; height: 34; kind: "wifi"; opacity: root.iconOpacity(0) }
        Icon { x: 151; y: 273; width: 34; height: 34; kind: "filter"; strokeColor: platform.iconLamps.length>2 && platform.iconLamps[2]>0 ? "#ff6262" : "#e9edef"; opacity: Math.max(root.iconOpacity(1), root.iconOpacity(2)) }
        Icon { x: 151; y: 317; width: 34; height: 34; kind: "lock"; opacity: root.iconOpacity(3) }
        Rectangle {
            x: 446; y: 194; width: 27; height: 27; radius: 14
            color: "transparent"; border.color: "#e9edef"; border.width: 2
            opacity: root.iconOpacity(5)
            Text { anchors.centerIn: parent; text: "UV"; color: "#e9edef"; font.pixelSize: 12 }
        }
        Row {
            x: 149; y: 376; spacing: 29
            Repeater {
                model: ["Sleep","wave","wave2","wave3","Auto"]
                Item {
                    id: gearIndicator
                    required property string modelData
                    required property int index
                    width: 47; height: 38
                    opacity: root.iconOpacity(8 + index)
                    Text {
                        anchors.centerIn: parent; visible: gearIndicator.index===0 || gearIndicator.index===4
                        text: gearIndicator.modelData; color: "#e9edef"; font.family: "Segoe UI"; font.pixelSize: 16
                    }
                    Icon {
                        anchors.centerIn: parent; visible: gearIndicator.index>0 && gearIndicator.index<4
                        width: 36; height: 36; kind: gearIndicator.modelData
                    }
                }
            }
        }
        Text {
            x: 438; y: 406; text: "Turbo"; color: "#e9edef"; font.pixelSize: 12
            opacity: root.showAll || (root.displayOn && platform.powerOn && platform.gear===5) ? 1 : 0.12
        }
        Icon { x: 151; y: 422; width: 35; height: 35; kind: "droplet"; opacity: root.iconOpacity(6) }
        Row {
            x: 212; y: 438; spacing: 15
            Repeater {
                model: 4
                Canvas {
                    required property int index
                    readonly property int brightness: platform.supplied && platform.waterLamps.length>index ? platform.waterLamps[index] : 0
                    width: 42; height: 10
                    readonly property int waterColor: platform.waterColor
                    onWaterColorChanged: requestPaint()
                    onBrightnessChanged: requestPaint()
                    onPaint: {
                        const c=getContext("2d"); c.reset()
                        c.globalAlpha=brightness>0 ? brightness/255 : 1
                        c.fillStyle=brightness>0 ? (waterColor===1 ? "#ff9f43" : "#80e8ac") : "#15191b"
                        c.beginPath(); c.moveTo(6,1); c.lineTo(42,1)
                        c.lineTo(36,9); c.lineTo(0,9); c.closePath(); c.fill()
                    }
                }
            }
        }
        Icon { x: 449; y: 422; width: 35; height: 35; kind: "water"; opacity: root.iconOpacity(7) }
    }
}
