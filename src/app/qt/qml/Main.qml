// ShowMaster Qt 桌面端 —— P1-1-C1 主窗口骨架
// =============================================================================
// §9.1 六区锚点布局（默认 1600x900，均以锚点约束、无绝对坐标叠放）：
//   ┌─────────────────────────────────────────────────────────┐  48px TopBar
//   ├──────────┬─────────────────────────────────┬───────────┤
//   │  Media    │        PreviewStage 中央区      │ Inspector │  ← 上抵 TopBar，
//   │  Library  │   （C4 内部再分 PVW/PGM）        │   300px   │     下抵 Timeline
//   │   280px   │                                 │           │
//   ├──────────┴─────────────────────────────────┴───────────┤ 220px Timeline
//   ├─────────────────────────────────────────────────────────┤  24px StatusBar
//   └─────────────────────────────────────────────────────────┘
//
// 职责边界：本文件只做“骨架 + 布局锚点 + 总线分发/全局快捷键”三层胶水；
// 各面板的业务逻辑必须写在 panels/ 对应文件中（C2–C6），不在本文件实现。
// 分发只按信封 op 选目标面板，不解析业务字段——解析归目标面板（单一职责）。
//
// 运行时契约（Task D ui_controller 注入，本文件不依赖具体 C++ 类型）：
//   上下文属性 kernelBridge（QObject）需提供：
//     Q_INVOKABLE postCommand(dst, op, paramsJson: string)
//     信号 busEvent(json) / statusChanged(json) / playlistChanged(json)
//          / frameReady(base64Jpeg) / kernelReady() / fatalError(msg)
//   面板调用发送一律经 root.post()，禁止直接散落 postCommand。
// =============================================================================

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import "panels"   // C2–C6 面板（目录导入：TopBar/StatusBar/TimelineDock 等即类型名）

ApplicationWindow {
    id: root

    // ---- §9.1 六区尺寸常量（锚点引用，改一处全局生效）----
    readonly property int topBarH:     48
    readonly property int mediaW:     280
    readonly property int inspectorW: 300
    readonly property int timelineH:  220
    readonly property int statusH:     24

    // 事件分发后供各面板消费的全局动作信号（面板在 Component.onCompleted 连接）
    // §9.3 快捷键只在此触发信号，具体语义由拥有数据的面板实现：
    signal requestSceneRecall(int index)     // F1–F4：召回场景，Inspector(C5) 消费
    signal requestImport()                   // Ctrl+I：导入素材，MediaLibrary(C3) 消费
    signal requestOpen()                     // Ctrl+O：打开工程，Inspector(C5) 消费
    signal requestSave()                     // Ctrl+S：保存当前工程/场景
    signal requestSaveAs()                   // Ctrl+Shift+S：另存/打包
    signal requestTimelineZoom(real factor)  // Ctrl+= / Ctrl+-：时间线缩放，C6 消费
    signal requestPlayheadHome()             // Home：播放头回起点，C6 消费
    signal requestPlayheadEnd()              // End：播放头到末端，C6 消费
    signal requestEscape()                   // Esc：停播 + 关闭覆盖层，C4 消费

    width: 1600
    height: 900
    minimumWidth: 1280
    minimumHeight: 720
    visible: true
    title: qsTr("ShowMaster 桌面工作台")

    // 视觉基调：深色导播风（面板各自可微调，背景统一取自根色板）
    readonly property color cBg:        "#1b1f26"
    readonly property color cPanel:     "#232933"
    readonly property color cPanelAlt:  "#2a313c"
    readonly property color cBorder:    "#39424f"
    readonly property color cText:      "#e6e9ee"
    readonly property color cTextDim:   "#8b94a3"
    readonly property color cAccent:    "#2f81f7"
    readonly property color cOk:        "#3fb950"
    readonly property color cWarn:      "#d29922"
    readonly property color cErr:       "#f85149"

    // =========================================================================
    // 总线消息分发中枢（C1 核心：只做信封解包与目标路由）
    // =========================================================================
    // 向下发送：dst/op 对齐 §5.1 引擎地址与 kernel.cpp 实际 sink 语义
    function post(dst, op, params) {
        if (!kernelBridge) { console.warn("[Main] kernelBridge 未注入，忽略命令", op); return; }
        kernelBridge.postCommand(dst, op, params ? JSON.stringify(params) : "{}");
    }
    // 便捷：快捷操作全部走 KernelHost 直连方法（规避 headless dst 映射陷阱）
    function bridge() { return kernelBridge; }

    // ---- 上行信号 → 规范化 JS 对象 → 分发给各面板 ----
    // 面板统一暴露回调（未实现的回调由 callPanel 静默跳过，便于逐步接入）：
    //   onStatus(obj)  onPlaylistSnapshot(obj)  onFrame(b64)  onBusEvent(type,op,env)
    function callPanel(panelId, method, arg1, arg2) {
        var p = root[panelId];
        if (!p || typeof p[method] !== "function") return false;
        p[method](arg1, arg2);
        return true;
    }

    function onStatusRaw(text) {
        var o = parseJson(text);
        if (!o) return;
        callPanel("topBar",      "onStatus", o);
        callPanel("statusBar",   "onStatus", o);
        callPanel("previewStage","onStatus", o);
        callPanel("inspector",   "onStatus", o);
        callPanel("timelineDock","onStatus", o);   // C6：播放头/总长/状态灯
    }
    function onPlaylistRaw(text) {
        var o = parseJson(text);
        if (!o) return;
        callPanel("inspector", "onPlaylistSnapshot", o);
    }
    function onFrameRaw(text) {
        callPanel("previewStage", "onFrame", text);
    }
    function onBusRaw(text) {
        var env = parseJson(text);
        if (!env) return;
        var type = env.type || "", op = env.op || "";
        if (type === "evt")      { routeEvent(op, env); }
        else if (type === "rsp" || type === "err") { routeReply(type, op, env); }
        else console.log("[Main] 忽略信封类型:", type, op);
    }

    // rsp/err：按 op 前缀选目标（回复接收方=命令发起方所在面板）
    function routeReply(type, op, env) {
        var ok = (type === "rsp" && env.code === 0);
        var errMsg = !ok ? (env.params && env.params.msg) || env.msg || "操作失败" : "";
        if (!ok) { callPanel("statusBar", "notify", errMsg, 2); }

        if (op === "media.query" || op === "media.import" || op === "media.remove"
                || op === "media.restore" || op === "media.update_tags") {
            callPanel("mediaPanel", "onReply", op, env);
        } else if (op === "media.preview") {
            callPanel("previewStage", "onReply", op, env);
        } else if (op === "scene.list" || op === "scene.save" || op === "scene.recall"
                || op === "scene.delete") {
            callPanel("inspector", "onReply", op, env);
        } else if (op.indexOf("timeline.") === 0) {
            callPanel("timelineDock", "onReply", op, env);
        } else if (op.indexOf("playlist.") === 0) {
            callPanel("inspector", "onReply", op, env);
        } else {
            console.log("[Main] 未路由回复:", op);
        }
    }

    // evt：广播事件天然带业务意图，按 op 前缀路由到数据归属面板；
    // 播放类事件同时补发 previewStage（节目名/播放状态展示）
    function routeEvent(op, env) {
        if (op.indexOf("evt.timeline.") === 0) {
            callPanel("timelineDock", "onEvent", op, env);
            if (op.indexOf("item_started") >= 0 || op.indexOf("item_ended") >= 0
                    || op.indexOf("item_aborted") >= 0)
                callPanel("previewStage", "onEvent", op, env);
        } else if (op.indexOf("evt.scene.") === 0) {
            callPanel("inspector", "onEvent", op, env);
        } else if (op.indexOf("evt.playlist.") === 0) {
            callPanel("inspector", "onEvent", op, env);
        } else {
            console.log("[Main] 未路由事件:", op);
        }
    }

    // ---- KernelHost 直连接口（含快捷方法，无信封往返）----
    function playPause()  { var b = bridge(); if (b) b.transportPlay(""); }
    function stop()       { var b = bridge(); if (b) b.transportStop(); }
    function pause()      { var b = bridge(); if (b) b.transportPause(); }
    function resume()     { var b = bridge(); if (b) b.transportResume(); }
    function go()         { var b = bridge(); if (b) b.playlistGo(); }

    function parseJson(text) {
        if (!text) return null;
        try { return JSON.parse(text); } catch (e) { console.warn("[Main] JSON 解析失败:", e); return null; }
    }

    Component.onCompleted: {
        // §9.3 全局快捷键→面板动作信号（信号定义于 root，此处接往各面板方法；
        // callPanel 对尚未实现的方法静默跳过，便于 C2–C6 逐步接入）
        root.requestImport.connect(function() { callPanel("mediaPanel", "onImportRequest"); });
        root.requestSceneRecall.connect(function(i) { callPanel("inspector", "onSceneRecallRequest", i); });
        root.requestTimelineZoom.connect(function(f) { callPanel("timelineDock", "onZoomRequest", f); });
        root.requestPlayheadHome.connect(function() { callPanel("timelineDock", "onPlayheadHome"); });
        root.requestPlayheadEnd.connect(function() { callPanel("timelineDock", "onPlayheadEnd"); });
        root.requestEscape.connect(function() { callPanel("previewStage", "onEscape"); });

        // 绑定 kernelBridge 上行信号（Task D 注入；缺失时仅告警不崩溃，便于独立预览）
        if (!kernelBridge) { console.warn("[Main] 未检测到 kernelBridge（Task D 注入）"); return; }
        kernelBridge.busEvent.connect(onBusRaw);
        kernelBridge.statusChanged.connect(onStatusRaw);
        kernelBridge.playlistChanged.connect(onPlaylistRaw);
        kernelBridge.frameReady.connect(onFrameRaw);
        kernelBridge.kernelReady.connect(function() {
            callPanel("statusBar", "notify", "引擎就绪", 0);
            kernelBridge.requestStatus();
            kernelBridge.requestPlaylist();
            // 各面板自行拉取自身数据（媒体库/场景/时间线轨道）：
            callPanel("mediaPanel", "onKernelReady");
            callPanel("inspector", "onKernelReady");
            callPanel("timelineDock", "onKernelReady");   // C6：就绪后拉取 timeline.tracks
        });
        kernelBridge.fatalError.connect(function(msg) {
            callPanel("statusBar", "notify", "致命错误: " + msg, 2);
        });
    }

    // =========================================================================
    // §9.1 六区锚点布局
    // =========================================================================
    background: Rectangle { color: cBg }

    TopBar { id: topBar; height: topBarH; anchors { top: parent.top; left: parent.left; right: parent.right } }

    StatusBar { id: statusBar; height: statusH;
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right } }

    TimelineDock { id: timelineDock; height: timelineH;
        anchors { bottom: statusBar.top; left: parent.left; right: parent.right } }

    MediaLibraryPanel { id: mediaPanel; width: mediaW;
        anchors { top: topBar.bottom; bottom: timelineDock.top; left: parent.left } }

    InspectorPanel { id: inspector; width: inspectorW;
        anchors { top: topBar.bottom; bottom: timelineDock.top; right: parent.right } }

    // 中央区为独立场景：C4 在内部再按 PGM/PVW 拆分并挂帧纹理
    PreviewStage { id: previewStage;
        anchors { top: topBar.bottom; bottom: timelineDock.top;
                  left: mediaPanel.right; right: inspector.left } }

    // =========================================================================
    // §9.3 全局快捷键（事件驱动层面：只做“按键→动作/信号”映射）
    // =========================================================================
    Shortcut { sequence: "Space";    onActivated: root.playPause() }
    Shortcut { sequence: "Shift+Space"; onActivated: root.stop() }
    Shortcut { sequence: "G";        onActivated: root.go() }
    Shortcut { sequence: "Esc";      onActivated: { root.stop(); root.requestEscape(); } }

    Shortcut { sequence: "Ctrl+O";        onActivated: root.requestOpen() }
    Shortcut { sequence: "Ctrl+S";        onActivated: root.requestSave() }
    Shortcut { sequence: "Ctrl+Shift+S";  onActivated: root.requestSaveAs() }
    Shortcut { sequence: "Ctrl+I";        onActivated: root.requestImport() }

    Shortcut { sequence: "F1"; onActivated: root.requestSceneRecall(0) }
    Shortcut { sequence: "F2"; onActivated: root.requestSceneRecall(1) }
    Shortcut { sequence: "F3"; onActivated: root.requestSceneRecall(2) }
    Shortcut { sequence: "F4"; onActivated: root.requestSceneRecall(3) }

    Shortcut { sequence: "Ctrl+="; onActivated: root.requestTimelineZoom(1.25) }
    Shortcut { sequence: "Ctrl+-"; onActivated: root.requestTimelineZoom(0.8) }
    Shortcut { sequence: "Home";   onActivated: root.requestPlayheadHome() }
    Shortcut { sequence: "End";    onActivated: root.requestPlayheadEnd() }

    // §9.3 撤销/重做：引擎侧未落位时保持静默占位，避免空实现误导
    Shortcut { sequence: "Ctrl+Z"; onActivated: callPanel("statusBar", "notify", "撤销：引擎未接入", 1) }
    Shortcut { sequence: "Ctrl+Y"; onActivated: callPanel("statusBar", "notify", "重做：引擎未接入", 1) }
}
