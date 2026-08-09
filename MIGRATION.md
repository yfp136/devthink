# DevThink 换机迁移指南

适用场景：从旧 Mac → 新 Mac（**Apple Silicon / M 系列**），**带源码继续开发**。
当前已打好的 `DevThink-0.1.0-arm64.dmg` 在新 Mac 上可直接安装（无需重打包）。

---

## 一、旧 Mac 打包（终端执行）

排除 `node_modules/`、`dist*/`、`release/`、`output/` 等体积大且可重建的目录，新机用 `npm install` 重建即可。

**方式 A（含 DeepSeek key，最省事）** —— 打包含 `.devthinkrc`，新机装好直接能用 AI：

```bash
cd /Users/yxiao88/WorkBuddy/2026-08-09-10-38-23
tar -czf ~/Desktop/devthink-migrate.tar.gz \
  --exclude='DevThink/node_modules' \
  --exclude='DevThink/dist*' \
  --exclude='DevThink/release' \
  --exclude='DevThink/output' \
  --exclude='*.timestamp-*.mjs' \
  DevThink
```

**方式 B（key 不进包，更安全）** —— 额外排除 `.devthinkrc`，新机在「AI 设置」里重填 key：

```bash
cd /Users/yxiao88/WorkBuddy/2026-08-09-10-38-23
tar -czf ~/Desktop/devthink-migrate.tar.gz \
  --exclude='DevThink/node_modules' \
  --exclude='DevThink/dist*' \
  --exclude='DevThink/release' \
  --exclude='DevThink/output' \
  --exclude='DevThink/.devthinkrc' \
  --exclude='*.timestamp-*.mjs' \
  DevThink
```

> 若 `tar` 报 `--exclude` 不支持（极少数老旧 BSD tar），改用：
> `COPYFILE_DISABLE=1 tar -czf ~/Desktop/devthink-migrate.tar.gz -X <(echo "DevThink/node_modules\nDevThink/dist*\nDevThink/release\nDevThink/output\nDevThink/.devthinkrc\n*.timestamp-*.mjs") DevThink`

---

## 二、传输到新 Mac

用 **U盘 / 移动硬盘 / 隔空投送 / 网盘 / 微信文件传输助手** 把 `~/Desktop/devthink-migrate.tar.gz` 传到新 Mac。

---

## 三、新 Mac 恢复（终端执行）

```bash
# 1. 解压到任意工作目录
cd ~/WorkBuddy            # 或你习惯的目录
tar -xzf ~/Downloads/devthink-migrate.tar.gz   # 按实际下载位置改路径
cd DevThink

# 2. 配置 Node 环境（二选一）
# 方式一：装 nvm（推荐，长期省心）
#   curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.7/install.sh | bash
#   nvm install 22 && nvm use 22
# 方式二：新机也装了 WorkBuddy，直接用自带 Node（把下面一行加进 ~/.zshrc）
#   echo 'export PATH="$HOME/.workbuddy/binaries/node/versions/22.22.2/bin:$PATH"' >> ~/.zshrc
#   source ~/.zshrc

# 3. 重装依赖（会自动下载 Electron 二进制，npmmirror 镜像加速）
ELECTRON_MIRROR=https://npmmirror.com/mirrors/electron/ npm install

# 4. 起预览服务（保持后台运行）
npm run dev

# 5. 另开一个终端，起 Electron 桌面端
npm run electron:dev
```

---

## 四、新 Mac 首次可能遇到的坑（提前给解法）

| 现象 | 解法 |
|------|------|
| Electron 弹「无法检查是否含恶意软件 / 已损坏」 | 先清隔离：`xattr -cr node_modules/electron/dist/Electron.app`；若仍拦，重新签名：`codesign --force --deep --sign - node_modules/electron/dist/Electron.app`；或系统设置 → 隐私与安全性 → 安全性 → 点「仍要打开」 |
| `npm: command not found` | 确认第二步 Node 路径已写入 `~/.zshrc` 并执行 `source ~/.zshrc` |
| 端口 5174 被占用 | 开发端口已固定为 5174 且 `strictPort: true`，被占用时 `npm run dev` 会直接报错退出（而非白屏）。改端口需**两处同步**：`vite.config.js` 的 `server.port` 与 `electron/main.js` 里的 `localhost:5174`，保持一致即可 |
| AI 没反应 / 走 Mock | 确认 `.devthinkrc` 已随包带来（方式 A）；或打开 DevThink「AI 设置」手动填 DeepSeek key |
| 窗口白屏 | Electron 桌面端依赖 `localhost:5174` 的预览服务（仅 `ELECTRON_DEV=1` 开发模式），先确认 `npm run dev` 已起且终端显示 `5174`；必要时重启 Electron |

---

## 五、只装来用（可选，与源码模式不冲突）

旧机的 `DevThink/release/DevThink-0.1.0-arm64.dmg` 也能直接拷到新 Mac 双击安装（Apple Silicon 通用）。
装好后打开 DevThink →「AI 设置」→ 填入 DeepSeek key 即可。未签名包首次打开需走「仍要打开」解锁。

---

## 六、安全提示

- `.devthinkrc` 含 DeepSeek key。**方式 A 的压缩包含 key**，传到新 Mac 后可删除压缩包副本。
- 旧 Mac 退役前，建议到 DeepSeek 后台**轮换/作废该 key**，避免泄露。
- 解压后若目录里残留 `*.timestamp-*.mjs`（Vite 临时缓存），可直接删除，不影响运行。
