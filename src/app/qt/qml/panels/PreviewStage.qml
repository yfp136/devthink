// ShowMaster Qt 桌面端 —— P1-1-C4 中央预监/输出区 PreviewStage
// =============================================================================
// §9.1 中央区（剩余空间）：PVW 预监 / PGM 输出 双画面 + 叠加信息（播放头/时间/场景名）。
//   本版 PGM = 本地窗口输出：画面来自 kernelBridge.frameReady(base64Jpeg)（引擎
//   capture_pgm_frame_jpeg 链路，无渲染上下文时空串→维持引导空态，不空白）。
// §9.2/9.3 Esc：退出监视器放大浮层并清除预监候选（关闭浮层语义，停播由 Main 负责）。
// 交互：
//   - 素材库『预监』(media.preview rsp) → PVW 显示候选目标（帧通道 P1-3 联调接入）
//   - 双击监视器 → 该监视器放大铺满中央区；再次双击或 Esc 还原
// 事件驱动：onFrame(b64) 刷新 PGM 帧；onStatus(o) 更新走带/状态叠加；
//   onEvent(op,env) 消费 evt.timeline.item_* 更新当前节目名行。
// =============================================================================

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../UiStyle.js" as UiStyle   // 共享样式/工具（.pragma library 引擎级单例）

Rectangle {
    id: root
    color: UiStyle.cBg

    // ---- 当前输出/节目状态（onStatus 2Hz 更新）----
    property string playState: "idle"     // timeline scheduler play_state
    property string mediaState: "idle"    // media engine state
    property int posMs: 0
    property int totalMs: 0
    property string pgmB64: ""            // 最近一帧（base64 jpeg，来自 frameReady）

    // ---- PVW 候选（media.preview rsp / 事件驱动）----
    property string pvwArmed: ""          // 目标显示名（"" = 未预监）
    property string nowLine: ""           // PGM 当前条目（evt.timeline.item_* 驱动）

    // ---- 监视器放大浮层（双击进入，Esc/再次双击退出）----
    property string expanded: ""          // "" | "pvw" | "pgm"

    // =========================================================================
    // 工具
    // =========================================================================
    function dataUrl(b64) {
        if (!b64) return "";
        if (b64.indexOf("data:") === 0) return b64;
        return "data:image/jpeg;base64," + b64;
    }
    // timeline scheduler 状态文案（与 playlistLabel 语义不同的补丁映射）
    function schedLabel(s) {
        switch (s) {
        case "idle":        return "空闲";
        case "loaded":      return "已装载";
        case "running":     return "运行中";
        case "waiting_go":  return "待触发";
        case "paused":      return "已暂停";
        case "ended":       return "已结束";
        }
        return s || "-";
    }
    function shortRef(u) {
        if (!u) return "";
        return (u.length > 10) ? u.slice(0, 10) : u;
    }
    function pad2(n) { return n < 10 ? "0" + n : "" + n; }
    function wallClock() {
        var d = new Date();
        return pad2(d.getHours()) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds());
    }

    // =========================================================================
    // 上行回调（Main 分发，缺失方法由 callPanel 静默跳过）
    // =========================================================================
    function onStatus(o) {
        if (!o) return;
        if (o.play_state  !== undefined) root.playState  = o.play_state;
        if (o.media_state !== undefined) root.mediaState = o.media_state;
        if (o.pos_ms      !== undefined) root.posMs      = o.pos_ms;
        if (o.total_ms    !== undefined) root.totalMs    = o.total_ms;
        // 场景/节目运行态落到状态 LED 与文案行
        if (o.playlist_index !== undefined && o.playlist_state
                && (o.playlist_state === "running" || o.playlist_state === "waiting_go"))
            root.nowLine = "节目单 #" + (o.playlist_index + 1) + " · " +
                           UiStyle.playlistLabel(o.playlist_state);
    }
    function onFrame(b64) {
        if (!b64) { root.pgmB64 = ""; return; }   // 引擎侧空帧不发；同帧去重在驱动侧完成
        root.pgmB64 = b64;
    }
    function onReply(op, env) {
        if (op !== "media.preview") return;
        var p = (env && env.params) || {};
        if (env && env.code !== 0) { root.pvwArmed = ""; return; }
        var name = p.name || p.media_id || p.path || "";
        root.pvwArmed = name ? "已载入：" + name : "已载入候选目标";
    }
    function onEvent(op, env) {
        var p = (env && env.params) || {};
        var ref = p.ref_uuid || "";
        var tk = (p.track_index !== undefined) ? ("T" + (p.track_index + 1)) : "";
        var where = (p.start_ms !== undefined) ? (" @" + UiStyle.fmtMs(p.start_ms)) : "";
        var reason = p.reason ? (" · " + p.reason) : "";
        if (op.indexOf("item_started") >= 0)
            root.nowLine = "▶ " + tk + (ref ? " " + shortRef(ref) : "") + where + reason;
        else if (op.indexOf("item_ended") >= 0)
            root.nowLine = "■ " + tk + (ref ? " " + shortRef(ref) : "") + " 已结束" + reason;
        else if (op.indexOf("item_aborted") >= 0)
            root.nowLine = "⏹ " + tk + (ref ? " " + shortRef(ref) : "") + " 中止" + reason;
    }
    function onEscape() {
        root.expanded = "";
        if (root.pvwArmed !== "") root.pvwArmed = "";
    }
    function toggleExpanded(which) {
        root.expanded = (root.expanded === which) ? "" : which;
    }

    // 叠加态计算
    function pgmLed() {
        if (root.playState === "running" || root.playState === "waiting_go") return UiStyle.cOk;
        if (root.playState === "paused") return UiStyle.cWarn;
        return UiStyle.cIdle;
    }
    function pvwLed() { return root.pvwArmed ? UiStyle.cAccent : UiStyle.cIdle; }

    // =========================================================================
    // 监视器通用组件（PVW/PGM 复用；标签/指示灯/时间/信息行可配置）
    // =========================================================================
    component StageMonitor: Rectangle {
        id: mon
        property string monLabel: ""
        property color led: UiStyle.cIdle
        property string b64: ""
        property string hint: ""          // 无帧时的中央引导（§9.4 不空白）
        property string infoLine: ""      // 左下叠加信息行
        property string timeText: ""      // 右上时间码/时钟叠加
        property bool big: false          // 当前为放大浮层态
        property string emptyBg: "#0a0d13"

        signal wantedExpand()

        radius: 4
        color: emptyBg
        border.color: big ? UiStyle.cAccent : UiStyle.cBorder
        border.width: big ? 2 : 1
        clip: true

        // 微弱取景网格（无帧时也有层次，不空白）
        Grid {
            anchors.fill: parent
            rows: 4; columns: 8
            Repeater {
                model: 32
                Rectangle {
                    color: "transparent"
                    border.color: "#ffffff06"
                    border.width: 1
                }
            }
        }

        Image {
            id: monImg
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            source: b64 ? root.dataUrl(b64) : ""
            smooth: true
            cache: false
        }

        // 无画面引导（§9.4：不允许空白面板）
        Column {
            id: hintBox
            visible: !b64
            anchors.centerIn: parent
            width: Math.min(parent.width - 60, 560)
            spacing: 8
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: monLabel
                color: UiStyle.cTextDim
                font.pixelSize: 18
                font.bold: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                text: hint
                color: "#5f6b7a"
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                lineHeight: 1.5
            }
        }

        // 左上：监视器名 + 状态灯
        Row {
            anchors { left: parent.left; leftMargin: 8; top: parent.top; topMargin: 8 }
            spacing: 5
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 7; height: 7; radius: 4
                color: mon.led
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: monLabel
                color: "#dfe5ec"
                font.pixelSize: 11
                font.bold: true
                style: Text.Outline
                styleColor: "#cc000000"
            }
        }

        // 右上：时间码/时钟叠加（PGM 用：墙钟 + 播放头）
        Text {
            anchors { right: parent.right; rightMargin: 8; top: parent.top; topMargin: 8 }
            visible: timeText !== ""
            text: timeText
            color: "#e2e8ef"
            font.pixelSize: 11
            font.family: "Menlo, Consolas, monospace"
            style: Text.Outline
            styleColor: "#cc000000"
        }

        // 左下：当前节目/状态行
        Text {
            anchors { left: parent.left; leftMargin: 8; bottom: parent.bottom; bottomMargin: 8 }
            visible: infoLine !== ""
            text: infoLine
            color: "#dfe5ec"
            font.pixelSize: 11
            style: Text.Outline
            styleColor: "#cc000000"
        }

        // 右下：放大浮层提示
        Text {
            anchors { right: parent.right; rightMargin: 8; bottom: parent.bottom; bottomMargin: 8 }
            visible: !mon.big
            text: "双击放大 · Esc 退出"
            color: "#4d5a68"
            font.pixelSize: 9
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onDoubleClicked: mon.wantedExpand()
        }
    }

    // =========================================================================
    // PVW（上，45%）：预监候选画面
    // =========================================================================
    StageMonitor {
        id: pvw
        anchors { top: parent.top; topMargin: 6; left: parent.left; leftMargin: 6; right: parent.right; rightMargin: 6 }
        visible: root.expanded !== "pgm"
        height: root.expanded === "pvw" ? (parent.height - 12) : (parent.height - 18) * 0.45
        big: root.expanded === "pvw"

        monLabel: "PVW 预监"
        led: root.pvwLed()
        b64: ""   // PVW 独立帧通道属 P1-3 联调（引擎当前仅推 PGM 帧）
        hint: root.pvwArmed === ""
                ? "未选择预监画面。在素材库选中素材后点击『预监』按钮载入候选，或双击本监视器放大。"
                : "候选已就绪：" + root.pvwArmed + "\n独立 PVW 帧通道随 PGM 帧链路（P1-3）联调后显示画面。"
        infoLine: root.pvwArmed === "" ? "" : ("● " + root.pvwArmed)

        onWantedExpand: root.toggleExpanded("pvw")
    }

    // =========================================================================
    // PGM（下，55%）：本地窗口节目输出（frameReady 帧 + 叠加信息）
    // =========================================================================
    StageMonitor {
        id: pgm
        anchors { bottom: parent.bottom; bottomMargin: 6; left: parent.left; leftMargin: 6; right: parent.right; rightMargin: 6 }
        visible: root.expanded !== "pvw"
        height: root.expanded === "pgm" ? (parent.height - 12) : (parent.height - 18) * 0.55
        big: root.expanded === "pgm"

        monLabel: "PGM 节目输出"
        led: root.pgmLed()
        b64: root.pgmB64
        hint: "节目输出待命。播放时间线或节目单后显示画面：\nSpace 播放/暂停 · Shift+Space 停止回零 · G 节目 GO（快捷键 §9.3）"
        timeText: root.clockText + "  ·  " + UiStyle.fmtMs(root.posMs) + " / " + UiStyle.fmtMs(root.totalMs)
        infoLine: (root.nowLine !== "" ? root.nowLine + "    " : "")
                  + "媒体:" + UiStyle.playLabel(root.mediaState)
                  + "  主状态:" + root.schedLabel(root.playState)

        onWantedExpand: root.toggleExpanded("pgm")
    }

    // 墙钟叠加信息（§9.1）：本地 500ms 定时刷新，播放头/时长随 status 2Hz 联动
    property string clockText: wallClock()
    Timer {
        interval: 500
        repeat: true
        running: true
        triggeredOnStart: false
        onTriggered: root.clockText = root.wallClock()
    }
}
