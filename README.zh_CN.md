<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# AI Passport 上的 Codex Buddy

这个分支是 Codex Buddy 在 FoloToy AI Passport 上的第一版。目标板是 ESP32-C3、8 MB Flash、无 PSRAM、240×320、三颗 ADC 按键。启动后直接进入这套界面，不再进入硬件测试菜单。

设备显示 Codex 的 5 小时与 7 天用量、重置倒计时，以及当前工作姿态。电脑上的 Codex 插件启动本地 BLE 桥。需要授权时，可以在设备上允许或拒绝。这条路径不把数据发到额外的服务器。

行为来自 [qinyu/codex-buddy](https://github.com/qinyu/codex-buddy)。屏幕、按键和宠物按 Passport 重新设计。报文、已移植范围和未做项见 [docs/assets/codex-buddy.zh_CN.md](docs/assets/codex-buddy.zh_CN.md)。

## 屏幕

竖屏、深色底：

- 链路状态、工作姿态，以及 Pi 宠物。用量页里按上、下切换姿态。
- `5小时` 与 `7天` 用量条，每条下面有倒计时。5 小时窗口在 3 小时及以下转橙、1 小时及以下转红。7 天窗口在 4 天及以下转橙、2 天及以下转红。
- 顶栏有电量百分比。电量计不可用时显示 `--`。
- 批准卡片显示工具名和提示。配对时显示 6 位配对码。
- 按键提示贴在屏幕右侧，从上到下对准上、下、确定。

## 按键

关机键只负责电源和休眠，不参与界面。短按少于 0.4 秒。长按至少 0.8 秒，松手后才生效。双击是确定键在 400 毫秒内连按两下短按。

| 按键 | 作用 |
| --- | --- |
| 上，短按 | 用量页上一个姿态。信息或设置里上一项。批准请求时允许。 |
| 下，短按 | 下一个姿态或下一项。批准请求时拒绝。关闭配对码。停在解除配对或恢复设置上时，下键不执行。 |
| 确定，短按 | 用量与信息翻页。设置里调一档、开关或执行当前项。批准请求时执行当前高亮。关闭配对码。 |
| 确定，长按 | 进入设置。设置里再长按，回到用量。 |
| 确定，双击 | 任意页面立刻回到用量，包括从设置退出。 |

信息页有名称、按键说明、电量、地址、链路加密、六位配对码和署名，并用中文区分本机 Pi 与经 Herdr 的 Pi。设置会记住亮度、声音总开关和蓝牙开关。无线、指示灯、时钟显示为不适用。解除配对和恢复设置都是短按确定执行，然后回到用量。变暗之后的第一次按键只负责唤醒。有批准请求或配对码时保持背光。没有请求且 45 秒没有按键时，背光变暗。

## 音效

设置里的声音开关打开时，ES8311 会发声。普通短按是轻嗒。翻页和进出设置是两音阶轻滑。切换姿态是短促上滑。待批准时重复清晰双音，直到处理完。失败是低沉双音。配对成功和允许是短促上行。拒绝和取消是单低下行。低电是慢三连，只响一次，电量回到 20% 以上后才会再响。

## 主机侧

先安装 Python 包 `bleak`，再信任 `plugins/codex-usage-passport`。`SessionStart`、`UserPromptSubmit`、`PermissionRequest` 三个钩子运行 `scripts/hook_entry.py`，并保持一个桥进程。桥从本机 Codex rollout 日志读取最近的 `token_count`，把一行 JSON 写到 Nordic UART 的 RX 特征。桥不在时，权限钩子不返回决定，Codex 仍走自己的批准提示。

没有 rollout 日志时可以先打印报文：

```bash
python3 tools/codex_usage_ble_bridge.py --manual --state busy --primary 40 --secondary 12 --print-packet
```

广播名是 `Codex-` 加上 MAC 最后两个字节。链路使用 LE Secure Connections，屏幕上显示 6 位配对码。配对记录保存在 NVS。

## 构建

使用 ESP-IDF 5.5.3，运行 `./tools/validate.sh`。从 `0x0` 烧录合并镜像：

```text
build/FoloToy-AI-Passport-full.bin
```

合并烧录可能清掉 NVS，包括 BLE 配对。不要把仅含应用的镜像烧到 `0x0`。

## 中文

固定界面文案使用文泉驿微米黑的 16 px 与 20 px 子集。这些标签不用 Montserrat。字符表、许可和转换命令见 [assets/README.zh_CN.md](assets/README.zh_CN.md)。子集以外的批准提示可能显示缺字方框；桥会把工具文本压成较短的 ASCII。
