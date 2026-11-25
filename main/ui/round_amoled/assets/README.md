# 小安半身拆件素材

[XiaoAn 原角色图](xiaoan_source.png) 是造型参考。第二版按用户选定的哑光概念制成拆件，使用 Codex 内置 ImageGen 生成 [部件图集](xiaoan_rig_atlas.png) 和[独立躯干](xiaoan_rig_torso.png)。部件按半身构图对齐，使用低反差深青色帽壳、柔和单弯耳、无腮红奶油脸、小椭圆眼和环绕脖子的黄领巾。

图集提示为「以第二版圆屏概念图为准，采用低反差哑光材质与收敛五官，生成三行三列透明部件图集。分别放无眼嘴的头部、躯干、左右臂、睁闭眼与三种嘴型」。独立躯干提示为「按同视角单独生成紧凑的深青色躯干和完整黄色围领，双臂独立绘制」。

使用 Pillow 重新生成固件用的 RGB565A8 数据与尺寸索引。

```sh
python3 main/ui/round_amoled/assets/pack_xiaoan_rig.py
```

[xiaoan_rig.rgb565a8](xiaoan_rig.rgb565a8) 共 158268 字节。每块数据先存小端 RGB565 颜色平面，再存 8 位透明度平面，部件起点对齐到四字节。只有这份像素数据随圆屏固件进入 Flash。原图与透明 PNG 供美术编辑和复现。
