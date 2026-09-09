// ShowMaster Qt 桌面端 —— P1-1-C2 顶栏 TopBar（48px）
// =============================================================================
// §9.1 顶部工具条：Logo/模式 | 走带控制（停止/播放暂停单键） | 播放列表 GO
// §9.2 状态灯 → 顶栏右侧：播放列表执行态芯片 + 时间码（心跳 2Hz 驱动 onStatus）
// 依赖 kernelBridge 全局上下文属性（Task D 注入）；命令全部走 KernelHost 直连
// 快捷方法（transportPlay/Pause/Resume/Stop、playlistGo），无总线往返。
// =============================================================================

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../UiStyle.js" as UiStyle   // 共享样式/工具（.pragma library 引擎级单例）

Rectangle {
    id: root
    color: UiStyle.cPanel
    border.color: UiStyle.cBorder
    border.width: 1

    // ---- 内联组件（须先声明后使用）----
    component TransportBtn: Button {
        id: tbtn
        property string glyph: ""
        property string tip: ""
        property bool accent: false
        Layout.preferredWidth: 36
        Layout.preferredHeight: 26
        enabled: !!kernelBridge
        hoverEnabled: true
        contentItem: Text {
            text: tbtn.glyph
            color: tbtn.enabled ? (tbtn.accent ? "#dbe9ff" : UiStyle.cText) : UiStyle.cTextDim
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 4
            color: tbtn.pressed ? UiStyle.cBorder
                 : (tbtn.hovered ? UiStyle.cPanelAlt : "transparent")
            border.color: tbtn.hovered ? UiStyle.cBorder : "transparent"
        }
        ToolTip.visible: hovered
        ToolTip.text: tip
        ToolTip.delay: 600
    }

    component PlaylistChip: Rectangle {
        id: chip
        property string stateText: "idle"
        property int stateIndex: -1
        Layout.preferredHeight: 20
        radius: 10
        color: chip.colorFor(stateText)
        function colorFor(s) {
            if (s === "running") return "#163d24";
            if (s === "waiting_go") return "#5a470f";
            if (s === "paused") return "#3d3316";
            if (s === "loaded") return "#16302f";
            return "#232933";   // idle/ended
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 5
            Rectangle {
                width: 6; height: 6; radius: 3
                color: chip.textColorFor(chip.stateText)
            }
            Text {
                text: UiStyle.playlistLabel(chip.stateText)
                    + (chip.stateIndex >= 0 ? " #" + (chip.stateIndex + 1) : "")
                color: chip.textColorFor(chip.stateText)
                font.pixelSize: 11
            }
        }
        function textColorFor(s) {
            if (s === "running") return UiStyle.cOk;
            if (s === "waiting_go") return "#e3b341";
            if (s === "paused") return UiStyle.cWarn;
            return UiStyle.cTextDim;
        }
    }

    // ---- 来自 statusChanged 心跳快照（Main.qml 分发）----
    property string playState: "stopped"      // stopped|playing|paused
    property string playlistState: "idle"     // idle|loaded|running|waiting_go|paused|ended
    property int playlistIndex: -1            // 播放列表当前条目下标
    property int posMs: 0
    property int totalMs: 0

    // ---- 供 Main.qml 分发的面板回调 ----
    function onStatus(o) {
        if (!o) return;
        playState = o.play_state || playState;
        playlistState = o.playlist_state || playlistState;
        playlistIndex = (o.playlist_index === undefined || o.playlist_index === null)
                        ? -1 : o.playlist_index;
        posMs = o.pos_ms || 0;
        totalMs = o.total_ms || 0;
    }

    // ---- 走带动作（空 mediaId = 时间线播放，语义对齐 kernel.transport_play）----
    function bridgePlay()    { if (kernelBridge) kernelBridge.transportPlay(""); }
    function bridgePause()   { if (kernelBridge) kernelBridge.transportPause(); }
    function bridgeResume()  { if (kernelBridge) kernelBridge.transportResume(); }
    function bridgeStop()    { if (kernelBridge) kernelBridge.transportStop(); }
    function bridgeGo()      { if (kernelBridge) kernelBridge.playlistGo(); }

    function togglePlayPause() {
        if (!kernelBridge) return;
        if (playState === "playing") bridgePause();
        else if (playState === "paused") bridgeResume();
        else bridgePlay();
    }

    readonly property bool isPlaying: playState === "playing"
    readonly property bool isPaused:  playState === "paused"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        // ---- 左：品牌 + 模式 + 工程 ----
        Text {
            text: "ShowMaster"
            color: UiStyle.cText
            font.pixelSize: 15
            font.bold: true
        }
        Rectangle {
            Layout.preferredWidth: 64
            Layout.preferredHeight: 18
            radius: 9
            color: "#1c3a5e"
            border.color: UiStyle.cAccent
            Text {
                anchors.centerIn: parent
                text: "headless"
                color: "#7ab7ff"
                font.pixelSize: 10
            }
        }
        Rectangle { Layout.preferredHeight: 18; Layout.preferredWidth: 1; color: UiStyle.cBorder }
        Text {
            text: "未命名工程"
            color: UiStyle.cTextDim
            font.pixelSize: 12
            elide: Text.ElideRight
            Layout.maximumWidth: 160
        }

        Item { Layout.fillWidth: true }

        // ---- 中：走带控制 ----
        TransportBtn {
            glyph: "\u25A0"          // ■
            tip: "停止 (Shift+Space)"
            onClicked: root.bridgeStop()
        }
        TransportBtn {
            glyph: root.isPlaying ? "\u2590\u2590" : "\u25B6"   // ▐▐ / ▶
            tip: root.isPlaying ? "暂停 (Space)"
               : (root.isPaused ? "继续 (Space)" : "播放 (Space)")
            accent: true
            onClicked: root.togglePlayPause()
        }

        Rectangle { Layout.preferredHeight: 18; Layout.preferredWidth: 1; color: UiStyle.cBorder }

        // ---- 右：GO + 播放列表态 + 时间码 ----
        Button {
            text: "GO"
            Layout.preferredWidth: 46
            Layout.preferredHeight: 24
            enabled: !!kernelBridge
            onClicked: root.bridgeGo()
            contentItem: Text {
                text: "GO"
                color: parent.enabled ? "#ffffff" : UiStyle.cTextDim
                font.pixelSize: 12
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 4
                color: parent.down ? "#1d63d0" : "#1f6feb"
                border.color: "#58a6ff"
                opacity: 1
                // waiting_go 时呼吸提示：常亮并脉动
                SequentialAnimation on opacity {
                    running: root.playlistState === "waiting_go"
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.45; duration: 500 }
                    NumberAnimation { to: 1.0; duration: 500 }
                }
            }
        }

        PlaylistChip {
            stateText: root.playlistState
            stateIndex: root.playlistIndex
        }

        Text {
            text: UiStyle.fmtMs(root.posMs) + " / " + UiStyle.fmtMs(root.totalMs)
            color: root.isPlaying ? UiStyle.cText : UiStyle.cTextDim
            font.pixelSize: 12
            font.family: "Menlo, Monaco, monospace"
        }
    }
}
