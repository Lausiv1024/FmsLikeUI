# FmsLikeUI — 実装計画

M5Stack Tab5 (ESP32-P4) 向け、Airbus FMS 風デザインの宣言的 UI フレームワーク。

---

## 1. 前提と確定事項

### ターゲット環境

| 項目 | 値 | 根拠 |
|---|---|---|
| SoC | ESP32-P4NRW32 (RISC-V dual 360MHz) | M5 公式仕様 |
| PSRAM | 32MB / HEX mode / 200MHz | M5Tab5-UserDemo の sdkconfig |
| Flash | 16MB | 同上 |
| Display | 5" 1280x720 IPS / MIPI-DSI 2レーン / RGB565 | esp-bsp `m5stack_tab5` |
| Panel/Touch | rev1: ILI9881C + GT911 / rev2: ST7123 (タッチ一体) | BSP が I2C プローブで自動判別 |
| ESP-IDF | **v5.5.4** | BSP の宣言は `>=5.4` だが、依存する `espressif/usb` が IDF 5.5 の HAL を呼ぶため実質 5.5 以上が必須(→ [M0-NOTES](M0-NOTES.md)) |
| BSP | `espressif/m5stack_tab5` ^1.2.0 | Component Registry |
| LVGL | **9.2.2 から開始** | M5 公式ファームと同一。PPA 描画が必要になったら 9.4 へ引き上げ検討 |

### 設計方針(合意済み)

1. **アーキテクチャ**: LVGL 9 をバックエンドにし、その上に Flutter 同型の 3 層を自前実装。
2. **PC シミュレータを作る**: SDL2 + LVGL で 1280x720 を WSL/Windows 上に表示。UI コードは実機と完全共有。
3. **フォント**: 当面 B612 Mono の英数字。ただし**フォント解決を最初から抽象化**し、後から日本語グリフ(UDEV Gothic 等)をフォールバックチェーンで足せるようにする。最終選定はシミュレータ上で実物を見比べて決める。
4. **ゴール**: 汎用フレームワーク本体が主成果物。実証として `ACTIVE/PERF` `ACTIVE/INIT` のデモ画面を再現する。

---

## 2. アーキテクチャ

### 2.1 なぜこの構成か

FMS 風 UI が必要とする描画要素は矩形・罫線・テキスト・単純な多角形(タブの斜めカット)だけで、LVGL の豊富なウィジェット群はほぼ不要。それでも LVGL を土台に据えるのは、自前で書くと重い以下を丸ごと引き受けてくれるため:

- ディスプレイ flush / DMA / ダブルバッファ / ティアリング対策(esp_lvgl_port + BSP)
- **dirty area 管理と部分再描画**(1280x720 の全画面再描画は PSRAM 帯域的に非現実的)
- アンチエイリアス付きフォントラスタライズ(`lv_font`, 4bpp)
- タッチ入力の取り込みとヒットテスト

一方 LVGL の `lv_obj` ツリーを直接組むと宣言的にならないので、**LVGL のレイアウト機能(flex/grid)は一切使わず**、Flutter と同じ制約ベースのレイアウトを自前で回し、結果の絶対座標だけを `lv_obj_set_pos/set_size` で流し込む。

### 2.2 3層モデル

```
  Widget          immutable / 使い捨て / build() で毎回生成
    │             Column, Row, Text, Container, GestureDetector, FmsFieldBox ...
    │  createElement()
    ▼
  Element         永続 / 差分検出(型 + Key で比較)/ State を保持
    │             StatelessElement, StatefulElement, RenderObjectElement
    │  createRenderObject() / updateRenderObject()
    ▼
  RenderObject    永続 / レイアウト(BoxConstraints↓ Size↑)と描画
    │             RenderFlex, RenderPadding, RenderText, RenderDecoratedBox ...
    │  paint()
    ▼
  lv_obj_t        LVGL のオブジェクト。描画を伴う葉ノードにだけ生成
```

**LVGL オブジェクトの節約**: `Column` / `Padding` / `Align` のような**レイアウト専用 RenderObject は `lv_obj` を作らない**。`lv_obj` を持つのは実際にピクセルを出すもの(テキスト、枠付きボックス、線、カスタム描画)だけで、親には「最も近い `lv_obj` を持つ祖先」を選び、そこからの相対座標を与える。FMS の 1 画面で `lv_obj` は 100〜300 個程度に収まる想定。

### 2.3 メモリ管理(ここが C++ 版の肝)

Flutter の Widget は GC 前提の使い捨てオブジェクト。C++ でこれを素直に再現するために **ダブルバッファ・アリーナ**を使う:

- `build()` 中の Widget は arena から bump 確保する。
- Element は差分検出のために「前回の Widget」を保持する必要があるため、arena を 2 面持ち、フレームごとにスワップ。新 arena に build → 旧 arena の Widget と diff → スワップして旧面を reset。
- Widget が `std::function`(`onTap` 等)を持つので、arena reset 時にデストラクタを走らせるための dtor リストを arena が持つ。
- Element / State / RenderObject は永続オブジェクトで、通常の `unique_ptr` 管理。

これにより `build()` 毎回呼び出しのコストは実質「bump ポインタを進めるだけ」になり、フラグメンテーションも起きない。

### 2.4 使う側のコード(目標とする書き味)

```cpp
class PerfPage : public StatefulWidget {
 public:
  State* createState() override { return new PerfPageState; }
};

class PerfPageState : public State<PerfPage> {
  int tab_ = 0;
  int v1_ = 153, vr_ = 155, v2_ = 160;

  Widget* build(BuildContext& ctx) override {
    const auto& t = FmsTheme::of(ctx);
    return new FmsPanel{{
      .title = "ACTIVE/PERF",
      .child = new Column{{
        .cross = CrossAxis::Start,
        .children = {
          new FmsTabBar{{
            .tabs = {"T.O", "CLB", "CRZ", "DES", "APPR", "GA"},
            .index = tab_,
            .onChanged = [this](int i) { setState([&] { tab_ = i; }); },
          }},
          new SizedBox{{ .height = 12 }},
          new Row{{ .children = {
            new FmsLabel{{ .text = "V1" }},
            new SizedBox{{ .width = 8 }},
            new FmsFieldBox{{
              .text  = fmt("%d", v1_),
              .unit  = "KT",
              .color = t.cyan,          // 操作可能な入力値
              .onTap = [this] { editV1(); },
            }},
          }}},
          new Expanded{ new Spacer{} },
        },
      }},
    }};
  }
};
```

C++20 の designated initializer で名前付き引数風に書ける。ネストは Flutter とほぼ同型になる。

---

## 3. ディレクトリ構成

```
FmsLikeUI/
├── CMakeLists.txt              # ESP-IDF プロジェクト定義
├── sdkconfig.defaults          # esp32p4 / PSRAM HEX 200M / LVGL / partial buffer
├── docs/
│   ├── PLAN.md                 # 本書
│   └── DESIGN.md               # 詳細設計(M1 で追記)
├── main/                       # 実機エントリポイント(ESP-IDF)
│   └── main.cpp                #   BSP init → fmsui::runApp(new DemoApp)
├── components/
│   ├── fmsui/                  # ★ フレームワーク本体(ESP-IDF 非依存 / LVGL のみ依存)
│   │   ├── include/fmsui/
│   │   │   ├── foundation/     # Size, Offset, Rect, EdgeInsets, Color, Key, Arena
│   │   │   ├── widgets/        # Widget, Element, State, BuildOwner, BuildContext
│   │   │   ├── render/         # RenderObject, RenderBox, BoxConstraints, PaintContext
│   │   │   ├── basic/          # Text, Container, Row, Column, Stack, GestureDetector ...
│   │   │   ├── theme/          # FmsTheme, FmsColors, FmsTypography, InheritedWidget
│   │   │   └── fms/            # FMS 風ウィジェット群
│   │   └── src/
│   ├── fmsui_port_esp/         # 実機ポート(BSP / esp_lvgl_port / タッチ / tick)
│   └── fmsui_fonts/            # 生成済み lv_font C 配列
├── sim/                        # PC シミュレータ(素の CMake + SDL2 + LVGL)
│   └── main_sim.cpp
├── demo/                       # デモアプリ(実機・シム共有)
│   ├── perf_page.cpp
│   ├── init_page.cpp
│   └── font_gallery.cpp        # フォント比較画面
├── tests/                      # レイアウト/差分検出のユニットテスト(PC, doctest)
└── tools/
    └── gen_fonts.py            # lv_font_conv 呼び出し(サイズ・グリフ範囲を一元管理)
```

`components/fmsui` は **ESP-IDF の component としても、素の CMake ライブラリとしてもビルドできる** ように CMakeLists を書く(`IDF_TARGET` の有無で分岐)。

---

## 4. デザイントークン(参考画像から抽出)

実装時にシミュレータ上で微調整する前提の初期値:

| ロール | 用途 | 初期値 |
|---|---|---|
| `background` | 画面全体 | `#000000` |
| `surface` | パネル背景 | `#14181C` |
| `border` | 罫線・枠 | `#4A5560` |
| `label` | 静的ラベル(白) | `#D8DCE0` |
| `cyan` | **操作可能な入力値** | `#29C5E8` |
| `green` | 計算値・実測値・アクティブ | `#1AE01A` |
| `amber` | 注意・要アクション | `#FF9E1B` |
| `magenta` | 制約値 | `#E060E0` |
| `button` | 押しボタン面 | `#2A3038` |
| `titlebar` | `ACTIVE/PERF` の帯 | `#C8D0C8` (背景) + 黒文字 |

色の意味付け(シアン=パイロットが入力する値、緑=システムが計算した値、アンバー=注意喚起)は Airbus の実機の規約に沿っており、これを `FmsTheme` のセマンティックな名前として API に出す。

### FMS ウィジェット群

- `FmsScaffold` — 黒背景 + 上部の FMS ヘッダ + 下部の `MSG LIST` 行
- `FmsPanel` — `ACTIVE/PERF` のようなタイトル帯付きの枠
- `FmsLabel` / `FmsValue` — 白ラベル / 色分けされた値 + 単位(小さめ)
- `FmsFieldBox` — 枠付き入力値。タップでスクラッチパッドを開く
- `FmsButton` — グレーの押しボタン(`POS MONITOR`, `IRS`, `DEPARTURE` …)
- `FmsDropdown` — `▼` 付き(`ACTIVE ▼`, `POSITION ▼`, `DATA ▼`)
- `FmsTabBar` — **斜めカットの台形タブ**(`T.O` / `CLB` / `CRZ` / `DES` / `APPR` / `GA`)。カスタム描画(多角形 + 罫線)が必要
- `FmsRadio` — `TOGA` / `FLEX` の丸ラジオ
- `FmsDivider` — 水平罫線
- `FmsScratchpad` + `FmsKeypad` — タッチ機なので MCDU のキーパッドをソフトキーで再現

---

## 5. マイルストーン

### M0 — 基盤と2ターゲットビルド(最初のリスク潰し)

- ESP-IDF プロジェクト骨格 + `espressif/m5stack_tab5` BSP 導入
- 実機で LVGL の「Hello」が 1280x720 で出て、タッチが効くところまで
- SDL2 シミュレータで同じ LVGL 画面が PC に出るところまで
- **要検証(最大のリスク)**: パネルのネイティブ向きは 720(H)x1280(V) の**縦**。横長 UI を出すには回転が要る。BSP / esp_lvgl_port がどう扱っているか、ソフト回転なら性能が出るか、`CONFIG_LVGL_PORT_ENABLE_PPA` のハード回転が使えるかを実機で確認する。ここで詰まると設計に影響する。
- 描画バッファ構成の初期決定(Espressif の実測では PSRAM 上の巨大バッファは遅く、画面の 10〜25% の部分バッファが定石)

**完了条件**: 実機とシムの両方で同一の LVGL 画面が出て、fps とタッチ応答が測れている。

### M1 — コアフレームワーク

- `foundation`: `Size` / `Offset` / `Rect` / `EdgeInsets` / `Color` / `Key` / `Arena`
- `Widget` / `Element` / `State` / `setState` / `BuildOwner`(dirty リスト → フレーム単位で再ビルド)
- `RenderBox` / `BoxConstraints` / レイアウトパス(制約が下り、サイズが上がる)
- LVGL バックエンド: `RenderText`, `RenderDecoratedBox`, `RenderCustomPaint`
- 基本ウィジェット: `Text`, `Container`, `Padding`, `SizedBox`, `Align`, `Center`, `Row`, `Column`, `Expanded`, `Flexible`, `Spacer`, `Stack`, `Positioned`, `GestureDetector`
- **PC 側でレイアウトのユニットテスト**(実機不要で回帰を止められる)

**完了条件**: `setState` でカウンタが増える画面が、シムと実機の両方で同一コードから動く。

### M2 — テーマとフォント

- `InheritedWidget` + `FmsTheme::of(context)`
- フォント抽象(サイズ×ウェイト → `lv_font_t*` の解決、フォールバックチェーン)
- `tools/gen_fonts.py` で B612 Mono を複数サイズ(16/20/24/28/32px, 4bpp AA)にビットマップ化
- **シミュレータにフォント比較画面(`font_gallery`)を出し、B612 Mono / JetBrains Mono / IBM Plex Mono / Martian Mono を同じ FMS 画面で並べて見比べて決定**

**完了条件**: フォントが決まり、`FmsTheme` 経由で全ウィジェットに効いている。

### M3 — FMS ウィジェット群

第4節のウィジェットを実装。台形タブとスクラッチパッド/キーパッドが山場。

**完了条件**: シムのウィジェットカタログ画面に全部並び、タッチで反応する。

### M4 — デモ画面

`ACTIVE/PERF (T.O)` と `ACTIVE/INIT` を参考画像に寄せて再現。2 面を左右に並べるレイアウトも。値のタップ → スクラッチパッド入力 → 反映まで通す。

**完了条件**: 実機で参考画像と並べて遜色ないスクリーンショットが撮れる。

### M5 — 性能と仕上げ

- fps / flush 時間 / ヒープ・PSRAM 使用量の計測
- 部分バッファサイズ、`LV_DEF_REFR_PERIOD`、ダブルバッファ構成のチューニング
- 足りなければ LVGL 9.4 の PPA 描画ユニット(experimental)を評価
- README(フレームワークの使い方)、OFL ライセンス表記の同梱

---

## 6. 想定リスク

| リスク | 影響 | 対策 |
|---|---|---|
| **画面回転**: パネルネイティブが縦 720x1280 | 横 UI にソフト回転が必要なら大幅に遅くなる | M0 で最優先確認。PPA ハード回転 or パネル初期化での対処を探る |
| **PSRAM 帯域**: 1280x720 は大きい | fps が出ない | 部分バッファ(画面の 10〜25%)、内部 SRAM の活用、再描画領域の最小化。フレームワーク側で「変更のあった RenderObject だけ再描画」を効かせる |
| Widget アリーナのライフタイム管理 | クラッシュ | ダブルバッファ arena + dtor リスト。PC 側で ASan/UBSan を有効にしてテストを回す |
| LVGL 9.2.2 に PPA 描画がない | 性能不足時の打ち手が限られる | 9.4 への引き上げ余地を残す(`esp_lvgl_port` は lvgl >=8 <10 を許容)。ただし PPA 描画は experimental でティアリングの既知不具合あり |
| 私(Claude)が実機を持っていない | 見た目の最終確認ができない | シミュレータで詰めきる。実機確認はフラッシュしてもらってスクリーンショットで判断 |

---

## 7. ライセンス

- B612 / B612 Mono: **SIL OFL 1.1**(商用組込み・ビットマップ化可)。著作権表示と OFL 全文の同梱が必要 → `licenses/` に配置する。
- FlyByWire の `HoneywellMCDU.ttf` 等は実機トレース + GPL-3.0 のため**使わない**。
