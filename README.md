# 剪贴板历史 (Clipboard History Manager)

Windows 原生剪贴板历史管理工具，C++ / Win32 + GDI+ + SQLite 开发，编译成单个 EXE，零运行时依赖。

## 功能

- ✅ 自动记录复制的内容：文字、图片、文件
- ✅ 保存 30 天历史，超期自动清理
- ✅ 卡片式展示，最新记录排最前
- ✅ 关键词搜索历史
- ✅ 类型筛选：全部 / 文本 / 图片 / 文件
- ✅ 日期筛选：全部 / 今天 / 昨天 / 最近 7 天
- ✅ 置顶 / 删除单条、一键清空全部
- ✅ 双击卡片复制回剪贴板，Ctrl+V 即可粘贴
- ✅ 右键卡片预览完整内容（文本 / 原图）
- ✅ 图片预览支持滚轮缩放、拖动、Esc / 右键关闭
- ✅ 系统托盘常驻，Alt+C 呼出 / 隐藏窗口
- ✅ 开机自启选项（托盘右键菜单）
- ✅ 单实例运行，重复启动会唤出现有窗口

## 构建

### 前提条件
- Windows 10/11
- Visual Studio 2022（Community 版即可，自带 CMake ≥ 3.16）

### 方法一：一键构建（推荐）
1. 在文件资源管理器中打开 `clipboard-history` 目录
2. **双击 `run_build.bat`**（自动配置 VS 环境并编译）

生成的可执行文件：`clipboard-history\build\Release\ClipboardHistory.exe`

> 提示：请直接在资源管理器里双击运行，不要在 Git Bash 里执行 `build.bat`。
> `build.bat` 里的 `>nul` 在 cmd.exe 下是重定向到空设备；在 bash 下会被当成普通文件，
> 误生成一个叫 `nul` 的文件（`nul` 是 Windows 保留设备名，会导致 `git add` 报错）。

### 方法二：命令行构建
打开 "Developer Command Prompt for VS 2022"，进入项目目录：

```bat
cd /d d:\aaa\clipboard-history
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## 使用

1. 运行 `build\Release\ClipboardHistory.exe`
2. 程序常驻系统托盘
3. 正常使用 Ctrl+C 复制内容（文字 / 图片 / 文件），自动记录
4. **Alt+C** 呼出 / 隐藏窗口查看历史
5. 双击任意卡片 → 再到目标处 Ctrl+V 粘贴

### 托盘菜单
- **显示窗口** — 打开主界面
- **开机自启** — 勾选后随系统启动
- **退出** — 完全关闭程序

### 卡片操作
- 点击 **📌** — 置顶 / 取消置顶
- 点击 **✕** — 删除此条记录
- 双击卡片 — 复制到剪贴板
- 右键卡片 — 弹出完整预览（文本 / 原图）
- 底部 **清空全部** — 删除所有历史

### 筛选
- 顶部搜索框 — 按关键词搜索
- 类型按钮 — 全部 / 文本 / 图片 / 文件
- 日期按钮 — 全部 / 今天 / 昨天 / 最近 7 天

## 数据文件

- 数据库位置：`%APPDATA%\ClipboardHistory\history.db`
- 配置（窗口位置、开机自启等）：注册表 `HKCU\Software\ClipboardHistory`

## 技术栈

| 组件 | 技术 |
|------|------|
| GUI | Win32 API + GDI+ |
| 数据库 | SQLite 3（amalgamation，静态编译进 EXE）|
| 构建 | CMake + MSVC |

## 项目结构

```
clipboard-history/
├── CMakeLists.txt            # 构建配置
├── run_build.bat             # 一键构建（双击运行，走 PowerShell）
├── build.ps1                 # run_build.bat 调用的 PowerShell 构建脚本
├── build.bat                 # 备选构建（NMake，需在开发者命令提示符里跑）
├── gen_icon.py               # 生成 res/app.ico 的脚本（纯标准库）
├── resource.rc / resource.h  # Windows 资源（图标、版本信息、应用清单）
├── src/
│   ├── main.cpp              # 入口、窗口、托盘、热键、单实例
│   ├── database.cpp/h        # SQLite CRUD 操作
│   ├── clipboard_monitor.cpp/h  # 剪贴板监听与读写
│   └── card_panel.cpp/h      # 卡片列表 UI（搜索框 + 筛选 + 卡片绘制）
├── vendor/
│   ├── sqlite3.c             # SQLite 源码（编译进 EXE）
│   └── sqlite3.h
└── res/
    ├── app.ico               # 应用图标（由 gen_icon.py 生成）
    └── app.manifest          # 应用清单（comctl32 v6 主题 + Per-Monitor V2 DPI）
```
