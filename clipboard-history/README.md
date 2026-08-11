# 剪贴板历史 (Clipboard History Manager)

Windows 原生剪贴板历史管理工具，C++ / Win32 开发，轻量无依赖。

## 功能

- ✅ 自动记录复制的文字和图片
- ✅ 保存 30 天历史，超期自动清理
- ✅ 卡片式展示，最新记录排最前
- ✅ 搜索历史记录
- ✅ 置顶 / 删除条目
- ✅ 双击卡片复制回剪贴板，Ctrl+V 即可粘贴
- ✅ 系统托盘常驻，Alt+C 呼出/隐藏窗口
- ✅ 开机自启选项（托盘右键菜单）
- ✅ 单实例运行，重复启动会唤出现有窗口

## 构建

### 前提条件
- Windows 10/11
- Visual Studio 2022（Community 版即可）
- CMake ≥ 3.16（VS 自带）

### 编译步骤

**方法一：双击构建**
1. 在文件资源管理器中打开本项目目录
2. 双击 `run_build.bat`

**方法二：命令行构建**
1. 打开"Developer Command Prompt for VS 2022"
2. 进入项目目录：
   ```
   cd /d d:\aaa\clipboard-history
   cmake -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```
3. 生成的可执行文件：`build\Release\ClipboardHistory.exe`

## 使用

1. 运行 `ClipboardHistory.exe`
2. 软件会在系统托盘显示图标
3. 正常使用 Ctrl+C 复制内容，自动记录
4. **Alt+C** 呼出窗口查看历史
5. 双击任意卡片 → Ctrl+V 粘贴

### 托盘菜单
- **显示窗口** — 打开主界面
- **开机自启** — 勾选后随系统启动
- **退出** — 完全关闭程序

### 卡片操作
- 点击 **📌** — 置顶/取消置顶
- 点击 **✕** — 删除此条记录
- 双击卡片 — 复制到剪贴板

## 数据文件

- 数据库位置：`%APPDATA%\ClipboardHistory\history.db`
- 配置（窗口位置、开机自启等）：注册表 `HKCU\Software\ClipboardHistory`

## 技术栈

| 组件 | 技术 |
|------|------|
| GUI | Win32 API + GDI+ |
| 数据库 | SQLite 3 (amalgamation，静态编译进 EXE) |
| 构建 | CMake + MSVC |

## 项目结构

```
src/
├── main.cpp              # 入口，窗口，托盘，热键，单实例
├── database.cpp/h        # SQLite CRUD 操作
├── clipboard_monitor.cpp/h  # 剪贴板监听与读写
├── card_panel.cpp/h      # 卡片列表 UI（搜索框 + 卡片绘制）
vendor/
├── sqlite3.c             # SQLite 源码（编译进 EXE）
└── sqlite3.h
res/
└── app.ico               # 应用图标
```
