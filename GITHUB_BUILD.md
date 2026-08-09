# DevThink Windows 安装包打包指南（GitHub Actions 自动打）

> 目标：在你这台 Mac 上**无法直接打出 Windows 的 `.exe`**（electron-builder 不支持 Mac→Win 交叉编译）。
> 本文用 GitHub Actions 在云端 Windows runner 上自动打包，一次性产出 **Windows（`.exe`）+ macOS（`.dmg`）+ Linux（`.AppImage`）** 三平台安装包。
> 你只需在本地做「初始化 git → 推代码 → 打 tag」，剩下的打包由 GitHub 完成，去下载即可。

---

## 前置条件

- [ ] 有一个 GitHub 账号（[免费注册](https://github.com/join)）
- [ ] 本机终端能跑 `git`（Mac 自带，无需额外安装）
- [ ] DevThink 源码目录在 `/Users/yxiao88/WorkBuddy/2026-08-09-10-38-23/DevThink/`
- [ ] 想保护源码 → 建**私有仓库**（推荐）；想开源 → 建公开仓库

> 成品说明：Windows 包目前**未签名**（无 Authenticode 证书），你自己装会被 SmartScreen 拦一下，点"仍要运行"即可；正式分发给别人需另购代码签名证书（文末说明）。

---

## 步骤 1：本地初始化 Git 并提交

打开你本机 Mac 的「终端」App，执行：

```bash
cd /Users/yxiao88/WorkBuddy/2026-08-09-10-38-23/DevThink

# 初始化仓库（如果还没有 .git）
git init

# 确认敏感文件不会被提交（有输出即代表 .devthinkrc 已被忽略，key 安全）
git check-ignore .devthinkrc
# ↑ 应输出 ".devthinkrc"。若没输出，先停下手，检查 .gitignore

# 添加全部源码（node_modules / dist / release / .devthinkrc 已被忽略，不会进来）
git add .

# 首次提交
git commit -m "DevThink v0.1.0 源码（双窗口 AI 全链路开发工具）"
```

看到 `git check-ignore .devthinkrc` 有输出后，才能放心 `git add .` —— 这保证 DeepSeek key 不会上传到 GitHub。

---

## 步骤 2：在 GitHub 建私有仓库

1. 打开 https://github.com/new
2. **Repository name** 填 `devthink`（或你喜欢的名字）
3. 选 **Private**（私有，保护源码；公开也行但源码会所有人可见）
4. **不要**勾选 "Add a README file" / ".gitignore" / "License"（本地已有，避免冲突）
5. 点 **Create repository**

建好后，页面会显示一个类似这样的地址（以你的账号为准）：
```
https://github.com/<你的用户名>/devthink.git
```
复制它，下一步要用。

---

## 步骤 3：关联远程并推送

把下面 `https://github.com/<你的用户名>/devthink.git` 换成**你自己的仓库地址**：

```bash
cd /Users/yxiao88/WorkBuddy/2026-08-09-10-38-23/DevThink

# 关联远程仓库（替换成你的地址）
git remote add origin https://github.com/<你的用户名>/devthink.git

# 推送主分支
git branch -M main
git push -u origin main
```

- 首次推送会弹窗要求登录 GitHub → 用浏览器授权，或用 Personal Access Token 当密码。
- 推送完去 GitHub 仓库页面，应能看到 `src/`、`electron/`、`package.json`、`.github/` 等文件，**不应**看到 `node_modules/`、`.devthinkrc`、`release/`。

---

## 步骤 4：触发 Windows 打包

`.github/workflows/build.yml` 已配好：推 `v*` 标签自动打包，或手动触发。任选一种：

### 方式 A：打版本标签（推荐，会自动发 GitHub Release）

```bash
cd /Users/yxiao88/WorkBuddy/2026-08-09-10-38-23/DevThink

# 打标签并推送 —— 这一推就会触发三平台打包
git tag v0.1.0
git push origin v0.1.0
```

### 方式 B：手动触发（不打标签也行）

1. 去你的 GitHub 仓库 → 顶部 **Actions** 标签
2. 左侧选 **Build DevThink Installers**
3. 点右侧 **Run workflow** → 选 `main` 分支 → **Run workflow**

---

## 步骤 5：等待并下载 Windows 安装包

打包在云端跑，约 **3–6 分钟**（Windows runner 上要下载 Electron 运行时 + 编译）。

- **方式 A（打了 tag）**：跑完会自动在仓库 **Releases** 页面生成 `v0.1.0`，里面挂着三个平台的包。
  → 打开仓库 **Releases** → 下载 `DevThink Setup 0.1.0.exe`（安装向导版）或 `DevThink 0.1.0.exe`（免安装便携版）。
- **方式 B（手动触发）**：去 **Actions** → 点刚跑的 workflow run → 底部 **Artifacts** → 下载 `DevThink-windows-latest` 这个 zip，解压得到 `.exe`。

> 产物文件名（electron-builder 默认）：
> - `DevThink Setup 0.1.0.exe` —— NSIS 安装向导（推荐分发）
> - `DevThink 0.1.0.exe` —— Portable 单文件免安装版
> - `DevThink-0.1.0-arm64.dmg` / `DevThink-0.1.0-x64.dmg` —— macOS（另一 runner 产出）
> - `DevThink-0.1.0.AppImage` —— Linux

---

## 步骤 6：在 Windows 上安装验证

把 `.exe` 拷到 Windows 机器：

- **NSIS 版**：双击 `DevThink Setup 0.1.0.exe` → 走安装向导 → 桌面生成快捷方式。
- **Portable 版**：直接双击 `DevThink 0.1.0.exe` 就能跑，不写注册表。

首次打开若被 **SmartScreen** 拦（"Windows 已保护你的电脑"）：
- 点 **详细信息** → **仍要运行**，即可启动。
- 这是未签名导致的，仅影响安装体验，不影响功能。

启动后若要用真实 AI，在 DevThink 内点 **AI 设置** 填入 DeepSeek 的 `baseUrl` + `apiKey`（因为 `.devthinkrc` 没随仓库走，需在软件内重填，或用文本编辑器在软件目录放一份 `.devthinkrc`）。

---

## 常见问题

| 现象 | 原因与解决 |
|------|-----------|
| `git check-ignore .devthinkrc` 无输出 | `.gitignore` 没生效或文件被强制添加。执行 `git rm --cached .devthinkrc`（若已误加），确认 `.gitignore` 含 `.devthinkrc` 后再 `git add` |
| 推送被拒 `failed to push` | 远程仓库非空（你勾了初始化 README）。解决：`git pull origin main --allow-unrelated-histories` 后再推；或删掉远程仓库重建时别勾选项 |
| Actions 跑失败 `ELECTRON_MIRROR` 相关 | 正常应已配镜像。若仍失败，去 workflow run 日志看具体错误；多半是 GitHub 偶发限流，重试一次即可 |
| Windows 包双击被 SmartScreen 拦 | 未签名，点"仍要运行"。正式分发需 Authenticode 证书（见下） |
| 只想要 Windows 包，不想打 Mac/Linux | 改 `.github/workflows/build.yml` 的 `matrix.os` 只留 `windows-latest` 即可省时间 |

---

## 关于"正式分发"：代码签名

当前产出的包是**未签名**的，原因和 Mac 版一样——没有开发者证书。

- **Windows 正式分发**需要 **Authenticode 代码签名证书**（如 Sectigo / DigiCert，年费几百到上千元）。
- 拿到证书后，在 `package.json` 的 `build.win` 里加 `certificateFile` / `certificatePassword`，重新跑 Actions 即可自动签名，SmartScreen 不再拦。
- **macOS 正式分发**需要 **Apple Developer ID** 账号做签名 + 公证（同上，配 `build.mac` 的 `identity` 与 `notarize`）。

证书需你自己的开发者资质申请，无法代申请；配好后在 CI 里用 GitHub Secrets 注入，不写进代码。

---

## 相关文档

- 换机迁移（含 `.devthinkrc` 携带方式）：见 `MIGRATION.md`
- 工程说明与本地运行：见 `README.md`
