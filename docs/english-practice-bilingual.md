# 小安英语练习双语字幕

英语练习回复沿用现有 v1 语音路径。声明 `practice_bilingual_v1` 的设备可在练习的 `tts.start` 中收到英文 `text` 和可选中文 `text_zh`。下发与轮次、练习局和播放流绑定。旧设备仍按现有字幕协议工作。

英文用于播报。设备默认显示英文，「翻译」在同一滚动区域本地展开或收起中文。手动拖动暂停当前字幕的自动滚动，新回复从英文顶部开始显示。重听使用已有音频和字幕。

服务端 [#64](https://github.com/yuyuyulin/AgentSphere/issues/64) 负责双语生成和下发。固件 [#25](https://github.com/yuyuyulin/AgentSphere-esp32/issues/25) 负责滚动、本地展开和真机验收。
