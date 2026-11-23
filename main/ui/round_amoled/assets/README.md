# 小安圆屏角色素材

[xiaoan_source.png](xiaoan_source.png) 从经过确认的小安概念图生成，保留薄荷绿单弯耳、奶白脸和黄色领巾。上排从左到右为待机、眨眼、倾听，下排为思考、播报、全身问候。

固件内嵌 [xiaoan_sprites.rgb565](xiaoan_sprites.rgb565)。六帧按上面的顺序依次存储，每帧为 224 × 188、行优先、小端 RGB565，透明区域预合成到纯黑。运行时 LVGL 直接绘制 Flash 中的像素。源 PNG 供后续美术编辑，烧录镜像仅包含 RGB565 素材。

在仓库根目录用安装了 Pillow 的 Python 重新打包。

```sh
python3 main/ui/round_amoled/assets/pack_xiaoan_sprites.py \
  main/ui/round_amoled/assets/xiaoan_source.png \
  main/ui/round_amoled/assets/xiaoan_sprites.rgb565
```
