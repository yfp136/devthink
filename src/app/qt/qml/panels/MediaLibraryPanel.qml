// ShowMaster Qt 桌面端 —— P1-1-C3 左侧媒体库面板（280px）
// =============================================================================
// §9.1 左侧素材库：类型/回收站分类 + 素材列表 + 预览小窗 + 导入按钮
// §9.4 素材卡片预处理状态点：灰=待处理 蓝=处理中 绿=就绪 红=失败（点击红卡可重试）
// 数据契约（kernel.cpp engine.media sink）：
//   media.query   params {media_type,style_tags,include_recycle,name,page,page_size}
//                 reply params {items[],total,page,page_size,has_more}
//   media.import  params {paths[],auto_tag}  reply params {results[],total}
//   media.remove  params {media_id}   media.restore params {media_id}
//   media.preview params {media_id|path}       （帧/音频由 C4 预监承接）
// 所有命令经 kernelBridge.postCommand("engine.media", op, json) 发出；
// 回复经 Main 按 op 路由回本面板 onReply(op, env)。
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../UiStyle.js" as UiStyle   // 共享样式/工具（.pragma library 引擎级单例）

Rectangle {
    id: root

    color: UiStyle.cPanel
    border.color: UiStyle.cBorder
    border.width: 1

    // ---- 分类芯片数据（key 对齐 media_type 枚举，""=全部）----
    property var typeChips: [
        { key: "",            label: "全部" },
        { key: "video",       label: "视频" },
        { key: "audio",       label: "音频" },
        { key: "image",       label: "图片" },
        { key: "subtitle",    label: "字幕" }
    ]
    property string typeFilter: ""          // 当前类型过滤
    property bool inRecycle: false          // 回收站视图
    property string searchText: ""
    property int totalCount: 0              // 当前过滤下引擎侧总数
    property string selectedId: ""          // 列表选中素材
    property var selectedRow: null          // 预览小窗数据快照

    property bool busy: false               // 等待引擎回复（导入/查询中）
    property string hintText: ""
    property int hintLevel: 0               // 0=info 1=warn 2=err
    property int hintTick: 0                // 驱动本地横幅淡出

    ListModel { id: mediaModel }            // roles 见 refresh() append

    // =========================================================================
    // 本地提示横幅（成功/信息类提示不必占用全局状态栏；错误类由 Main 转发状态栏）
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
        color: hintLevel === 2 ? "#3a1d20"
             : hintLevel === 1 ? "#3a331a" : "#16324f"
        Behavior on height { NumberAnimation { duration: 140 } }
        Text {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            verticalAlignment: Text.AlignVCenter
            text: root.hintText
            color: hintLevel === 2 ? UiStyle.cErr
                 : hintLevel === 1 ? UiStyle.cWarn : "#7ab7ff"
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
    }
    // hintTick 的属主是 root：QML 的 onXxxChanged 只绑定「所在对象自身」的信号，
    // 写在子项 hintBanner 内会被解析为不存在的属性，运行期报
    // Cannot assign to non-existent property "onHintTickChanged"，
    // 进而使整个 MediaLibraryPanel 类型不可用（Main.qml 装载失败 → --smoke 退出 1）。
    onHintTickChanged: hintTimer.restart()

    // =========================================================================
    // 数据：查询/渲染/刷新
    // =========================================================================
    function post(op, params) {
        if (!kernelBridge) { hint("引擎未就绪", 2); return; }
        busy = true;
        kernelBridge.postCommand("engine.media", op, params ? JSON.stringify(params) : "{}");
    }
    function queryParams() {
        var p = { page: 1, page_size: 100 };
        if (root.typeFilter) p.media_type = root.typeFilter;
        if (root.inRecycle)  p.include_recycle = true;
        if (root.searchText.trim()) p.name = root.searchText.trim();
        return p;
    }
    function refresh() {
        root.selectedId = "";
        root.selectedRow = null;
        post("media.query", queryParams());
    }
    function onKernelReady() { refresh(); }

    // 面板回调：Main 收到 engine.media 回复后按 op 路由至此
    function onReply(op, env) {
        busy = false;
        if (!env || env.code !== 0) { refresh(); return; }   // 失败后同步真实状态
        var p = env.params || {};
        if (op === "media.query") {
            mediaModel.clear();
            var items = p.items || [];
            totalCount = p.total || items.length;
            for (var i = 0; i < items.length; i++) {
                var it = items[i];
                mediaModel.append({
                    media_id:     it.media_id || "",
                    file_name:    it.file_name || "",
                    media_type:   it.media_type || "other",
                    duration_ms:  it.duration_ms || 0,
                    width:        it.width || 0,
                    height:       it.height || 0,
                    fps:          it.fps || 0,
                    style_tags:   it.style_tags || "",
                    preproc:      it.preproc_status || "done",
                    preproc_msg:  it.preproc_msg || ""
                });
            }
            busy = false;
        } else if (op === "media.import") {
            var res = p.results || [];
            hint("导入完成：新增 " + res.length + " 个素材", 0);
            refresh();
        } else if (op === "media.remove") {
            hint("已移入回收站", 0);
            refresh();
        } else if (op === "media.restore") {
            hint("已从回收站恢复", 0);
            refresh();
        } else if (op === "media.update_tags") {
            hint("标签已更新", 0);
            refresh();
        }
    }

    // =========================================================================
    // 素材类型外观
    // =========================================================================
    function typeChar(t) {
        if (t === "video") return "视";
        if (t === "audio") return "音";
        if (t === "image") return "图";
        if (t === "subtitle") return "字";
        return "件";
    }
    function typeColor(t) {
        if (t === "video") return "#2f81f7";
        if (t === "audio") return "#3fb950";
        if (t === "image") return "#d29922";
        if (t === "subtitle") return "#a371f7";
        return "#6e7681";
    }
    function durationOf(row) {
        return UiStyle.fmtMs(row.duration_ms);
    }

    // =========================================================================
    // 视图主体（顶部提示横幅之下为：标题行 → 分类芯片 → 回收站/搜索 → 列表 → 预览小窗）
    // =========================================================================
    ColumnLayout {
        anchors { top: hintBanner.bottom; left: parent.left; right: parent.right;
                  bottom: parent.bottom }
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.topMargin: 6
        spacing: 6

        // ---- 标题行：媒体库 + 计数 + 导入 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                text: "媒体库"
                color: UiStyle.cText
                font.pixelSize: 13
                font.bold: true
            }
            Text {
                text: root.inRecycle ? "回收站"
                     : (totalCount > 0 ? totalCount + " 项" : "")
                color: root.inRecycle ? UiStyle.cWarn : UiStyle.cTextDim
                font.pixelSize: 10
                visible: root.inRecycle || totalCount > 0
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                id: busyChip
                Layout.preferredWidth: busy ? 40 : 0
                Layout.preferredHeight: 16
                visible: busy
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
                id: importBtn
                Layout.preferredWidth: 54
                Layout.preferredHeight: 22
                enabled: !!kernelBridge && !root.inRecycle
                onClicked: root.onImportRequest()
                contentItem: Text {
                    text: "+ 导入"
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
                ToolTip.visible: hovered
                ToolTip.text: "导入素材文件 (Ctrl+I)"
                ToolTip.delay: 600
            }
        }

        // ---- 分类芯片（媒体类型 / 回收站）----
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: root.typeChips
                Button {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 20
                    onClicked: {
                        root.inRecycle = false;
                        root.typeFilter = modelData.key;
                        refresh();
                    }
                    contentItem: Text {
                        text: modelData.label
                        color: (!root.inRecycle && root.typeFilter === modelData.key)
                               ? "#ffffff" : UiStyle.cTextDim
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 3
                        color: (!root.inRecycle && root.typeFilter === modelData.key)
                               ? UiStyle.cAccent : (parent.hovered ? UiStyle.cPanelAlt : "transparent")
                        border.color: UiStyle.cBorder
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                Layout.preferredWidth: 52
                Layout.preferredHeight: 20
                onClicked: {
                    root.inRecycle = !root.inRecycle;
                    if (root.inRecycle) root.typeFilter = "";
                    refresh();
                }
                contentItem: Text {
                    text: "回收站"
                    color: root.inRecycle ? "#e3b341" : UiStyle.cTextDim
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 3
                    color: root.inRecycle ? "#3d3316" : (parent.hovered ? UiStyle.cPanelAlt : "transparent")
                    border.color: root.inRecycle ? UiStyle.cWarn : UiStyle.cBorder
                }
            }
        }

        // ---- 搜索行 ----
        TextField {
            id: searchField
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            placeholderText: "搜索文件名 / 标签"
            placeholderTextColor: UiStyle.cTextDim
            color: UiStyle.cText
            font.pixelSize: 11
            selectByMouse: true
            onAccepted: { root.searchText = text; refresh(); }
            background: Rectangle {
                radius: 3
                color: UiStyle.cField
                border.color: UiStyle.cBorder
            }
        }

        // ---- 素材列表区（含空态覆盖层）----
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ListView {
                id: mediaList
                anchors.fill: parent
                model: mediaModel
                spacing: 4
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { width: 6; policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: row
                    property bool sel: root.selectedId === model.media_id
                    width: mediaList.width
                    height: 50
                    radius: 4
                    color: sel ? "#1d3a5f" : (rowHover.hovered ? UiStyle.cPanelAlt : "transparent")
                    border.color: sel ? UiStyle.cAccent : "transparent"
                    border.width: 1

                    MouseArea {
                        id: rowHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.selectItem(model.media_id)
                        onDoubleClicked: root.previewItem(model.media_id)
                    }

                    // 缩略占位（左 58px：类型字符块）
                    Rectangle {
                        anchors { left: parent.left; leftMargin: 5; verticalCenter: parent.verticalCenter }
                        width: 58
                        height: 38
                        radius: 3
                        color: UiStyle.cBg
                        border.color: UiStyle.cBorder
                        Text {
                            anchors.centerIn: parent
                            text: root.typeChar(model.media_type)
                            color: root.typeColor(model.media_type)
                            font.pixelSize: 14
                            font.bold: true
                        }
                    }

                    // 主信息列
                    Column {
                        anchors { left: parent.left; leftMargin: 70; right: parent.right; rightMargin: 6
                                  verticalCenter: parent.verticalCenter }
                        spacing: 2
                        Text {
                            width: parent.width
                            text: model.file_name
                            color: row.sel ? "#ffffff" : UiStyle.cText
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                        Row {
                            spacing: 6
                            Text {
                                text: UiStyle.mediaTypeLabel(model.media_type)
                                color: UiStyle.cTextDim
                                font.pixelSize: 10
                            }
                            Text {
                                text: root.durationOf({ duration_ms: model.duration_ms })
                                color: UiStyle.cTextDim
                                font.pixelSize: 10
                                visible: model.duration_ms > 0
                            }
                            Text {
                                text: (model.width > 0 ? model.width + "×" + model.height : "")
                                color: UiStyle.cTextDim
                                font.pixelSize: 10
                                visible: model.width > 0
                            }
                            Text {
                                text: model.style_tags
                                color: "#7a8ba5"
                                font.pixelSize: 10
                                elide: Text.ElideRight
                                width: 60
                                visible: model.style_tags !== ""
                            }
                        }
                    }

                    // §9.4 预处理状态点 + 悬浮动作（删/恢复、失败重试）
                    Rectangle {
                        anchors { right: parent.right; rightMargin: 5; top: parent.top; topMargin: 6 }
                        width: 7; height: 7; radius: 4
                        color: UiStyle.preprocColor(model.preproc)
                        ToolTip.visible: rowHover.hovered
                        ToolTip.text: UiStyle.preprocLabel(model.preproc)
                                       + (model.preproc_msg ? "：" + model.preproc_msg : "")
                        ToolTip.delay: 500
                    }
                    Row {
                        anchors { right: parent.right; rightMargin: 5; bottom: parent.bottom; bottomMargin: 4 }
                        spacing: 4
                        // 失败可重试（§9.4：引擎预处理器位后为占位行为，仅提示不空转）
                        Rectangle {
                            width: 24; height: 14; radius: 2
                            visible: rowHover.hovered && model.preproc === "failed"
                            color: "#4a2b2b"
                            Text {
                                anchors.centerIn: parent
                                text: "重试"
                                color: UiStyle.cErr
                                font.pixelSize: 9
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.hint("重试预处理：引擎预处理器位尚未开放，M1 联调后接入", 1)
                            }
                        }
                        Rectangle {
                            width: 30; height: 14; radius: 2
                            visible: rowHover.hovered && !row.sel
                            color: root.inRecycle ? "#2d3a2e" : "#3d2a2b"
                            Text {
                                anchors.centerIn: parent
                                text: root.inRecycle ? "恢复" : "删除"
                                color: root.inRecycle ? UiStyle.cOk : UiStyle.cErr
                                font.pixelSize: 9
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.requestRemove(model.media_id, model.file_name)
                            }
                        }
                    }
                }

                // §9.4 空态：素材库/分类/回收站为空时给出引导与"一键"入口，不允许空白
                // 覆盖层常驻列表区，仅在无数据时显示（与 delegate 不共存）
            }

            Item {
                id: emptyZone
                anchors.fill: parent
                visible: mediaModel.count === 0 && !root.busy
                Rectangle { anchors.fill: parent; color: "transparent" }

                Column {
                    anchors.centerIn: parent
                    width: parent.width - 24
                    spacing: 8

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.inRecycle ? "回收站为空"
                            : (root.typeFilter !== "" || root.searchText !== "")
                              ? "没有匹配的素材" : "素材库还是空的"
                        color: UiStyle.cTextDim
                        font.pixelSize: 12
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: root.inRecycle ? "删除的素材会先进入回收站，可随时恢复"
                            : (root.typeFilter !== "" || root.searchText !== "")
                              ? "换个分类或关键词试试，或清除筛选条件"
                              : "把视频、音频、图片导入媒体库，拖入时间线即可编排"
                        color: "#5c6672"
                        font.pixelSize: 10
                    }
                    Button {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: !root.inRecycle
                                && root.typeFilter === "" && root.searchText === ""
                        width: 108
                        height: 24
                        enabled: !!kernelBridge
                        onClicked: root.onImportRequest()
                        contentItem: Text {
                            text: "一键导入素材"
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
                    Button {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: root.typeFilter !== "" || root.searchText !== ""
                        width: 96
                        height: 24
                        onClicked: {
                            root.typeFilter = "";
                            root.searchText = "";
                            searchField.text = "";
                            refresh();
                        }
                        contentItem: Text {
                            text: "清除筛选"
                            color: UiStyle.cText
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? UiStyle.cPanelAlt : UiStyle.cField
                            border.color: UiStyle.cBorder
                        }
                    }
                }
            }
        }

        // ---- 预览小窗（选中素材快照 + 发送预监）----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            radius: 4
            color: UiStyle.cBg
            border.color: root.selectedRow ? UiStyle.cBorder : UiStyle.cBorder

            Text {
                anchors { top: parent.top; topMargin: 6; horizontalCenter: parent.horizontalCenter }
                text: root.selectedRow ? "选中素材" : "预览小窗"
                color: UiStyle.cTextDim
                font.pixelSize: 10
            }
            Text {
                anchors { top: parent.top; topMargin: 20; horizontalCenter: parent.horizontalCenter }
                text: root.selectedRow ? "" : "点击左侧素材查看详情；双击发送预监"
                color: "#4a5260"
                font.pixelSize: 10
            }

            // 选中素材快照
            RowLayout {
                visible: !!root.selectedRow
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.bottomMargin: 6
                spacing: 8

                Rectangle {
                    Layout.preferredWidth: 62
                    Layout.preferredHeight: 40
                    radius: 3
                    color: UiStyle.cPanelAlt
                    border.color: UiStyle.cBorder
                    Text {
                        anchors.centerIn: parent
                        text: root.typeChar(root.selectedRow.media_type)
                        color: root.typeColor(root.selectedRow.media_type)
                        font.pixelSize: 15
                        font.bold: true
                    }
                }
                Column {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        width: parent.width
                        text: root.selectedRow.file_name
                        color: UiStyle.cText
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                    Text {
                        text: (root.selectedRow.duration_ms > 0 ? UiStyle.fmtMs(root.selectedRow.duration_ms) + " · " : "")
                            + UiStyle.mediaTypeLabel(root.selectedRow.media_type)
                            + (root.selectedRow.width > 0 ? " · " + root.selectedRow.width + "×" + root.selectedRow.height : "")
                        color: UiStyle.cTextDim
                        font.pixelSize: 10
                    }
                    Row {
                        spacing: 4
                        Rectangle {
                            width: 6; height: 6; radius: 3
                            anchors.verticalCenter: parent.verticalCenter
                            color: UiStyle.preprocColor(root.selectedRow.preproc)
                        }
                        Text {
                            text: "预处理 " + UiStyle.preprocLabel(root.selectedRow.preproc)
                            color: UiStyle.cTextDim
                            font.pixelSize: 10
                        }
                    }
                }
                Button {
                    Layout.preferredWidth: 46
                    Layout.preferredHeight: 22
                    enabled: !!kernelBridge
                    onClicked: root.previewItem(root.selectedRow.media_id)
                    contentItem: Text {
                        text: "预监"
                        color: parent.enabled ? "#7ab7ff" : UiStyle.cTextDim
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 3
                        color: parent.hovered ? "#1c3a5e" : "transparent"
                        border.color: UiStyle.cAccent
                    }
                }
            }
        }
    }

    // =========================================================================
    // 交互动作
    // =========================================================================
    function selectItem(id) {
        for (var i = 0; i < mediaModel.count; i++) {
            var r = mediaModel.get(i);
            if (r.media_id === id) {
                selectedId = id;
                selectedRow = {
                    media_id: r.media_id, file_name: r.file_name,
                    media_type: r.media_type, duration_ms: r.duration_ms,
                    width: r.width, height: r.height, fps: r.fps,
                    preproc: r.preproc
                };
                return;
            }
        }
        selectedId = "";
        selectedRow = null;
    }
    function previewItem(id) {
        if (!kernelBridge) { hint("引擎未就绪", 2); return; }
        selectItem(id);
        kernelBridge.postCommand("engine.media", "media.preview",
            JSON.stringify({ media_id: id }));
        hint("已发送到预监（PGM）", 0);
    }
    function requestRemove(id, name) {
        pendingId = id;
        pendingName = name;
        confirmOverlay.state = "remove";     // remove | restore | retry
        confirmOpen = true;
    }

    function toLocalPath(urlStr) {
        if (!urlStr) return "";
        var s = "" + urlStr;
        if (s.indexOf("file://") === 0) s = s.substring(7);
        try { return decodeURIComponent(s); } catch (e) { return s; }
    }

    // Ctrl+I（Main 转发 requestImport）或面板按钮 → 打开文件选择
    function onImportRequest() {
        if (!kernelBridge) { hint("引擎未就绪，无法导入", 2); return; }
        if (root.inRecycle) { hint("请先退出回收站视图再导入", 1); return; }
        importDialog.open();
    }

    // =========================================================================
    // 删除二次确认（§9.4 破坏性操作统一应用内确认）
    // =========================================================================
    property string pendingId: ""
    property string pendingName: ""
    property bool confirmOpen: false

    Rectangle {
        id: confirmOverlay
        anchors.fill: parent
        visible: root.confirmOpen
        color: "#99000000"
        z: 50

        MouseArea { anchors.fill: parent }    // 吞掉下层事件

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(root.width - 24, 230)
            radius: 6
            color: UiStyle.cPanelAlt
            border.color: UiStyle.cBorder

            Column {
                anchors { left: parent.left; right: parent.right; margins: 12 }
                anchors.top: parent.top
                anchors.topMargin: 14
                spacing: 8
                Text {
                    width: parent.width
                    text: root.inRecycle ? "恢复该素材？" : "移入回收站？"
                    color: UiStyle.cText
                    font.pixelSize: 12
                    font.bold: true
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.pendingName.length > 24
                          ? root.pendingName.substring(0, 24) + "…"
                          : root.pendingName
                    color: UiStyle.cTextDim
                    font.pixelSize: 11
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.inRecycle
                          ? "恢复后素材回到正常媒体库。"
                          : "删除后进入回收站，可随时恢复；不会物理删除文件。"
                    color: "#5c6672"
                    font.pixelSize: 10
                }
                Row {
                    spacing: 8
                    Button {
                        width: 88; height: 24
                        text: "取消"
                        onClicked: root.confirmOpen = false
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
                            var op = root.inRecycle ? "media.restore" : "media.remove";
                            root.post(op, { media_id: root.pendingId });
                            root.confirmOpen = false;
                        }
                        contentItem: Text {
                            text: root.inRecycle ? "恢复" : "删除"
                            color: root.inRecycle ? UiStyle.cOk : UiStyle.cErr
                            font.pixelSize: 11
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: root.inRecycle ? "#1c3321" : "#3a1d20"
                            border.color: root.inRecycle ? UiStyle.cOk : UiStyle.cErr
                        }
                    }
                }
            }
        }
    }

    // =========================================================================
    // 文件选择（QtQuick.Dialogs；兼容 Qt6，任务 D 注入后可用）
    // =========================================================================
    FileDialog {
        id: importDialog
        title: "导入素材"
        selectMultiple: true
        nameFilters: ["媒体文件 (*.mp4 *.mov *.mkv *.m4v *.webm *.mp3 *.wav *.flac *.aac *.ogg *.jpg *.jpeg *.png *.webp *.gif *.srt *.ass *.vtt)", "所有文件 (*)"]
        onAccepted: {
            var urls = selectedFiles;
            if (!urls || urls.length === 0) return;
            var paths = [];
            for (var i = 0; i < urls.length; i++) paths.push(root.toLocalPath(urls[i]));
            hint("正在导入 " + paths.length + " 个文件…", 0);
            root.post("media.import", { paths: paths, auto_tag: true });
        }
    }

    Component.onCompleted: {
        // 无 kernelBridge 时维持可用空态展示（Task D 注入前可独立预览）
        if (!kernelBridge) return;
        refresh();
    }
}
