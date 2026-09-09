// ShowMaster 桌面端 —— 共享样式/工具（P1-1-C2 起各面板复用）
// -----------------------------------------------------------------------------
// 以 .pragma library JS 单例库方式暴露：引擎对同一库只加载一次，全部导入方
// 共享同一实例（等同跨文档单例，语义由 Qt 引擎保证，不依赖 QML 模块/qmldir）。
// 面板内 `import "../UiStyle.js" as UiStyle` 后即可用 `UiStyle.cPanel`、
// `UiStyle.fmtMs(ms)` 访问。颜色以十六进制字符串提供，可直接赋给 color。
// 深色导播风格色板与时间/状态文案工具集中于此，杜绝逐文件散落魔法色值。
// =============================================================================

.pragma library

// ---- 色板（与 Main.qml 根注释一致）----
var cBg       = "#1b1f26";   // 窗口底
var cPanel    = "#232933";   // 面板底
var cPanelAlt = "#2a313c";   // 面板内部次级底
var cField    = "#313945";   // 输入框/缩略图底
var cBorder   = "#39424f";   // 分隔线/描边
var cText     = "#e6e9ee";   // 主文本
var cTextDim  = "#8b94a3";   // 次要文本
var cAccent   = "#2f81f7";   // 主强调（播放/GO/选中）
var cOk       = "#3fb950";
var cWarn     = "#d29922";
var cErr      = "#f85149";
var cIdle     = "#6b7280";   // 灰（待处理/空闲）

// §9.4 素材预处理状态点配色：灰=待处理 蓝=处理中 绿=就绪 红=失败
var preIdle    = "#6b7280";
var preWorking = "#2f81f7";
var preReady   = "#3fb950";
var preFailed  = "#f85149";

// ---- 素材预处理状态（§9.4 状态点）----
// 引擎侧字符串：done/failed（media_library.h）；UI 全态：pending/working/ready/failed
// 统一归一：null/pending=灰 待处理，working=蓝 处理中，done/ready=绿 就绪，
//           failed/error=红 失败（可重试）。面板直接调用 UiStyle.preprocColor/Label。
function preprocColor(s) {
    if (s === "working" || s === "processing") return preWorking;
    if (s === "done"    || s === "ready")      return preReady;
    if (s === "failed"  || s === "error")      return preFailed;
    return preIdle;
}
function preprocLabel(s) {
    if (s === "working" || s === "processing") return "处理中";
    if (s === "done"    || s === "ready")      return "就绪";
    if (s === "failed"  || s === "error")      return "失败";
    return "待处理";
}

// ---- 时间工具（播放头/时长显示统一格式 mm:ss.mmm / h:mm:ss.mmm）----
function fmtMs(ms) {
    if (ms === undefined || ms === null || isNaN(ms) || ms < 0) ms = 0;
    var t = Math.floor(ms);
    var m = Math.floor(t / 60000);
    var s = Math.floor((t % 60000) / 1000);
    var mm = Math.floor((t % 1000) / 10);
    var mm2 = m < 10 ? "0" + m : "" + m;
    var ss = s < 10 ? "0" + s : "" + s;
    var ms3 = mm < 10 ? "0" + mm : "" + mm;
    return mm2 + ":" + ss + "." + ms3;
}

// ---- 状态文案映射 ----
function playlistLabel(s) {
    switch (s) {
    case "idle":        return "就绪";
    case "loaded":      return "已装载";
    case "running":     return "运行中";
    case "waiting_go":  return "待触发";
    case "paused":      return "已暂停";
    case "ended":       return "已结束";
    }
    return s || "-";
}
function playLabel(s) {
    switch (s) {
    case "playing": return "播放中";
    case "paused":  return "已暂停";
    case "stopped": return "已停止";
    case "loading": return "载入中";
    case "error":   return "错误";
    }
    return s || "空闲";
}
function mediaTypeLabel(t) {
    switch (t) {
    case "video":    return "视频";
    case "audio":    return "音频";
    case "image":    return "图片";
    case "subtitle": return "字幕";
    }
    return t || "素材";
}
