# GUI

英文版: [README.en.md](README.en.md)

`gui/` 是 GPM 的原生 Direct2D 前端。

它负责把包列表、安装队列、进度、主题和语言切换这些界面能力组织起来。

## 主要部分

- `main.cpp` - 页面状态、WebSocket 客户端、包列表渲染和用户动作。
- `assets/` - 前端图片和静态资源。
- `lang/` - 本地化文件。
- `themes/` - 主题 JSON 文件。
- `ui_d2d/` - 原生 UI 运行时和控件实现。

## 构建提示

通常使用 `build_mingw.bat` 构建。
如果 `g++` 或 `windres` 不在 `PATH` 里，可以先设置 `MINGW_BIN`。
