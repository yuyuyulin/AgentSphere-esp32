# 当前交付与验证状态

更新于 2026-11-23。本页记录 **AgentSphere ESP32** 当前的真实工程进度，严格区分「文档定稿」「编译通过」与「真机实测验收」。

## 当前阶段

**阶段 A · HTTPS Bootstrap 与 6 位配对码认领（#3）已完成 1.75C 真机全链路验收并合入 `main`**，下一阶段为 **阶段 A · WSS + Opus 语音主链路（#4）**。

## 模块状态总览

| 阶段 / 模块 | 当前状态 | 已确认证据 | 待验证 / 限制 |
| --- | --- | --- | --- |
| 产品功能需求（[requirements.md](requirements.md)） | 已定稿 | 明确 C 端随身定位、本地 SoftAP 配网（不向用户暴露服务器地址）、6 位配对码认领、10 态状态机、按键/IMU、圆屏全简体中文界面与 2 层交互；已确定角色名「小安」与主唤醒词「小安同学」 | 电池休眠/关机时长及唤醒阈值待真机实测定值 |
| 端云通信协议（[protocol.md](protocol.md)） | v1 草案已定稿 | 对齐 AgentSphere 平台契约，定义 HTTPS 引导/认领/OTA 与 WSS + Opus 语音信令；HTTPS Bootstrap 与认领接口已在真机与已部署服务端联调通过 | 待 AgentSphere 服务端补齐 `WSS /device/v1/connect` 语音通道后端到端联调 |
| 固件技术架构（[architecture.md](architecture.md)） | 已定稿 | 已实测 `waveshare/esp32_s3_touch_amoled_1_75c@3.0.0` + `lvgl@9.6.0` + `esp_codec_dev@1.5.11` 在 ESP-IDF `v6.1` 下解析并编译通过；已将 `mbedtls` 动态内存迁至 PSRAM 并预留 64KB 内部 DMA 池，彻底解决 HTTPS 握手与 QSPI AMOLED 刷新并发时的 DMA OOM | `esp-sr >= 2.6` 与 Opus 组件将在后续阶段引入并实测 |
| 开源治理与 GitHub 协作 | 已就绪 | MIT 协议、`.gitignore`（含 `sdkconfig.local` 隔离生产构建地址）、`AGENTS.md`、`CONTRIBUTING.md`、Issue/PR 模板、13 个仓库标签与基础 CI 均已生效 | 后续可补充云端 ESP-IDF 自动编译 CI |
| Step 0 · 1.75C 工程骨架与真机验板（#1） | 真机验收通过 | ESP-IDF `v6.1`（`esp32s3`）构建通过（约 735 KB，`ota_0` 余量 82%）；真机实测通过：I2C 5 芯片（ES8311/AXP2101/ES7210/CST9217/QMI8658）全部在线、466×466 AMOLED 显示与触控光圈/开心表情正常、开机提示音与 `BOOT` 录音回放可出声、AXP2101 充放电与电量（`93% / 4107mV`）及 IMU 读数正常、8MB PSRAM（余 8095 KB）与 SRAM（余 252 KB）稳定；确认 1.75C 无外置 PCF85063 并已自动回退 ESP32-S3 内置 RTC，同时将默认音量提至 95% 改善音量偏小问题 | 后续阶段 A/B 继续调优实际语音播报响度与 IMU 体感阈值 |
| 阶段 A · 本地 SoftAP 配网与多 Wi-Fi（#2） | 手机配网真机复测通过 | 2026-11-23 用户在微雪 1.75C 重测确认手机配网正常。串口观察到客户端加入热点并获得 DHCP 地址。验收固件 `62db356` 使用 ESP-IDF `v6.1`（`esp32s3`）构建通过，固件 `0x159820`，`ota_0` 余量 66%。 | 手机自动弹窗、重启自动回连、离线跳过、多网络切换和退出后的内存回收暂缺独立的验收记录。 |
| 阶段 A · HTTPS Bootstrap 与设备认领（#3） | 真机验收通过 | ESP-IDF `v6.1`（`esp32s3`）零警告编译通过（`0x19a2d0` / 约 1.60 MB，`ota_0` 余量 60%）；微雪 1.75C 真机与已部署 AgentSphere 服务端全链路实测通过：HTTPS TLS 根证书包校验通过、`server_time_ms` 同步 RTC、首次下发 6 位配对码与 1Hz 实时倒计时、Web 认领后持久化 `device_token` 且硬重启自动恢复绑定态、服务端解绑后重启收到 `401 REAUTH_REQUIRED` 自动清凭据并重新出码、点击「刷新配对码」立即申请新码、全界面简体中文（思源黑体 18px 开源子集字库，零缺字、零花屏、零 DMA 报错） | 在线态实时感知服务端解绑将在阶段 A 接入 `WSS` 长连接（#4）后支持 |
| 阶段 A · WSS + Opus（#4） | 已解锁，待开工 | 前置设备认领（#3）已完成真机验收。 | 依赖 AgentSphere 服务端部署 `WSS /device/v1/connect` 语音网关 |
| 阶段 B · V1.0 日常可用（唤醒/表情/电源/OTA） | 未开始 | — | 依赖阶段 A |
| 阶段 C · 极简英语口语练习场景 | 未开始 | — | 依赖阶段 B 与服务端场景隔离支持 |
