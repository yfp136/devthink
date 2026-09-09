// ShowMaster Qt 桌面端 —— P1-1-C5 右侧属性面板 InspectorPanel（300px）
// =============================================================================
// §9.1 右侧属性面板：按选中对象切换 素材属性/条目属性/场景属性/全局设置；
// §9.2 Phase 1 落地为两个页签（本版聚焦已确认的引擎 sink）：
//   · 场景 Scene：场景网格（缩略/名字/淡变）、保存当前态、召回（双击 / F1-F4）、删除（二次确认）
//   · 节目单 Playlist：播放列表状态与 GO、条目列表、空态引导（engine.playlist 驱动）
// 数据契约（kernel.cpp engine.scene / engine.playlist sink，本版仅用已注册操作）：
//   scene.list   -> reply params {items[{scene_id,scene_name,folder,fade_ms,recall_mode,create_ms,has_thumb}],total}
//   scene.save   params{scene_name,folder,fade_ms,recall_mode} -> reply {scene_id} + evt.scene.saved{scene_id}
//   scene.recall params{scene_id,fade_ms,recall_mode} -> reply OK + evt.scene.recalled{scene_id}
//   scene.delete params{scene_id} -> reply OK（无 evt，需自行刷新）
//   playlist.load params{items[]} -> reply {item_count} + evt.playlist.loaded
//   playlist.go / start / next / stop / pause / resume -> reply {state}
//   事件：evt.playlist.{loaded,item_started,item_ended,ended,waiting_go}（见 playlist_executor.cpp）
// 所有命令经 kernelBridge.postCommand(dst, op, json)；回复/事件由 Main 按信封路由回本面板
// （onReply / onEvent / onPlaylistSnapshot / onStatus）。
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

    // ---- 页签 ----
    readonly property int tabScene: 0
    readonly property int tabPlaylist: 1
    property int tab: tabScene

    // ---- 场景数据 ----
    property var sceneRows: []          // scene.list 返回 items（保留顺序）
    property string selSceneId: ""      // 网格选中（召回/删除作用对象）
    property bool sceneBusy: false
    property int statusSceneCount: -1   // kernel status 里的 scene_count（权威回写）

    // ---- 播放列表数据 ----
    property var playlistItems: []      // 最新 get_playlist_json 快照（含 state/current_index）
    property string playState: "idle"   // playlist_state（来自 status）
    property int playIndex: -1          // playlist_index（来自 status）
    property int itemCount: 0           // 本地已知条目数
    property bool playlistBusy: false

    // ---- 本地横幅 / 通用 ----
    property string hintText: ""
    property int hintLevel: 0           // 0=info 1=warn 2=err
    property int hintTick: 0

    // =========================================================================
    // 本地提示横幅（风格同 C3；仅信息类，错误由 Main 状态栏统一提示）
    // =========================================================================
    function hint(text, level) {
        hintText = text || "";
        hintLevel = level === undefined ? 0 : level;
        hintTick++;
    }
    Rectangle {
        id: hintBanner
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: hintText ? 22 : 0
        visible: height > 0
        color: hintLevel === 2 ? "#3a1d20" : (hintLevel === 1 ? "#3a331a" : "#16324f")
        Behavior on height { NumberAnimation { duration: 140 } }
        Text {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            verticalAlignment: Text.AlignVCenter
            text: root.hintText
            color: hintLevel === 2 ? UiStyle.cErr : (hintLevel === 1 ? UiStyle.cWarn : "#7ab7ff")
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Timer {
            id: hintTimer
            interval: 2600
            repeat: false
            running: root.hintText !== ""
            onTriggered: { root.hintText = ""; }
        }
        onHintTickChanged: { hintTimer.restart(); }
    }

    // =========================================================================
    // 命令发送与回调（Main 契约入口）
    // =========================================================================
    function post(dst, op, params) {
        if (!kernelBridge) { hint("引擎未就绪", 2); return; }
        kernelBridge.postCommand(dst, op, params ? JSON.stringify(params) : "{}");
    }
    function requestSceneList() {
        root.sceneBusy = true;
        post("engine.scene", "scene.list", {});
    }

    // kernel 就绪：拉一次权威场景表；播放列表由 Main 主动推（requestPlaylist）
    function onKernelReady() { requestSceneList(); }

    // 上行广播 → 场景标签自身维护的 scene_count 兜底
    function onStatus(o) {
        if (o && typeof o.playlist_state === "string") playState = o.playlist_state;
        if (o && typeof o.playlist_index === "number") playIndex = o.playlist_index;
        if (o && typeof o.scene_count === "number") statusSceneCount = o.scene_count;
    }
    function onPlaylistSnapshot(o) {
        if (!o) return;
        if (Array.isArray(o.items)) playlistItems = o.items;
        if (o.state) playState = o.state;
        if (typeof o.current_index === "number") playIndex = o.current_index;
        if (Array.isArray(o.items)) itemCount = o.items.length;
    }

    // Main routeReply → onReply（scene.* / playlist.* 收在本面板）
    function onReply(op, env) {
        sceneBusy = false;
        playlistBusy = false;
        // 错误回复已由 Main 状态栏播报（如 SCENE_NOT_FOUND），本地不叠加成功提示
        if (env && typeof env.code === "number" && env.code !== 0) return;
        var p = env && env.params ? env.params : {};
        if (op === "scene.list") {
            if (Array.isArray(p.items)) {
                sceneRows = p.items;
                // 保序且尽量保持选中（F1-F4 对应前 4 个，与 Main 键位一致）
                var keep = root.selSceneId !== ""
                        && sceneRows.some(function(r) { return r.scene_id === root.selSceneId; });
                if (sceneRows.length > 0 && !keep)
                    selSceneId = sceneRows[0].scene_id || "";
                else if (sceneRows.length === 0)
                    selSceneId = "";   // 列表清空时同步失效选中，防止误删/误召回
            }
            return;
        }
        if (op === "scene.save") {
            hint("场景已保存", 0);
            requestSceneList();   // 唯一刷新点（evt.scene.saved 不再回读，避免双刷新）
            return;
        }
        if (op === "scene.delete") { hint("场景已删除", 0); requestSceneList(); return; }
        if (op === "scene.recall") { return; }   // 召回不改场景表，无需刷新；效果由事件播报
        if (op.indexOf("playlist.") === 0) {
            var st = p.state || "";
            if (op === "playlist.load")      { hint("节目单已装载 " + (p.item_count !== undefined ? "（" + p.item_count + " 项）" : ""), 0); }
            else if (op === "playlist.go")   { if (st) playState = st; }
            else if (op === "playlist.stop") { if (st) playState = st; }
            else if (op === "playlist.pause"){ if (st) playState = st; }
            else if (op === "playlist.resume"){ if (st) playState = st; }
            return;
        }
    }

    // Main routeEvent → onEvent（evt.scene.* / evt.playlist.* 收在本面板）
    function onEvent(op, env) {
        var p = env && env.params ? env.params : {};
        if (op === "evt.scene.saved" || op === "evt.scene.recalled") {
            // saved 已由 scene.save reply 刷新（避免双刷新）；recalled 回读一次即可
            if (op === "evt.scene.recalled") {
                requestSceneList();
                hint("已召回场景" + (p.scene_id ? "（" + shortId(p.scene_id) + "）" : ""), 0);
            }
            return;
        }
        if (op.indexOf("evt.playlist.") === 0) {
            // 事件驱动：等待 kernel 下一次 status/playlist 快照同步即可；
            // 本面板先本地消化文本语义，避免轮询。
            if (op === "evt.playlist.loaded")    { /* 快照将同步 */ }
            else if (op === "evt.playlist.item_started") { if (typeof p.index === "number") playIndex = p.index; }
            else if (op === "evt.playlist.item_ended")   { /* 下一快照同步 */ }
            else if (op === "evt.playlist.ended")        { playState = "ended"; playIndex = -1; }
            else if (op === "evt.playlist.waiting_go")   { playState = "waiting_go"; if (typeof p.index === "number") playIndex = p.index; }
            return;
        }
    }

    // F1-F4（Main requestSceneRecall）→ 召回前 4 个场景（带淡变）
    function onSceneRecallRequest(i) {
        if (i < 0 || i >= sceneRows.length) {
            hint("无场景可召回（按 F1-F4 召回的 1-4 号）", 1);
            return;
        }
        recallScene(sceneRows[i].scene_id);
    }

    // 双击网格 / 面板动作 → 召回
    function recallScene(id) {
        var row = findScene(id);
        if (!row) { hint("场景不存在或已被删除", 2); return; }
        if (!kernelBridge) { hint("引擎未就绪", 2); return; }
        var p = { scene_id: id };
        if (row.fade_ms) p.fade_ms = row.fade_ms;
        post("engine.scene", "scene.recall", p);
    }
    function findScene(id) {
        for (var i = 0; i < sceneRows.length; i++)
            if (sceneRows[i] && sceneRows[i].scene_id === id) return sceneRows[i];
        return null;
    }
    function shortId(id) { return id ? ("" + id).substring(0, 6) : ""; }

    // 节目单条目类型（playlist item type）→ 中文标签；素材类回落到 mediaTypeLabel
    function itemTypeLabel(t) {
        switch (t) {
        case "media":             return "媒体";
        case "scene":             return "场景";
        case "delay":             return "延时";
        case "command":           return "命令";
        case "timeline_segment":  return "时间线";
        }
        return UiStyle.mediaTypeLabel(t || "media");
    }
    // 场景操作行文案（绑定引用：内部读 root 属性以触发依赖）
    function selSceneLabel() {
        if (!root.selSceneId) return "当前选中：未选择";
        var r = findScene(root.selSceneId);
        return "当前选中：" + (r ? (r.scene_name || "场景") : "（未知）");
    }

    // =========================================================================
    // 空态 / 场景保存对话框（§9.4：空态给"一键创建"入口，不允许空白）
    // =========================================================================
    property bool saveOpen: false
    property bool deleteAskOpen: false

    Rectangle {
        id: overlay
        anchors.fill: parent
        visible: root.saveOpen || root.deleteAskOpen
        color: "#99000000"
        z: 60
        MouseArea {
            anchors.fill: parent
            // 点击对话框外部 → 关闭（Esc 语义由对话框内容自行处理）
            onClicked: { root.saveOpen = false; root.deleteAskOpen = false; }
        }

        // ---- 保存场景对话框（engine.scene.scene.save 无全局工程态，属引擎内场景快照）----
        Rectangle {
            id: saveDlg
            anchors.centerIn: parent
            width: Math.min(root.width - 24, 250)
            radius: 6
            color: UiStyle.cPanelAlt
            border.color: UiStyle.cBorder
            visible: root.saveOpen
            onVisibleChanged: { if (visible) sceneNameField.forceActiveFocus(); }

            MouseArea { anchors.fill: parent }   // 吞掉空白区点击，防冒泡触发外部关闭

            Column {
                anchors { left: parent.left; right: parent.right; margins: 12 }
                anchors.top: parent.top
                anchors.topMargin: 14
                spacing: 8
                Text {
                    width: parent.width
                    text: "保存当前场景"
                    color: UiStyle.cText
                    font.pixelSize: 12
                    font.bold: true
                }
                TextField {
                    id: sceneNameField
                    width: parent.width
                    placeholderText: "场景名称"
                    placeholderTextColor: UiStyle.cTextDim
                    color: UiStyle.cText
                    font.pixelSize: 11
                    selectByMouse: true
                    onAccepted: root.doSaveScene()
                    background: Rectangle {
                        radius: 3
                        color: UiStyle.cField
                        border.color: UiStyle.cBorder
                    }
                }
                Row {
                    spacing: 8
                    Button {
                        width: 88; height: 24
                        text: "取消"
                        onClicked: root.saveOpen = false
                        contentItem: Text {
                            text: "取消"
                            color: UiStyle.cText
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? UiStyle.cPanel : UiStyle.cField
                            border.color: UiStyle.cBorder
                        }
                    }
                    Button {
                        width: 88; height: 24
                        onClicked: root.doSaveScene()
                        contentItem: Text {
                            text: "保存"
                            color: "#ffffff"
                            font.pixelSize: 11
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? "#1d63d0" : "#1f6feb"
                        }
                    }
                }
            }
        }

        // ---- 删除场景二次确认（§9.4 破坏性操作统一应用内确认）----
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(root.width - 24, 250)
            radius: 6
            color: UiStyle.cPanelAlt
            border.color: UiStyle.cBorder
            visible: root.deleteAskOpen

            MouseArea { anchors.fill: parent }   // 吞掉空白区点击，防冒泡触发外部关闭

            Column {
                anchors { left: parent.left; right: parent.right; margins: 12 }
                anchors.top: parent.top
                anchors.topMargin: 14
                spacing: 8
                Text {
                    width: parent.width
                    text: "删除场景？"
                    color: UiStyle.cText
                    font.pixelSize: 12
                    font.bold: true
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "删除后不可恢复。当前引擎为场景快照（内存态），重启引擎将丢失全部场景。"
                    color: "#5c6672"
                    font.pixelSize: 10
                }
                Row {
                    spacing: 8
                    Button {
                        width: 88; height: 24
                        text: "取消"
                        onClicked: root.deleteAskOpen = false
                        contentItem: Text {
                            text: "取消"
                            color: UiStyle.cText
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? UiStyle.cPanel : UiStyle.cField
                            border.color: UiStyle.cBorder
                        }
                    }
                    Button {
                        width: 88; height: 24
                        onClicked: {
                            root.post("engine.scene", "scene.delete", { scene_id: root.selSceneId });
                            root.deleteAskOpen = false;
                        }
                        contentItem: Text {
                            text: "删除"
                            color: UiStyle.cErr
                            font.pixelSize: 11
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: "#3a1d20"
                            border.color: UiStyle.cErr
                        }
                    }
                }
            }
        }
    }

    function doSaveScene() {
        var name = sceneNameField.text.trim();
        if (!name) { hint("请填写场景名称", 1); return; }
        root.post("engine.scene", "scene.save", { scene_name: name });
        root.saveOpen = false;
        sceneNameField.text = "";
    }

    // =========================================================================
    // 视图主体：顶栏（标题/计数 + 新场景）→ 页签 → 内容区
    // =========================================================================
    ColumnLayout {
        anchors { top: hintBanner.bottom; left: parent.left; right: parent.right;
                  bottom: parent.bottom }
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.topMargin: 6
        spacing: 6

        // ---- 标题行 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                text: root.tab === root.tabScene ? "场景" : "节目单"
                color: UiStyle.cText
                font.pixelSize: 13
                font.bold: true
            }
            Text {
                text: root.tab === root.tabScene
                      ? (sceneRows.length > 0 ? sceneRows.length + " 个" : (statusSceneCount > 0 ? statusSceneCount + " 个" : ""))
                      : (itemCount > 0 ? itemCount + " 项" : "")
                color: UiStyle.cTextDim
                font.pixelSize: 10
                visible: root.tab === root.tabScene ? (sceneRows.length > 0 || statusSceneCount > 0)
                                                     : (itemCount > 0)
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: (root.sceneBusy || root.playlistBusy) ? 40 : 0
                Layout.preferredHeight: 16
                visible: root.sceneBusy || root.playlistBusy
                radius: 3
                color: "#1c3a5e"
                Text {
                    anchors.centerIn: parent
                    text: "…"
                    color: "#7ab7ff"
                    font.pixelSize: 10
                }
            }
            Button {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 22
                enabled: !!kernelBridge && root.tab === root.tabScene
                onClicked: root.saveOpen = true
                contentItem: Text {
                    text: "＋ 保存态"
                    color: parent.enabled ? "#ffffff" : UiStyle.cTextDim
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 3
                    color: parent.down ? "#1d63d0"
                         : (parent.enabled ? "#1f6feb" : UiStyle.cPanelAlt)
                }
            }
        }

        // ---- 页签条 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: [
                    { key: root.tabScene, label: "场景" },
                    { key: root.tabPlaylist, label: "节目单" }
                ]
                Button {
                    Layout.preferredWidth: 66
                    Layout.preferredHeight: 22
                    onClicked: root.tab = modelData.key
                    contentItem: Text {
                        text: modelData.label
                        color: root.tab === modelData.key ? "#ffffff" : UiStyle.cTextDim
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 3
                        color: root.tab === modelData.key ? UiStyle.cAccent : (parent.hovered ? UiStyle.cPanelAlt : "transparent")
                        border.color: root.tab === modelData.key ? UiStyle.cAccent : UiStyle.cBorder
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        // =====================================================================
        // 内容区：场景页签（双列网格可滚动；空态覆盖全区域，操作行叠加底部）
        // =====================================================================
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.tab === root.tabScene
            clip: true

            // 场景缩略网格（2 列；单击选中 / 双击召回 / 右键删除，§9.2 场景召回）
            GridView {
                id: sceneGrid
                anchors.fill: parent
                anchors.topMargin: 4
                anchors.bottomMargin: root.sceneRows.length > 0 ? 28 : 0
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { width: 6; policy: ScrollBar.AsNeeded }
                cellWidth: sceneGrid.width / 2
                cellHeight: 92
                model: root.sceneRows

                delegate: Rectangle {
                    id: sceneCard
                    property bool sel: root.selSceneId === modelData.scene_id
                    width: sceneGrid.cellWidth - 8
                    height: 84
                    radius: 4
                    color: sel ? "#1d3a5f" : (cardMouse.hovered ? UiStyle.cPanelAlt : UiStyle.cField)
                    border.color: sel ? UiStyle.cAccent : UiStyle.cBorder
                    border.width: 1

                    MouseArea {
                        id: cardMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: {
                            root.selSceneId = modelData.scene_id;
                            // 右键 = 删除入口（破坏性操作走二次确认，§9.4）
                            if (mouse.button === Qt.RightButton) root.deleteAskOpen = true;
                        }
                        onDoubleClicked: {
                            if (mouse.button === Qt.LeftButton) {
                                root.selSceneId = modelData.scene_id;
                                root.recallScene(modelData.scene_id);
                            }
                        }
                    }

                    // 缩略占位
                    Rectangle {
                        anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
                        width: 44; height: 34
                        radius: 3
                        color: UiStyle.cBg
                        border.color: UiStyle.cBorder
                        Text {
                            anchors.centerIn: parent
                            text: "场"
                            color: "#7ab7ff"
                            font.pixelSize: 13
                            font.bold: true
                        }
                    }
                    // 名称 + 元信息
                    Column {
                        anchors { left: parent.left; leftMargin: 58; right: parent.right; rightMargin: 5
                                  verticalCenter: parent.verticalCenter }
                        spacing: 3
                        Text {
                            width: parent.width
                            text: modelData.scene_name || ("场景 " + shortId(modelData.scene_id))
                            color: sel ? "#ffffff" : UiStyle.cText
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                        Text {
                            text: "淡变 " + (modelData.fade_ms > 0 ? modelData.fade_ms + "ms" : "cut")
                                + (modelData.recall_mode && modelData.recall_mode !== "fade" ? " · " + modelData.recall_mode : "")
                            color: UiStyle.cTextDim
                            font.pixelSize: 9
                        }
                        Text {
                            text: "双击召回" + (index < 4 ? " · F" + (index + 1) : "")
                                + (modelData.has_thumb ? " · 缩略图" : "")
                            color: "#5c6672"
                            font.pixelSize: 9
                        }
                    }
                }
            }

            // 场景空态：读取中 / 无场景引导（§9.4 不允许空白面板）
            Item {
                anchors.fill: parent
                anchors.topMargin: 4
                visible: root.sceneRows.length === 0
                Column {
                    anchors.centerIn: parent
                    width: parent.width - 24
                    spacing: 8
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.sceneBusy ? "正在读取场景…" : "还没有保存的场景"
                        color: root.sceneBusy ? "#7ab7ff" : UiStyle.cTextDim
                        font.pixelSize: 12
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: !root.sceneBusy
                        text: "把当前输出画面存为场景快照，之后可按 F1-F4 一键淡变召回。"
                        color: "#5c6672"
                        font.pixelSize: 10
                    }
                    Button {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 120
                        height: 24
                        visible: !root.sceneBusy
                        enabled: !!kernelBridge
                        onClicked: root.saveOpen = true
                        contentItem: Text {
                            text: "保存当前场景"
                            color: parent.enabled ? "#ffffff" : UiStyle.cTextDim
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: parent.enabled ? (parent.down ? "#1d63d0" : "#1f6feb")
                                                   : UiStyle.cPanelAlt
                            border.color: parent.enabled ? "transparent" : UiStyle.cBorder
                        }
                    }
                }
            }

            // 场景操作行：选中信息 + 删除（网格底部预留 28px 避让）
            RowLayout {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 26
                visible: root.sceneRows.length > 0
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: root.selSceneLabel()
                    color: UiStyle.cTextDim
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }
                Button {
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 22
                    enabled: !!kernelBridge && root.selSceneId !== ""
                    onClicked: root.deleteAskOpen = true
                    contentItem: Text {
                        text: "删除"
                        color: parent.enabled ? UiStyle.cErr : UiStyle.cTextDim
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 3
                        color: parent.hovered && parent.enabled ? "#3d2a2b" : "transparent"
                        border.color: parent.enabled ? UiStyle.cErr : UiStyle.cBorder
                    }
                }
            }
        }

        // =====================================================================
        // 内容区：节目单页签
        // =====================================================================
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.tab === root.tabPlaylist
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                // 状态行：播放列表状态 + 当前位置
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: root.playState === "running" ? UiStyle.cOk
                             : root.playState === "paused" || root.playState === "waiting_go" ? UiStyle.cWarn
                             : UiStyle.cIdle
                    }
                    Text {
                        text: "播放列表：" + UiStyle.playlistLabel(root.playState)
                            + (root.playIndex >= 0 ? " · 当前 #" + (root.playIndex + 1) : "")
                        color: UiStyle.cText
                        font.pixelSize: 11
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        Layout.preferredWidth: 46
                        Layout.preferredHeight: 22
                        enabled: !!kernelBridge && root.itemCount > 0
                        onClicked: root.post("engine.playlist", "playlist.go", {})
                        contentItem: Text {
                            text: "GO"
                            color: parent.enabled ? "#ffffff" : UiStyle.cTextDim
                            font.pixelSize: 11
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 3
                            color: parent.enabled ? (parent.down ? "#1d63d0" : "#1f6feb") : UiStyle.cPanelAlt
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "GO（快捷键 G）"
                        ToolTip.delay: 600
                    }
                }

                // 条目列表
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    ListView {
                        id: plist
                        anchors.fill: parent
                        anchors.topMargin: 2
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { width: 6; policy: ScrollBar.AsNeeded }

                        // 模型 = engine 播放列表快照数组（JS Array 暴露 modelData + index，
                        // 不可用 ListModel 的角色语法）；空态由下方覆盖层负责，无需换模型。
                        model: root.playlistItems
                        delegate: Rectangle {
                            id: pitem
                            property bool isCur: index === root.playIndex
                            width: plist.width
                            height: 26
                            radius: 3
                            color: isCur ? "#1d3a5f" : (phover.hovered ? UiStyle.cPanelAlt : "transparent")
                            border.color: isCur ? UiStyle.cAccent : "transparent"
                            border.width: 1

                            MouseArea {
                                id: phover
                                anchors.fill: parent
                                hoverEnabled: true
                            }

                            RowLayout {
                                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                                anchors.leftMargin: 8
                                anchors.rightMargin: 6
                                spacing: 6
                                Text {
                                    text: modelData.trigger === "go" ? "▶"
                                        : modelData.trigger === "delay" ? "⏱"
                                        : modelData.trigger === "timecode" ? "◎" : ""
                                    color: modelData.trigger === "go" ? UiStyle.cAccent : UiStyle.cWarn
                                    font.pixelSize: 10
                                    visible: modelData.trigger && modelData.trigger !== "auto"
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.ref_uuid || ("条目 " + (index + 1))
                                    color: isCur ? "#ffffff" : UiStyle.cText
                                    font.pixelSize: 11
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    text: root.itemTypeLabel(modelData.type)
                                    color: UiStyle.cTextDim
                                    font.pixelSize: 10
                                }
                                Text {
                                    text: modelData.delay_ms ? (modelData.delay_ms + "ms") : ""
                                    color: UiStyle.cTextDim
                                    font.pixelSize: 9
                                    visible: modelData.delay_ms > 0
                                }
                            }
                        }
                    }

                    // 节目单空态：显示一键装载引导（无 engine 时展示占位信息）
                    Item {
                        anchors.fill: parent
                        visible: root.itemCount === 0 && !root.playlistBusy

                        Column {
                            anchors.centerIn: parent
                            width: parent.width - 24
                            spacing: 8
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: root.playState === "idle" ? "节目单为空" : "节目单已结束"
                                color: UiStyle.cTextDim
                                font.pixelSize: 12
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: "把素材拖入时间线后生成节目单；或在时间线面板装载后回此执行 GO。"
                                color: "#5c6672"
                                font.pixelSize: 10
                            }
                            Button {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 130
                                height: 24
                                enabled: !!kernelBridge
                                onClicked: root.hint("装载节目单：请使用时间线面板操作（C6）", 1)
                                contentItem: Text {
                                    text: "装载节目单"
                                    color: parent.enabled ? "#7ab7ff" : UiStyle.cTextDim
                                    font.pixelSize: 11
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 4
                                    color: parent.hovered && parent.enabled ? "#1c3a5e" : "transparent"
                                    border.color: parent.enabled ? UiStyle.cAccent : UiStyle.cBorder
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        // 无 kernelBridge 时维持可用空态展示（Task D 注入前可独立预览）
        if (!kernelBridge) return;
        requestSceneList();
    }
}
