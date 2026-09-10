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
./build-sim/fmsui_sim --demo fplan           # ACTIVE/F-PLN。窓送り(スクロールしない)
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
ctest --test-dir build-asan --output-on-failure   # 両方を ASan/UBSan 下で実行
```

実行ファイルは 2 つに分かれています。

| 実行ファイル | 見ているもの |
|---|---|
| `fmsui_test` | レイアウト計算、差分検出、統計とスタイルキャッシュ。表示も入力も持たない |
| `fmsui_interaction_test` | LVGL のポインタ入力から始まる操作の連鎖。専用の display と indev を持つ |

分けてあるのは、LVGL の display / input device / `FmsApp` singleton の状態を
レイアウトのテストから隔離するためです。失敗したときに「計算が壊れた」のか
「入力の連鎖が壊れた」のかが、どちらが赤くなったかで分かります。

### 実機(M5Stack Tab5)

**ESP-IDF 5.5 以上が必要です。** 公式 BSP が依存する `espressif/usb` は IDF 5.5 の HAL API を
呼ぶため、5.4.2 ではビルドが通りません(詳細は [docs/M0-NOTES.md](docs/M0-NOTES.md))。

Windows 側から `tools\idf.bat` で叩きます。`export.bat` を通してから `idf.py` を呼ぶだけのもので、
どこから実行してもプロジェクトルートに移動します。

```bat
tools\idf.bat set-target esp32p4
tools\idf.bat build
tools\idf.bat -p COM7 flash
tools\idf.bat -DFMSUI_DEMO=fplan build
tools\idf.bat -DFMSUI_DEMO=reorder -DFMSUI_ROWS=40 build
```

シリアルログは `idf.py monitor` が TTY を要求して使えないので、専用スクリプトを使います。

```bat
python tools\serial_capture.py COM7 20 --no-reset
```

`tools/idf.sh` は同じことを WSL 側からやりますが、**WSL の interop 登録が生きている間しか動きません**。
`systemd=true` の環境では binfmt_misc の `WSLInterop` エントリが定期的に消え、そうなると
`cmd.exe: Exec format error` で止まります。**実機のビルドを確実に通したいときは `idf.bat` を使ってください。**
消えた登録は `sudo sh -c 'echo ":WSLInterop:M::MZ::/init:PF" > /proc/sys/fs/binfmt_misc/register'` で戻せます。

インストール先が違う場合は環境変数で上書きできます。

```bat
set FMSUI_IDF=C:\esp\v5.5.4\esp-idf
set IDF_TOOLS_PATH=C:\Espressif
```

**ポートは COM7 です**(USB シリアルデバイス)。VS Code の `idf.portWin` が COM9 になっていることが
ありますが、COM9 は Bluetooth のシリアルポートです。

VS Code から実機ビルドする場合は、**汎用の CMake Tools 拡張にこのフォルダを触らせないでください**。
ESP-IDF 環境を通さずに cmake を起動するため、`riscv32-esp-elf-gcc` が PATH に無い・ジェネレータが
MSBuild になる・`$ENV{IDF_PATH}` 次第で別の ESP-IDF が読まれる、といった理由で失敗し、しかもその
結果を `build/` に書くので `idf.py` 側のキャッシュまで壊れます。`.vscode/settings.json` で自動構成を
切ってありますが、確実にするなら拡張機能ビューで Disable (Workspace) してください。

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
| `tests/` | ホストのユニットテスト。レイアウトと差分検出、およびヘッドレスの操作テスト |
| `tools/gen_lv_conf.py` | `lv_conf.h` を LVGL のテンプレートから生成 |
| `tools/gen_fonts.py` | TTF を LVGL のビットマップフォントに変換 |
| `tools/serial_capture.py` | 実機のシリアルログを取る |
| `tools/idf.bat` | Windows から ESP-IDF を叩く(実機ビルドの主経路) |
| `tools/idf.sh` | 同じことを WSL から。interop が生きているときだけ動く |

| ドキュメント | 中身 |
|---|---|
| [docs/DESIGN.md](docs/DESIGN.md) | 設計と、Flutter とあえて違えた点、踏んだバグ |
| [docs/DECISIONS.md](docs/DECISIONS.md) | 採用した設計判断と、その実装状況 |
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
