// ShowMaster Qt 桌面端 —— P1-1-C2 状态栏 StatusBar（24px）
// =============================================================================
// §9.1 底部状态条：左侧消息（notify 缓冲/自动消退），右侧引擎状态灯与计数。
// 状态灯仅消费 statusChanged 心跳（2Hz）已知键：play_state / media_state /
// playlist_state / scene_count / media_count —— 不猜测 engines[] 内部结构。
// notify(text, level) 由 Main.qml 分发总线提示：level 0=信息 1=警告 2=错误。
// =============================================================================

import QtQuick
import QtQuick.Layouts
import "../UiStyle.js" as UiStyle   // 共享样式/工具（.pragma library 引擎级单例）

Rectangle {
    id: root
    color: UiStyle.cPanel
    border.color: UiStyle.cBorder
    border.width: 1

    // ---- 内联组件（须先声明后使用）----
    component StateDot: Rectangle {
        id: dot
        property string labelText: ""
        property color colorOf: UiStyle.cIdle
        Layout.preferredHeight: 14
        Layout.preferredWidth: Math.max(56, dot.labelText.length * 8 + 16)
        radius: 7
        color: Qt.rgba(dot.colorOf.r, dot.colorOf.g, dot.colorOf.b, 0.15)
        border.color: Qt.rgba(dot.colorOf.r, dot.colorOf.g, dot.colorOf.b, 0.55)
        RowLayout {
            anchors.centerIn: parent
            spacing: 4
            Rectangle {
                width: 6; height: 6; radius: 3
                color: dot.colorOf
            }
            Text {
                text: dot.labelText
                color: dot.colorOf
                font.pixelSize: 10
            }
        }
    }

    property string playState: "stopped"
    property string mediaState: "idle"
    property string playlistState: "idle"
    property int sceneCount: 0
    property int mediaCount: 0

    property string message: ""
    property int messageLevel: 0        // 0 信息 / 1 警告 / 2 错误
    readonly property string defaultMessage: "就绪"

    // ---- 供 Main.qml 分发的面板回调 ----
    function onStatus(o) {
        if (!o) return;
        playState = o.play_state || playState;
        mediaState = o.media_state || mediaState;
        playlistState = o.playlist_state || playlistState;
        sceneCount = o.scene_count || 0;
        mediaCount = o.media_count || 0;
    }

    function notify(text, level) {
        message = text || defaultMessage;
        messageLevel = (level === undefined) ? 0 : level;
        msgTimer.restart();
    }

    function msgColor(lv) {
        if (lv === 1) return UiStyle.cWarn;
        if (lv === 2) return UiStyle.cErr;
        return UiStyle.cTextDim;
    }

    function stateColor(s) {   // 时间线播放态
        if (s === "playing") return UiStyle.cOk;
        if (s === "paused")  return UiStyle.cWarn;
        if (s === "error")   return UiStyle.cErr;
        return UiStyle.cIdle;  // stopped/idle
    }
    function mediaColor(s) {
        if (s === "playing") return UiStyle.cOk;
        if (s === "loading") return UiStyle.cAccent;
        if (s === "paused")  return UiStyle.cWarn;
        if (s === "error")   return UiStyle.cErr;
        return UiStyle.cIdle;
    }
    function playlistColor(s) {
        if (s === "running")    return UiStyle.cOk;
        if (s === "waiting_go") return UiStyle.cWarn;
        if (s === "paused")     return UiStyle.cWarn;
        if (s === "ended")      return UiStyle.cTextDim;
        return UiStyle.cIdle;
    }
    function mediaStateLabel(s) {
        // 媒体引擎状态压缩为状态栏可容纳的短标签
        if (s === "playing") return "媒体播放";
        if (s === "loading") return "媒体载入";
        if (s === "paused")  return "媒体暂停";
        if (s === "error")   return "媒体错误";
        return "媒体空闲";
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 10

        // ---- 左：消息 ----
        Text {
            id: msgText
            Layout.fillWidth: true
            text: root.message !== "" ? root.message : root.defaultMessage
            color: root.message !== "" ? root.msgColor(root.messageLevel) : UiStyle.cTextDim
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        // ---- 右：语义状态灯 ----
        StateDot {
            labelText: UiStyle.playLabel(root.playState)
            colorOf: root.stateColor(root.playState)
        }
        StateDot {
            labelText: root.mediaStateLabel(root.mediaState)
            colorOf: root.mediaColor(root.mediaState)
        }
        StateDot {
            labelText: "列表 " + UiStyle.playlistLabel(root.playlistState)
            colorOf: root.playlistColor(root.playlistState)
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 12; color: UiStyle.cBorder }
        Text {
            text: "素材 " + root.mediaCount + "  场景 " + root.sceneCount
            color: UiStyle.cTextDim
            font.pixelSize: 11
        }
    }

    Timer {
        id: msgTimer
        interval: 6000
        repeat: false
        onTriggered: root.message = ""
    }
}
