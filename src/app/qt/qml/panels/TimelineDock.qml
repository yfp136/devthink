// ShowMaster Qt 桌面端 —— P1-1-C6 底部时间线停靠面板 TimelineDock
// =============================================================================
// §9.1 底部 220px 时间线：轨头列 + 时间标尺 + 播放头 + 横向平移/缩放 + 事件驱动反馈。
//   本阶段聚焦“时间线工作台的表达与走带”：轨道结构、播放头定位、缩放与 Home/End
//   定位由本面板实现；条目块的拖拽编排属后续阶段（引擎已具备 timeline.* 编辑 op）。
//
// 运行时契约（数据源全部来自内核消息总线，本文件不持有内核）：
//   - 下行：kernelBridge.postCommand("engine.timeline", op, json)
//       transport.seek{pos_ms}     定位播放头（引擎调度器）
//       timeline.tracks            拉取轨道清单（回复含 tracks[]）
//   - 上行（Main 分发路由，回调名与 Main 保持一致，缺失方法 callPanel 静默跳过）：
//       onStatus(o)     状态快照（play_state/pos_ms/total_ms，Main.onStatusRaw→本面板）
//       onReply(op,env) timeline.tracks 等回复（Main.routeReply 按 timeline.* 前缀路由）
//       onEvent(op,env) evt.timeline.* 事件（Main.routeEvent 路由；item_* 播放事件
//                       同时补发 previewStage，本面板只做走带指示）
//       onZoomRequest(f)/onPlayheadHome()/onPlayheadEnd()   Main 快捷键信号钩子
//   - 本面板不空转：无内核（Task D 注入前预览态）时按引擎默认 8 轨呈现静态骨架，
//     有内核后以 timeline.tracks 回复为准刷新轨头。
// =============================================================================

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../UiStyle.js" as UiStyle   // 共享样式/工具（.pragma library 引擎级单例）

Rectangle {
    id: root
    color: UiStyle.cPanel
    border.color: UiStyle.cBorder
    border.width: 1
    clip: true

    // =========================================================================
    // 状态（onStatus/onReply/onEvent 上行刷新）
    // =========================================================================
    property string playState: "idle"          // scheduler play_state（stopped/playing/paused）
    property int posMs: 0                      // 播放头位置
    property int totalMs: 0                    // 时间线总时长（0=尚未装载）
    // 引擎默认 8 轨（timeline_scheduler 构造同序；收到 timeline.tracks 回复前先呈现骨架）
    property var defaultTracks: [
        { index: 0, type: "audio",   name: "主音频轨" },
        { index: 1, type: "video",   name: "主视频轨" },
        { index: 2, type: "scene",   name: "场景指令轨" },
        { index: 3, type: "command", name: "指令轨" },
        { index: 4, type: "vj",      name: "VJ 特效轨（Phase 2）" },
        { index: 5, type: "light",   name: "灯光轨（Phase 3）" },
        { index: 6, type: "pixel",   name: "像素灯带轨（Phase 3）" },
        { index: 7, type: "device",  name: "硬件中控轨（Phase 3）" }
    ]
    property var tracksModel: defaultTracks.slice()  // [{index,type,name}]（timeline.tracks 回复覆盖）
    property var running: ({})                 // track_index -> {ref,startMs}（item 事件驱动）
    property string lastEvt: ""                // 最近一条事件简报（footer 状态行）
    property bool engineSeen: false            // 是否收到过内核上行（区分预览态/联调态）

    // 布局常量（Main 以锚点强制本面板 220 高；行高随轨道数自适应）
    readonly property int headerH: 26
    readonly property int rulerH: 18
    readonly property int footerH: 16
    readonly property int headW: 152
    readonly property real basePxPerSec: 16.0  // 默认缩放下 1 秒像素宽
    property real zoom: 1.0                    // 缩放倍率（Ctrl+=/Ctrl+-，0.2..64）

    // =========================================================================
    // 几何/换算工具
    // =========================================================================
    function pxPerSec() { return basePxPerSec * root.zoom; }
    function xFor(ms)   { return ms / 1000 * pxPerSec(); }
    function msFor(x)   { return Math.max(0, x * 1000 / pxPerSec()); }
    function trackCount() { return root.tracksModel.length; }
    // 标尺可视时长：已装载用真实总长，未装载给 60s 骨架（不误导为“已有内容”）
    function durMs() { return root.totalMs > 0 ? root.totalMs : 60000; }
    function laneH() { return root.height - headerH - rulerH - footerH; }
    function rowH()  { return laneH() / Math.max(1, trackCount()); }

    // 时间轴刻度自适应：选择使相邻刻度 ≥48px 的最小步长
    function tickStep() {
        var steps = [0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600];
        var px = pxPerSec();
        for (var i = 0; i < steps.length; ++i)
            if (steps[i] * px >= 48) return steps[i];
        return 600;
    }
    function tickCount() { return Math.floor(durMs() / 1000 / tickStep()) + 2; }
    function labelEvery() { return Math.max(1, Math.round(64 / (tickStep() * pxPerSec()))); }
    function isMajor(i)   { return (i % labelEvery()) === 0; }
    function tickX(i)     { return i * tickStep() * pxPerSec(); }
    function tickText(i)  { return UiStyle.fmtMs(Math.round(i * tickStep() * 1000)); }
    function contentW() {
        var vw = root.width - headW;
        return Math.max(vw, xFor(durMs()) + 60);
    }

    // 轨道类型 → 中文短名/语义色点（全部取自 UiStyle 色板，无散落魔法色）
    function typeCn(t) {
        switch (t) {
        case "audio":   return "音频"; case "video":   return "视频";
        case "scene":   return "场景"; case "command": return "指令";
        case "vj":      return "VJ";  case "light":   return "灯光";
        case "pixel":   return "像素"; case "device":  return "设备";
        }
        return t || "轨道";
    }
    function dotColor(t) {
        switch (t) {
        case "video":   return UiStyle.cOk;
        case "audio":   return UiStyle.cAccent;
        case "scene":   return UiStyle.cWarn;
        case "command": return UiStyle.cIdle;
        }
        return UiStyle.cIdle;
    }

    function shortRef(u) { return u && u.length > 10 ? u.slice(0, 10) : (u || ""); }
    function runningKeys() { return Object.keys(root.running); }
    function runAt(k) { return root.running[k]; }
    function isRunning(tk) { return !!root.running["" + tk]; }
    function playheadX() { return xFor(root.posMs); }

    // running 为 JS 对象：整体替换以触发绑定刷新
    function setRun(tk, obj) {
        var m = {};
        var ks = Object.keys(root.running);
        for (var i = 0; i < ks.length; ++i) m[ks[i]] = root.running[ks[i]];
        if (obj) m["" + tk] = obj; else delete m["" + tk];
        root.running = m;
    }
    function setEvt(t) {
        root.lastEvt = t;
        evtTimer.restart();
    }

    // =========================================================================
    // 下行发送（统一经 kernelBridge，遵循面板惯例；无内核时静默）
    // =========================================================================
    function postTo(dst, op, p) {
        if (!kernelBridge) return false;
        kernelBridge.postCommand(dst, op, p ? JSON.stringify(p) : "{}");
        return true;
    }
    function doSeek(ms) {
        ms = Math.max(0, ms);
        if (root.totalMs > 0) ms = Math.min(ms, root.totalMs);
        root.posMs = ms;
        root.postTo("engine.timeline", "transport.seek", { pos_ms: ms });
    }

    // =========================================================================
    // 上行回调（Main 分发；钩子签名与 Main 注释对齐）
    // =========================================================================
    function onStatus(o) {
        if (!o) return;
        root.engineSeen = true;
        if (o.play_state !== undefined) root.playState = o.play_state;
        if (o.pos_ms      !== undefined) root.posMs = o.pos_ms;
        if (o.total_ms    !== undefined) root.totalMs = o.total_ms;
    }
    function onReply(op, env) {
        var p = (env && env.params) || {};
        if (op === "timeline.tracks") {
            if (env && env.code === 0 && p.tracks && p.tracks.length > 0)
                root.tracksModel = p.tracks;
        } else if (op === "timeline.load") {
            if (env && env.code === 0 && p.total_ms !== undefined)
                root.totalMs = p.total_ms;
        }
    }
    function onEvent(op, env) {
        var p = (env && env.params) || {};
        root.engineSeen = true;
        if (op.indexOf("evt.timeline.") < 0) return;
        var tk = (p.track_index !== undefined) ? p.track_index : -1;
        if (op.indexOf("item_started") >= 0) {
            if (tk >= 0) root.setRun(tk, { ref: p.ref_uuid || "", startMs: p.start_ms || 0 });
            root.setEvt("▶ 轨" + (tk + 1) + (p.ref_uuid ? " " + root.shortRef(p.ref_uuid) : "")
                        + (p.start_ms !== undefined ? " @" + UiStyle.fmtMs(p.start_ms) : ""));
        } else if (op.indexOf("item_ended") >= 0) {
            if (tk >= 0) root.setRun(tk, null);
            root.setEvt("■ 轨" + (tk + 1) + " 条目播放结束");
        } else if (op.indexOf("item_aborted") >= 0) {
            if (tk >= 0) root.setRun(tk, null);
            root.setEvt("⏹ 轨" + (tk + 1) + " 条目中止");
        } else if (op.indexOf("item_added") >= 0) {
            root.setEvt("＋ 轨" + (tk + 1) + " 新增条目");
        } else if (op.indexOf("item_removed") >= 0) {
            root.setEvt("－ 移除条目 " + root.shortRef(p.item_id));
        } else if (op.indexOf("item_updated") >= 0) {
            root.setEvt("✎ 条目属性更新");
        } else if (op.indexOf("loaded") >= 0) {
            if (p.total_ms !== undefined) root.totalMs = p.total_ms;
            root.setEvt("时间线装载完成 · 总长 " + UiStyle.fmtMs(root.totalMs));
        }
    }
    function onKernelReady() {
        // 引擎就绪：拉取轨道清单（回复前保留默认 8 轨骨架，避免拉取失败时空白）；
        // 状态/走带由 Main 的 statusChanged 持续推送
        root.engineSeen = true;
        root.postTo("engine.timeline", "timeline.tracks", {});
    }
    function onZoomRequest(f) {
        if (!f) return;
        root.zoom = Math.max(0.2, Math.min(64, root.zoom * f));
        // 缩放中心跟随播放头：保持当前播放头尽量可见（平移端到视口内）
        if (laneScroller.width > 0 && root.playheadX() > laneScroller.width * 0.85)
            laneScroller.contentX = Math.max(0, root.playheadX() - laneScroller.width * 0.3);
    }
    function onPlayheadHome() {
        root.doSeek(0);
        laneScroller.contentX = 0;
        root.setEvt("播放头回到起点 00:00.00");
    }
    function onPlayheadEnd() {
        var end = root.totalMs > 0 ? root.totalMs : 0;
        root.doSeek(end);
        laneScroller.contentX = Math.max(0, root.playheadX() - laneScroller.width * 0.5);
        root.setEvt("播放头移到末端 " + UiStyle.fmtMs(end));
    }

    // 事件简报 4s 后回到常态提示
    Timer {
        id: evtTimer
        interval: 4000
        onTriggered: root.lastEvt = ""
    }

    // =========================================================================
    // 顶栏：标题 + 缩放/定位按钮 + 走带时间码
    // =========================================================================
    component DockBtn: Rectangle {
        id: btn
        property string label: ""
        property string tip: ""
        signal clicked()
        width: 22; height: 18
        radius: 3
        color: btnArea.containsMouse ? UiStyle.cField : "transparent"
        border.color: UiStyle.cBorder
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: btn.label
            color: UiStyle.cText
            font.pixelSize: 12
            font.bold: true
        }
        MouseArea {
            id: btnArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: btn.clicked()
        }
        ToolTip.visible: btnArea.containsMouse && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 700
    }

    Rectangle {
        id: header
        height: root.headerH
        anchors { top: parent.top; left: parent.left; right: parent.right }
        color: UiStyle.cPanelAlt
        border.color: UiStyle.cBorder
        border.width: 1

        Row {
            anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
            spacing: 6
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "时间线"
                color: UiStyle.cText
                font.pixelSize: 12
                font.bold: true
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.trackCount() + " 轨"
                color: UiStyle.cTextDim
                font.pixelSize: 10
            }
        }
        Row {
            anchors { left: parent.left; leftMargin: 96; verticalCenter: parent.verticalCenter }
            spacing: 5
            DockBtn {
                label: "－"; tip: "缩小时间线（Ctrl+-）"
                onClicked: root.onZoomRequest(0.8)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "×" + root.zoom.toFixed(2)
                color: UiStyle.cTextDim
                font.pixelSize: 10
                font.family: "Menlo, Consolas, monospace"
            }
            DockBtn {
                label: "＋"; tip: "放大时间线（Ctrl+=）"
                onClicked: root.onZoomRequest(1.25)
            }
            Rectangle { width: 1; height: 12; color: UiStyle.cBorder;
                anchors.verticalCenter: parent.verticalCenter }
            DockBtn {
                label: "⏮"; tip: "播放头回起点（Home）"
                onClicked: root.onPlayheadHome()
            }
            DockBtn {
                label: "⏭"; tip: "播放头到末端（End）"
                onClicked: root.onPlayheadEnd()
            }
        }
        // 播放状态灯 + 走带时间码（mm:ss.mmm / mm:ss.mmm）
        Row {
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 7; height: 7; radius: 4
                color: root.playState === "playing" ? UiStyle.cOk
                     : root.playState === "paused"  ? UiStyle.cWarn
                     : UiStyle.cIdle
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: UiStyle.playLabel(root.playState)
                color: UiStyle.cTextDim
                font.pixelSize: 10
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: UiStyle.fmtMs(root.posMs) + " / " + UiStyle.fmtMs(root.totalMs)
                color: UiStyle.cText
                font.pixelSize: 11
                font.family: "Menlo, Consolas, monospace"
            }
        }
    }

    // =========================================================================
    // 主体：左轨头列（固定） + 右时间内容区（可横向平移）
    // =========================================================================
    Row {
        anchors { top: header.bottom; bottom: footer.top; left: parent.left; right: parent.right }

        // ---- 轨头列（含标尺占位角格）----
        Rectangle {
            width: root.headW
            height: parent.height
            color: UiStyle.cPanel
            border.color: UiStyle.cBorder
            border.width: 1

            Column {
                anchors.fill: parent
                // 角格：与标尺等高，注明“轨道/时间”
                Rectangle {
                    width: parent.width; height: root.rulerH
                    color: UiStyle.cPanelAlt
                    border.color: UiStyle.cBorder; border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "轨道 / 时间"
                        color: UiStyle.cTextDim
                        font.pixelSize: 9
                    }
                }
                // 轨头行（行高与右侧轨道行严格一致）
                Repeater {
                    model: root.tracksModel
                    Rectangle {
                        width: parent.width; height: root.rowH()
                        color: index % 2 ? UiStyle.cPanel : UiStyle.cPanelAlt
                        border.color: UiStyle.cBorder; border.width: 1
                        Row {
                            anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
                            spacing: 5
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 6; height: 6; radius: 3
                                color: root.dotColor(modelData.type)
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: (modelData.index + 1) + " " + root.typeCn(modelData.type)
                                color: UiStyle.cText
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                        Text {
                            anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
                            text: (modelData.name && modelData.name !== root.typeCn(modelData.type))
                                    ? modelData.name : ""
                            color: UiStyle.cTextDim
                            font.pixelSize: 9
                            elide: Text.ElideRight
                            width: Math.max(20, parent.width * 0.45)
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }
        }

        // ---- 右：标尺 + 轨道内容（同一时间坐标系的横向 Flickable）----
        Flickable {
            id: laneScroller
            width: parent.width - root.headW
            height: parent.height
            contentWidth: root.contentW()
            contentHeight: root.rulerH + root.laneH()
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            Item {
                id: timeCanvas
                width: root.contentW()
                height: root.rulerH + root.laneH()

                // ---- 标尺刻度 ----
                Repeater {
                    model: root.tickCount()
                    Rectangle {
                        x: root.tickX(index)
                        width: 1
                        height: root.isMajor(index) ? root.rulerH : root.rulerH * 0.5
                        y: root.rulerH - height   // 标尺刻度自标尺底线向上延伸
                        color: root.isMajor(index) ? "#556070" : "#39424f"
                        Text {
                            visible: root.isMajor(index)
                            anchors { left: parent.left; leftMargin: 3; top: parent.top }
                            text: root.tickText(index)
                            color: UiStyle.cTextDim
                            font.pixelSize: 8
                            font.family: "Menlo, Consolas, monospace"
                        }
                    }
                }
                // 底部标尺分隔线
                Rectangle {
                    y: root.rulerH - 1
                    width: timeCanvas.width; height: 1
                    color: UiStyle.cBorder
                }

                // ---- 轨道行底（首尾色带交替；正在播放的轨道加高亮）----
                Repeater {
                    model: root.tracksModel
                    Rectangle {
                        y: root.rulerH + index * root.rowH()
                        width: timeCanvas.width; height: root.rowH()
                        color: root.isRunning(modelData.index)
                                ? "#24303f"
                                : (index % 2 ? "transparent" : "#1e242d")
                        // 运行轨左侧强调条
                        Rectangle {
                            visible: root.isRunning(modelData.index)
                            width: 2; height: parent.height
                            color: UiStyle.cAccent
                        }
                    }
                }
                // ---- 轨道纵向网格（对齐标尺刻度，一次性绘制）----
                Repeater {
                    model: root.tickCount()
                    Rectangle {
                        x: root.tickX(index)
                        y: root.rulerH
                        width: 1
                        height: root.laneH()
                        color: "#ffffff08"
                    }
                }

                // ---- 运行条目芯片（item_started 驱动，随播放头提示当前内容）----
                Repeater {
                    model: root.runningKeys()
                    Rectangle {
                        id: chip
                        x: Math.min(root.playheadX() + 4, timeCanvas.width - 150)
                        y: root.rulerH + (parseInt(modelData) * root.rowH()) + (root.rowH() - 13) / 2
                        width: chipTxt.width + 10
                        height: 13
                        radius: 3
                        color: UiStyle.cAccent
                        Text {
                            id: chipTxt
                            anchors { left: parent.left; leftMargin: 4; verticalCenter: parent.verticalCenter }
                            text: "▶ " + root.shortRef(root.runAt(modelData).ref)
                            color: "#ffffff"
                            font.pixelSize: 8
                            font.bold: true
                        }
                    }
                }

                // ---- 播放头（贯穿标尺 + 轨道区；点击/拖拽轨道区可定位）----
                Rectangle {
                    x: root.playheadX()
                    y: 0
                    width: 1
                    height: timeCanvas.height
                    color: UiStyle.cAccent
                    // 播放头顶钮
                    Rectangle {
                        x: -3; y: 0
                        width: 7; height: 7
                        radius: 1
                        color: UiStyle.cAccent
                    }
                }

                // ---- 定位交互：在标尺/轨道区按下或拖拽即 seek ----
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onPressed:  root.doSeek(root.msFor(Math.max(0, mouse.x)))
                    onPositionChanged: if (pressed) root.doSeek(root.msFor(Math.max(0, mouse.x)))
                }
            }
        }
    }

    // =========================================================================
    // 底部状态行：事件简报 / 运行提示 / 引擎状态
    // =========================================================================
    Rectangle {
        id: footer
        height: root.footerH
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        color: UiStyle.cPanelAlt
        border.color: UiStyle.cBorder
        border.width: 1

        Text {
            anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
            text: root.lastEvt !== ""
                    ? root.lastEvt
                    : (root.engineSeen
                        ? "点击标尺/轨道定位播放头 · Ctrl+=/Ctrl+- 缩放 · Home/End 定位首尾"
                        : "等待引擎连接：就绪后自动载入轨道与走带（Task D 注入 kernelBridge）")
            color: root.lastEvt !== "" ? UiStyle.cText : UiStyle.cTextDim
            font.pixelSize: 10
            elide: Text.ElideRight
            width: parent.width * 0.72
        }
        Text {
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            text: root.engineSeen ? "● 引擎在线" : "○ 预览骨架"
            color: root.engineSeen ? UiStyle.cOk : UiStyle.cIdle
            font.pixelSize: 9
        }
    }
}
