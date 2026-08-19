# FmsLikeUI

M5Stack Tab5 (ESP32-P4) 向けの、Airbus FMS 風デザインの宣言的 UI フレームワーク。

Flutter と同じ 3 層(Widget → Element → RenderObject)と制約ベースのレイアウトを C++20 で実装し、
描画・フォント・部分再描画・タッチは LVGL 9 に任せています。フレームワークは ESP-IDF に依存しないので、
**実機と PC シミュレータで UI コードが 1 行も変わりません**。

```cpp
class PerfPageState : public State<PerfPage> {
  int v1_ = 153;

  Widget *build(BuildContext &ctx) override {
    return new Row{{
      .cross = CrossAxis::Center,
      .spacing = 10,
      .children = {
        new Text{{ .text = "V1", .color = kLabel }},
        new GestureDetector{{
          .on_tap = [this] { setState([&] { v1_ += 1; }); },
          .child = new Container{{
            .padding = EdgeInsets::symmetric(12, 0),
            .color = kSurface,
            .border_color = kBorder,
            .border_width = 1,
            .child = new Text{{ .text = fmt("%d", v1_), .color = kCyan }},
          }},
        }},
      },
    }};
  }
};
```

## ビルドと実行

### シミュレータ(WSL / Linux)

```bash
cmake -S sim -B build-sim -G Ninja
cmake --build build-sim

./build-sim/fmsui_sim                        # SDL ウィンドウ。マウスが指の代わり
./build-sim/fmsui_sim --shot out.png         # ヘッドレスで PNG を吐く(表示不要)
./build-sim/fmsui_sim --tap 100,97 --shot out.png   # タップを合成してから撮る
./build-sim/fmsui_sim --demo pages           # ACTIVE/PERF + ACTIVE/INIT(既定)
./build-sim/fmsui_sim --demo catalog         # FMS ウィジェット全部
./build-sim/fmsui_sim --demo reorder         # キー付き再配置(KEYS を切るとどう壊れるかが見える)
./build-sim/fmsui_sim --demo reorder --rows 40 --stats   # 実機ページと同規模で並べ替えコストを測る
./build-sim/fmsui_sim --demo m1              # コア/テーマのデモ
./build-sim/fmsui_sim --demo m0              # 素の LVGL のプローブ画面

# タップ列を打てる。MCDU の入力フローをまるごと検証する例:
./build-sim/fmsui_sim --demo catalog \
  --tap 850,268 --tap 1016,268 --tap 1181,268 \
  --tap 119,150 --shot out.png              # キーパッドで 123 → V1 に確定
```

`--tap` はヒットテストから setState、再ビルド、再描画までを実際に通すので、
「スクリーンショットは正しいが操作すると壊れる」を捕まえられます。複数指定すれば操作の連鎖を再現できます。

### テスト

```bash
cmake -S sim -B build-asan -G Ninja -DFMSUI_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan
./build-asan/fmsui_test        # レイアウト計算と差分検出。ASan/UBSan 下で実行
```

### 実機(M5Stack Tab5)

**ESP-IDF 5.5 以上が必要です。** 公式 BSP が依存する `espressif/usb` は IDF 5.5 の HAL API を
呼ぶため、5.4.2 ではビルドが通りません(詳細は [docs/M0-NOTES.md](docs/M0-NOTES.md))。

```bash
tools/idf.sh set-target esp32p4
tools/idf.sh build                    # WSL から Windows 側の idf.py を呼ぶ
tools/idf.sh -p COM7 flash            # 書き込みも WSL から通る
python3 tools/serial_capture.py COM7  # シリアルログ(idf.py monitor は TTY 必須で使えない)
```

**ポートは COM7 です**(USB シリアルデバイス)。VS Code の `idf.portWin` が COM9 になっていることが
ありますが、COM9 は Bluetooth のシリアルポートです。

実機の実測値は [docs/PERF.md](docs/PERF.md)、ブリングアップで踏んだ罠は
[docs/M0-NOTES.md](docs/M0-NOTES.md) にあります。

## 構成

| ディレクトリ | 中身 |
|---|---|
| `components/fmsui/` | フレームワーク本体。LVGL にのみ依存し、ESP-IDF には依存しない |
| `components/fmsui/src/arena.cpp` | Widget 用のバンプアロケータ |
| `components/fmsui/src/element.cpp` | 差分検出(Element ツリー) |
| `components/fmsui/src/render.cpp` | レイアウトと LVGL への描画 |
| `demo/` | 実機とシミュレータで共有するデモ画面 |
| `sim/` | SDL2 シミュレータ + ヘッドレス PNG 出力 |
| `main/` | 実機のエントリポイント(BSP 初期化 + 回転 + 性能ログ) |
| `components/fmsui_fonts/` | B612 Mono のビットマップフォント(生成物)|
| `assets/fonts/` | 元の TTF |
| `third_party/lvgl` | LVGL v9.5.0(submodule)。`third_party/lv_conf.h` を実機とシムで共有 |
| `tests/` | レイアウトと差分検出のユニットテスト |
| `tools/gen_lv_conf.py` | `lv_conf.h` を LVGL のテンプレートから生成 |
| `tools/gen_fonts.py` | TTF を LVGL のビットマップフォントに変換 |
| `tools/serial_capture.py` | 実機のシリアルログを取る |
| `tools/idf.sh` | WSL から Windows の ESP-IDF を叩く |

| ドキュメント | 中身 |
|---|---|
| [docs/DESIGN.md](docs/DESIGN.md) | 設計と、Flutter とあえて違えた点、踏んだバグ |
| [docs/PERF.md](docs/PERF.md) | 実機の実測値と最適化(失敗した実験も) |
| [docs/M0-NOTES.md](docs/M0-NOTES.md) | ブリングアップで判明した事実(IDF/BSP/COM ポート等) |
| [docs/PLAN.md](docs/PLAN.md) | 当初の計画 |

## フォント

**B612 Mono** — Airbus が ENAC と共同で、コックピット表示のために作らせた書体(SIL OFL 1.1)。
`tools/gen_fonts.py` が `assets/fonts/*.ttf` を LVGL のビットマップフォントに変換します。

```bash
npm --prefix tools install lv_font_conv   # 一度だけ
python3 tools/gen_fonts.py                # 16/20/24/28/32px, 4bpp
```

選定の経緯と、比較した他候補(JetBrains Mono / IBM Plex Mono / Martian Mono)は
[docs/DESIGN.md](docs/DESIGN.md) を参照。

## ライセンス

- LVGL: MIT
- B612 / B612 Mono: SIL OFL 1.1 — `licenses/` に著作権表示と OFL 全文を同梱すること
