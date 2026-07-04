# GS 写法指南

英文版: [style.en.md](style.en.md)

本文档是 `gs/SPEC.md` 的实践版补充。
它说明如何把 GS 写得更清楚、更稳定，也更适合放进 GPM 包里。

## 推荐写法

- 一行写一条命令。
- 运行时命令尽量使用 4 个大写字母。
- 长命令名只给宿主指令或扩展用，比如 `INSTALLROOT`。
- 参数之间用逗号分隔。
- 带空格、逗号或注释前缀的值要加引号。
- 包脚本尽量保持相对路径。

## 常见形式

```gs
SETV NAME = World
LOGS INFO, Hello %@NAME%
```

```gs
if %@ARCH% == x64:
  LOGS INFO, x64 build
elif %@ARCH% == arm64:
  LOGS INFO, arm64 build
else:
  LOGS INFO, other build
```

```gs
FUNC setup
  LOGS INFO, preparing

CALL setup
```

## 包脚本

包脚本通常按这个顺序写：

- `PREI` / `PREINST` 用来做准备。
- `INST` / `INSTALLING` 用来做主体安装。
- `POST` / `POSTINST` 用来做收尾和注册。

阶段块适合包生命周期动作，`FUNC` 适合放可复用的小逻辑。

## 命名与布局

- 需要跨多个步骤保存的值，尽量起清晰一点的变量名。
- 小的复用逻辑放进 `FUNC` 块，不要到处复制命令。
- 日志短提示用 `LOGS`，方便打包和排错。
- 除非能让逻辑更清楚，否则不要过度缩进。

## 和规范的关系

如果写法和行为冲突，以 `gs/SPEC.md` 为准。
这份文档只是实践建议，不是强制规范。
