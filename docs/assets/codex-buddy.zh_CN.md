<p align="right">
  <strong>简体中文</strong> · <a href="codex-buddy.md">English</a>
</p>

# Codex Buddy Passport 说明

本文记录 [qinyu/codex-buddy](https://github.com/qinyu/codex-buddy) 在 Passport 上的第一版移植。它是分支产品说明，不是上游 BSP 契约。

## 已移植

- Nordic UART 服务 `6e400001-b5a3-f393-e0a9-e50e24dcca9e`，广播名是 `Codex-` 加 MAC 两个字节。
- LE Secure Connections、仅显示的配对码，以及清除绑定的 `unpair` 命令。
- Stick 固件已经接受的用量字段：`state`、`tokens`、`primary`、`secondary`、`primary_resets_at`、`secondary_resets_at`、`primary_label`、`secondary_label`、`now`、`agent`。
- 单独的 `prompt` 对象（`id`、`tool`、`hint`）和 `{"prompt":null}`。
- 设备回复 `{"cmd":"permission","id":"...","decision":"accept"|"cancel"}`。
- 工作姿态 `sleep`、`idle`、`busy`、`attention`、`completed`、`dizzy`、`heart`。
- Stick 仪表上的用量条颜色和重置倒计时颜色。
- 本地钩子桥。`PermissionRequest` 在 Unix 套接字上等待，失败时不拦截 Codex 自己的提示。

主机模型沿用 Stick 插件：Codex 钩子启动本地进程，进程再走 BLE。本移植不用 app-server TUI 代理做陪伴通道。Passport 没有那套终端界面，Yu 在 Stick 上用的也是钩子桥。

## 重新设计的部分

屏幕是 `main/codex_ui.c` 里的 240×320 新布局，不用演示菜单、`demo_*.c` 页面或 `ui_pixel` 外壳。宠物是 `main/codex_pet.c` 里的 LVGL 图形，不是 GIF。主页已按纵栈定稿：顶栏是连接和电量，下面一行大字是姿态，中间约半屏是 Pi 宠物，底部是带倒计时的 5 小时和 7 天粗条。信息页和设置页是单列，行高约 34 像素，八项设置能放进一屏。宠物用 Stick 里现成的 Pi 帧，去掉黑底。本机 Pi 和经 Herdr 的 Pi 共用这只宠物，关于页用中文区分来源。没有 IMU，所以晕眩和心动可以由主机报文点亮；本地允许之后，心动还会保持 1.8 秒。

主页面是用量、姿态、信息。确定键依次切换。上下键在信息页里切换关于、按键、设备、蓝牙、署名。长按确定打开或关闭设置。设置把四档亮度和蓝牙开关写入 NVS。声音、无线、指示灯、时钟显示为不适用。解除配对和恢复设置都要再按一次确定。恢复设置只清 `codex` 偏好，不主动删绑定。下键取消确认。有批准卡片时，上键仍是允许，下键仍是拒绝。配对码卡片仍盖在页面上。45 秒没有按键会把背光降下来，下一次按键只负责唤醒。

固定中文使用 `assets/fonts/codex_font_16.c` 和 `assets/fonts/codex_font_20.c`。控件直接选这两套字体。`tests/test_codex_font_coverage.py` 检查界面源码里的每个可打印字符，并确认 U+9F98 不在字库里，这样缺字时测试会失败。

## 未做

- GIF 宠物、文件夹推送和 LittleFS 角色包。
- Stick 的 IMU 手势、午睡计时、升级，以及 Pi、Hermes、Cursor 的多代理汇聚。
- 第三用量条和 OpenCodex 配额客户端。
- 已锁定的视觉布局。信息页和设置页目前是占位列表，等 240×320 的方向选定后再排版。
- 音频、无线、状态灯和时钟旋转。设置里标明不适用，不假装已经实现。

`main/demo_*.c` 仍留在树里，让原有基线主机测试可以继续编译。固件的 CMake 不链接这些页面，`app_main` 也不会打开它们。

## 报文示例

```json
{"state":"busy","tokens":1800,"primary":42,"secondary":15,"primary_resets_at":1775731234,"secondary_resets_at":1776331234,"primary_label":"5h","secondary_label":"7d","now":1775700000,"agent":"CODEX"}
```

```json
{"prompt":{"id":"h1","tool":"Bash","hint":"echo hi"}}
```

链路超过 30 秒没有新行就记为中断。桥在保存的重置时刻已经过去时，按 5 小时或 7 天窗口把时刻向前滚动。

## 安全

RX 必须加密写。TX 通知必须加密订阅。状态应答只有在链路已加密且已认证时才报告 `"sec":true`。工具提示仍会经过这条链路，未配对或配对失败不能当成私密通道。绑定保存在 NVS。从 `0x0` 烧录合并镜像可能清掉该分区。
