# FmsLikeUI

[![CI](https://github.com/Lausiv1024/FmsLikeUI/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/Lausiv1024/FmsLikeUI/actions/workflows/ci.yml)

M5Stack Tab5 (ESP32-P4) 向けの、Airbus FMS 風デザインの宣言的 UI フレームワーク。

Flutter と同じ 3 層(Widget → Element → RenderObject)と制約ベースのレイアウトを C++20 で実装し、
描画・フォント・部分再描画・タッチは LVGL 9 に任せています。フレームワークは ESP-IDF に依存しないので、
**実機と PC シミュレータで UI コードが 1 行も変わりません**。

別のプロジェクトから使う方法(通常の CMake / ESP-IDF の component)と、その利用契約は [docs/USING.md](docs/USING.md) にあります。

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
ctest --test-dir build-asan --output-on-failure   # 3 つすべてを ASan/UBSan 下で実行
```

実行ファイルは 3 つに分かれています。

| 実行ファイル | 見ているもの |
|---|---|
| `fmsui_test` | レイアウト計算、差分検出、統計とスタイルキャッシュ。表示も入力も持たない |
| `fmsui_interaction_test` | LVGL のポインタ入力から始まる操作の連鎖。専用の display と indev を持つ |
| `fmsui_request_frame_test` | `requestFrame()` の高頻度・並行負荷。複数の producer スレッドと実際のフレームループ |

分けてあるのは、LVGL の display / input device / `FmsApp` singleton の状態を
レイアウトのテストから隔離するためです。失敗したときに「計算が壊れた」のか
「入力の連鎖が壊れた」のかが、どちらが赤くなったかで分かります。

負荷試験はスレッド同士を condition variable で待ち合わせるので、壊れると失敗ではなく**停止**します。
CTest の timeout (60 秒) でそれを失敗に変え、他の 2 つを巻き込まないように独立させてあります。

### CI(GitHub Actions)

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) が次の 5 つの job を走らせます。

| job | 起動 | やること |
|---|---|---|
| `host (debug)` | pull request、`master` への push、手動 | Debug ビルド、3 件の CTest、6 デモのヘッドレス描画(空でない PNG が出ること) |
| `host (asan-ubsan)` | 同上 | 同じことを `FMSUI_SANITIZE=ON` で行う。`fmsui` 本体が計装されていることも確かめ、ASan / UBSan / LeakSanitizer の報告を失敗にする |
| `consumer (host)` | 同上 | `sim/`・`demo/`・`tests/`・`fmsui_fonts` を除いたツリーで `consumers/host` を configure・ビルド・実行する。公開ヘッダーの単独コンパイルも含む([docs/USING.md](docs/USING.md)) |
| `device-build (esp32p4)` | `master` への push、手動 | ESP-IDF 5.5.4 の公式コンテナで、既定と `FMSUI_DEMO` の 5 構成をビルドする。LVGL Examples / Demos が 0 件であることを判定し、bin サイズと app 領域の空きを job summary に出す |
| `consumer (esp-idf, esp32p4)` | 同上 | 同じコンテナで、`main/`・`demo/`・BSP・`fmsui_fonts` を除いたツリーから `consumers/esp-idf` をビルドする。ビルドに入った component を検査し、bin サイズを job summary に出す |

警告をエラーにするのはプロジェクトのコード(`components/`、`main/`、`demo/`、`sim/`、`tests/`)だけで、
LVGL、ESP-IDF、managed component には掛けません。手元でも `-DFMSUI_WERROR=ON` で同じ扱いになります
(シムは `cmake -S sim ... -DFMSUI_WERROR=ON`、実機は `tools\idf.bat -DFMSUI_WERROR=ON build`)。
失敗した job だけが、調査用のログと PNG を 7 日間の artifact に残します。

CI の対象外: 実機への書き込みと物理タッチ、PNG の見た目の比較、ThreadSanitizer、長時間の耐久試験、
定期実行、リリース作成。device-build は pull request では走りません。

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

BSP などの managed component の版は、コミットしてある `dependencies.lock` で固定しています。
`main/idf_component.yml` の範囲指定だけに任せると、その日のレジストリの最新が入ります
(2026-09-11 には、ESP-IDF 5.5.4 でコンパイルできない `esp_lvgl_port` 2.9.0 が選ばれました)。
依存を上げるときは `tools\idf.bat update-dependencies` で lock を作り直し、実機で確認してから lock ごとコミットしてください。

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
| `tests/` | ホストのテスト。レイアウトと差分検出、ヘッドレスの操作テスト、`requestFrame()` の並行負荷テスト |
| `consumers/` | フレームワークを外から使うだけのプロジェクト。通常の CMake (`host/`) と ESP-IDF (`esp-idf/`)。利用契約のテスト用で、デモではない |
| `tools/gen_lv_conf.py` | `lv_conf.h` を LVGL のテンプレートから生成 |
| `tools/gen_fonts.py` | TTF を LVGL のビットマップフォントに変換 |
| `tools/serial_capture.py` | 実機のシリアルログを取る |
| `tools/ci/` | CI が呼ぶ検査。デモの描画、サニタイザ計装とその報告、実機ビルド 6 構成と LVGL ソースの内訳、consumer のビルドとその component の内訳 |
| `tools/idf.bat` | Windows から ESP-IDF を叩く(実機ビルドの主経路) |
| `tools/idf.sh` | 同じことを WSL から。interop が生きているときだけ動く |

| ドキュメント | 中身 |
|---|---|
| [docs/USING.md](docs/USING.md) | 別のプロジェクトから使う方法と、利用契約(所有範囲、ライフサイクル、スレッド境界) |
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
