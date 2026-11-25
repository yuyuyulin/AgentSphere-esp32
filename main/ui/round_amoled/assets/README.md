# 小安半身拆件素材

[XiaoAn 原角色图](xiaoan_source.png) 是造型参考。拆件使用 Codex 内置 ImageGen 生成 [部件图集](xiaoan_rig_atlas.png) 和[独立躯干](xiaoan_rig_torso.png)。部件按半身构图对齐，保留单弯耳、奶油色脸和黄色领巾。

图集提示为「沿用参考小安的造型、视角和哑光布料质感，生成透明背景的三行三列图集。分别放无眼嘴的完整头部、躯干、左臂、右臂、睁眼、闭眼、微笑、张嘴和思考嘴型」。独立躯干提示为「按同视角生成单独的圆形躯干和黄色领巾，用于连接可独立旋转的双臂」。

使用 Pillow 重新生成固件用的 RGB565A8 数据与尺寸索引。

```sh
python3 main/ui/round_amoled/assets/pack_xiaoan_rig.py
```

[xiaoan_rig.rgb565a8](xiaoan_rig.rgb565a8) 共 158268 字节。每块数据先存小端 RGB565 颜色平面，再存 8 位透明度平面，部件起点对齐到四字节。只有这份像素数据随圆屏固件进入 Flash。原图与透明 PNG 供美术编辑和复现。
