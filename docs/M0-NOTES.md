# M0 — 調査でわかったこと

## 環境

- ESP-IDF は Windows 側 (`C:\Users\lausiv1024\esp\v5.4.2`)、ツールは `C:\Espressif`。
- `tools/idf.sh` で **WSL から Windows の idf.py を呼べる**(`export.bat` が PATH 上の `python` からvenv名を決めるため、Python 3.11 を明示的に PATH 先頭に置く必要がある)。フラッシュとモニタは COM9 が WSL から見えないので Windows 側で行う。

## LVGL の持ち方

- `third_party/lvgl` に v9.2.2 を submodule で vendoring。
- `third_party/lv_conf.h` を **実機とシミュレータで共有**。`tools/gen_lv_conf.py` が LVGL の `lv_conf_template.h` から生成する(アンカー文字列が見つからなければ失敗するので、LVGL を上げたときに黙って古い設定のままになることがない)。
- ESP-IDF 側: `EXTRA_COMPONENT_DIRS=third_party` により、ローカルの `lvgl` コンポーネントが component manager 版を**上書きする**ことを確認済み(ビルドログの component path が `third_party/lvgl` を指す)。`LV_KCONFIG_IGNORE` を全体に定義し、LVGL の Kconfig を無効化して設定を一本化。

## ESP-IDF 5.4.2 では公式 BSP が使えない

`espressif/m5stack_tab5` は全バージョン(1.0.0〜1.2.0~1)が `usb: "^1"` に **public 依存**している。そして `espressif/usb` は **1.0.0 まで遡っても全バージョンが `usb_dwc_hal_set_fifo_config()` / `usb_dwc_hal_fifo_config_is_valid()` を呼ぶ**。これらは IDF 5.5 以降の HAL にしか存在しない。

```
managed_components/espressif__usb/src/hcd_dwc.c:1306:10:
  error: implicit declaration of function 'usb_dwc_hal_set_fifo_config'
```

BSP の `idf_component.yml` は `idf: ">=5.4"` と宣言しているが、**実際には IDF 5.5 以上が必要**(BSP 側の宣言ミス)。`bsp_usb.c` が BSP のソースに含まれるため、USB だけを切り離すこともできない。

→ **ESP-IDF 5.5.4 に上げる**方針で合意。副次的な利点として、ティアリング対策と PPA 統合を持つ `esp_lvgl_adapter`(IDF >= 5.5 必須)も使えるようになる。

## 実機の実測結果(M0 完了)

ESP-IDF 5.5.4 / LVGL 9.5.0 / BSP 1.2.0~1 で実機が動作。

| 項目 | 結果 |
|---|---|
| 基板リビジョン | **rev1**(`ili9881c: ID1: 0x98, ID2: 0x81, ID3: 0x5c`)+ GT911 タッチ |
| 解像度 | `display is 1280x720 after rotation (panel native is 720x1280)` |
| 回転 | **PPA(ハードウェア)**。`LVGL: Setting PPA context for SW rotation` |
| リフレッシュ | **29 refr/s**(LVGL の `LV_DEF_REFR_PERIOD=33` に張り付き = 上限) |
| リフレッシュ時間 | 平均 **0.16ms** / 最大 **1.5ms**(部分再描画時)。**43ms**(起動直後の全画面 1 回) |
| タッチ | 動作。タップ → ヒットテスト → setState → 再ビルド → 再描画が通る |
| メモリ | ディスプレイ初期化後、内部 SRAM 202KB 空き / PSRAM 28.7MB 空き |

**フレームワークの実機コスト:**

| | 時間 |
|---|---|
| 初回ビルド(lv_obj 34 個の生成込み) | 29 ms |
| **以降の再ビルド**(木全体 + レイアウト + LVGL 差分書き込み) | **1.1 〜 2.5 ms** |

Widget 88 個 / lv_obj 34 個 / アリーナ 3.8KB で一定(リークなし)。30fps のフレーム予算 33ms に対して
再ビルドが 1〜2.5ms なので、**「dirty なら木全体を作り直す」設計は実機の数字で妥当**と確認できた。

### 詰まった点

- **チップリビジョン**: この基板の P4 は **rev v1.0** だが、IDF 5.5 の最小リビジョン既定値は **v3.1**。
  ブートローダが起動を拒否する。v3.0 未満と v3.x はハードウェアが大きく異なり IDF が排他選択を要求するので、
  `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` + `CONFIG_ESP32P4_REV_MIN_100=y` が必要。
- **描画バッファ**: `esp_lvgl_port` は描画バッファを内部 DMA 可能 SRAM に取る。内部 SRAM は 428KB 空きだが
  最大ブロックは 248KB しかないので、`BSP_LCD_DRAW_BUF_HEIGHT=100`(720×100×2 = 144KB × 2 枚)は
  2 枚目が確保できず `bsp_display_start()` が assert してブートループする。**50 に下げれば通る**(72KB × 2 枚)。
- **COM ポート**: VS Code の `idf.portWin` は COM9 だが、それは **Bluetooth のシリアルポート**。
  Tab5 は **COM7**(USB シリアルデバイス)。
- **シリアルログ**: `idf.py monitor` は TTY を要求するので WSL から使えない。`tools/serial_capture.py` を使う。
  P4 は USB-Serial-JTAG なので、(a) ポートを開くと DTR/RTS が立ってダウンロードモードに落ちる、
  (b) リセットすると USB が再列挙されてハンドルが死ぬ、の 2 つの罠がある。

## 画面の向き(M0 の最大リスクだった。解決済み)

- パネルのネイティブ解像度は **720(H) x 1280(V) の縦**(`BSP_LCD_H_RES=720`, `BSP_LCD_V_RES=1280`)。
- BSP の `bsp_display_start()` は `esp_lcd_panel_mirror(false,false)` / `swap_xy=false` で縦のまま初期化し、`flags.sw_rotate = true` を渡している。つまり**横向きは回転を通して実現される**。
- `esp_lvgl_port` に `CONFIG_LVGL_PORT_ENABLE_PPA`(`depends on SOC_PPA_SUPPORTED`)があり、**画面回転を P4 の PPA にオフロードできる**。`sdkconfig.defaults` で有効にしてあり、**実機で有効に動作することを確認済み**。
- 部分再描画のリフレッシュが平均 0.16ms で済んでいるので、**回転はボトルネックになっていない**。ソフト回転との比較は必要になっていないが、`CONFIG_LVGL_PORT_ENABLE_PPA` を menuconfig で切れば同じログで比較できる。

## シミュレータ

- `build-sim/fmsui_sim` — SDL2 ウィンドウ(マウスが指の代わり)。WSLg で動く。
- `build-sim/fmsui_sim --shot out.png` — ヘッドレスで PNG を吐く。ディスプレイサーバ不要で、実機と同じ LVGL パイプラインを通る。1280x720 で正しくレンダリングされることを確認済み。
