# FmsLikeUI を別のプロジェクトから使う

FmsLikeUI は FMS アプリケーションではなく、FMS らしい画面を作るための UI フレームワークです。
この文書は、このリポジトリの外にあるプロジェクトが `components/fmsui` を使うときの**初版の利用契約**を書きます。

ここに書いてあるのは、[`consumers/`](../consumers) の 2 つのプロジェクトと CI の consumer job で
実際に確かめている範囲だけです。判断の経緯は [DECISIONS.md](DECISIONS.md) の
「外部利用契約と consumer smoke test」、フレームワークの中身は [DESIGN.md](DESIGN.md) にあります。

## 正式に扱う導入方法

初版で扱うのは、**ソースを取り込む** 2 つの経路だけです。

| 経路 | やり方 |
|---|---|
| 通常の CMake | 利用側が LVGL の target を用意し、`components/fmsui` を `add_subdirectory()` する |
| ESP-IDF | `components/fmsui`(と、必要なら `components/fmsui_fonts`)を local component または Git submodule として `EXTRA_COMPONENT_DIRS` に加える |

ESP Component Registry への公開、インストール済みの CMake package、ビルド済みライブラリの配布、
SemVer による互換性の保証は、まだ行っていません。

Git submodule として取り込む場合、LVGL は FmsLikeUI の中でさらに submodule になっているので、
`git submodule update --init --recursive` で取得してください。

## 確認済みの環境

| 項目 | 確認した版 |
|---|---|
| C++ | C++20(`fmsui` が `cxx_std_20` を要求する) |
| LVGL | v9.5.0。submodule `third_party/lvgl` のコミット `85aa60d` |
| LVGL の設定 | 同梱の [`third_party/lv_conf.h`](../third_party/lv_conf.h) |
| ホスト | Ubuntu 22.04、gcc 11.4、CMake 3.22.1 と 3.31.6、Ninja |
| ESP-IDF | 5.5.4(`espressif/idf:v5.5.4`、CI では digest で固定)、ESP32-P4 |

これ以外の LVGL の版・設定、コンパイラ(clang、MSVC など)、ESP32-P4 以外のターゲットは確認していません。

## 通常の CMake から使う

```cmake
set(CMAKE_CXX_STANDARD 20)
set(FMSUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/FmsLikeUI)

# 1. LVGL は利用側のもの。fmsui は `lvgl` という名前の target にリンクする。
set(LV_BUILD_CONF_PATH ${FMSUI_DIR}/third_party/lv_conf.h CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_subdirectory(${FMSUI_DIR}/third_party/lvgl ${CMAKE_BINARY_DIR}/lvgl)

# 2. フレームワーク。
add_subdirectory(${FMSUI_DIR}/components/fmsui ${CMAKE_BINARY_DIR}/fmsui)

# 3. B612 Mono を使うときだけ(任意)。target 名は fmsui_fonts。
# add_subdirectory(${FMSUI_DIR}/components/fmsui_fonts ${CMAKE_BINARY_DIR}/fmsui_fonts)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE fmsui::fmsui lvgl::lvgl)
```

- 同梱の `lv_conf.h` は LVGL の SDL ドライバを無効にしているので、SDL2 は要りません。
  シミュレータ(`sim/`)だけが、`lvgl` target に `LV_USE_SDL=1` を渡して有効にしています。
- `fmsui` は静的ライブラリで、`lvgl` を PUBLIC にリンクします。LVGL を別の方法で用意する場合も、
  `add_subdirectory(components/fmsui)` より前に `lvgl` という target を作っておいてください。
- 動く全体は [`consumers/host/CMakeLists.txt`](../consumers/host/CMakeLists.txt) にあります。

## ESP-IDF から使う

プロジェクトの `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
set(FMSUI_DIR ${CMAKE_CURRENT_LIST_DIR}/external/FmsLikeUI)

set(EXTRA_COMPONENT_DIRS
    ${FMSUI_DIR}/components/fmsui
    ${FMSUI_DIR}/third_party/lvgl            # LVGL を別に用意するなら不要
    # ${FMSUI_DIR}/components/fmsui_fonts    # B612 Mono を使うときだけ
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# LVGL の設定を Kconfig ではなく lv_conf.h から取る。
idf_build_set_property(COMPILE_DEFINITIONS "LV_KCONFIG_IGNORE" APPEND)

project(my_app)
```

`main/CMakeLists.txt` では `REQUIRES fmsui lvgl` とします。`sdkconfig.defaults` には次の 2 行を入れてください。
LVGL の ESP-IDF 用ビルドは、`LV_KCONFIG_IGNORE` を付けてもこの 2 つだけは Kconfig から読み、
既定ではどちらも有効なので、使わない examples と demos までコンパイルします。

```
# CONFIG_LV_BUILD_EXAMPLES is not set
# CONFIG_LV_BUILD_DEMOS is not set
```

- `EXTRA_COMPONENT_DIRS` には `components/fmsui` を**個別に**書いてください。`components/` 全体を指定すると、
  どこからも `REQUIRES` されていなくても `fmsui_fonts` までビルドに入ります。
- `third_party/lvgl` を component として登録すると、BSP や `esp_lvgl_port` が Component Registry から取ってくる
  `lvgl` より優先されます。M5Stack Tab5 ではこの形で動かしています(このリポジトリの最上位 `CMakeLists.txt`)。
- 動く全体は [`consumers/esp-idf/`](../consumers/esp-idf) にあります。これはビルドとリンクの確認用で、
  ボードを持たず、書き込みもしていません。

## 利用側が用意するもの

フレームワークが持つのは Widget / Element / RenderObject の木と、LVGL へ出す部分だけです。それ以外は利用側が持ちます。

| 利用側が持つもの | 内容 |
|---|---|
| LVGL の初期化と設定 | `lv_init()` と `lv_conf.h`。確認済みなのは同梱の `lv_conf.h` だけです。フレームワークは `lv_label` と `LV_FONT_DEFAULT` を使います |
| display | `lv_display_t` の作成、描画バッファ、flush。実機ではパネルドライバや BSP の仕事です |
| 入力 | pointer 型の `lv_indev_t`。`GestureDetector` は LVGL の `LV_EVENT_CLICKED` で動きます |
| tick | `lv_tick_inc()` または `lv_tick_set_cb()` |
| `lv_timer_handler()` を回すスレッド | そのスレッドがフレームループになります(下記) |
| フォント | `FmsThemeData::font` の 5 段(`unit` / `label` / `body` / `value` / `title`)に `lv_font_t` を入れる。`fmsui_fonts` の B612 Mono は任意です。`Text` の `font` を省くと `LV_FONT_DEFAULT` になります |
| マイクロ秒クロック(任意) | `FmsApp::setClock()`。無ければ統計の時間は LVGL の tick(ミリ秒)から取ります |
| スレッドの識別子(推奨) | `FmsApp::setThreadId()`。無ければ、別スレッドからの `setState()` を検出する assert が無効になります |
| ボード | BSP、パネル、タッチ、回転、PPA、PSRAM などの `sdkconfig`。フレームワークはどれも持ちません |

フレームワークの側が持つのは次のものです。

- `init()` に渡した display の**アクティブ screen**。スタイルを外して背景を黒にし、スクロールを止め、
  作る lv_obj をすべてその直下に絶対座標で並べます。重なり順を child の index で決めるので、
  同じ screen に利用側の lv_obj を置くことは想定していません。
- LVGL timer 1 つ(周期 10ms)。フレームごとの再ビルド・レイアウト・LVGL への反映はその中で行います。
- 共有スタイルのキャッシュと、Widget 用のアリーナ。どちらも `shutdown()` で解放します。

## 公開 API の範囲

入口は `<fmsui/fmsui.h>` です。個別のヘッダーを include してもかまいません。
公開ヘッダーは `components/fmsui/include/fmsui/` にあるものだけで、利用側が include できるのもそれだけです。

| 区分 | ヘッダー | 扱い |
|---|---|---|
| 通常のアプリが使う | `app.h`、`foundation.h`、`widget.h`、`widgets.h`、`theme.h`、`fms.h`、`str.h` | 利用契約に含む |
| 診断・高度な拡張 | `refresh.h`(LVGL の再描画時間)、`render.h`(`CustomPaint` に渡す `Painter` と `Canvas`、独自の RenderObjectWidget を作るための `RenderObject`、`styleCacheStats()`) | 使ってよいが、通常の利用より変わりやすい |
| 入口 | `fmsui.h` | 上の 9 つをまとめて include する。内部のヘッダーは含まない |

この 10 個は、どれも単独で include してコンパイルできることを CI で確かめています。

**フレームワーク内部**のアリーナ(`arena.h`)と Element ツリー(`element.h`)は `components/fmsui/src/internal/fmsui/` にあり、
ライブラリ自身のビルドにだけ PRIVATE な include path として渡されます(ESP-IDF では `PRIV_INCLUDE_DIRS`)。
利用側からは `<fmsui/arena.h>` も `<fmsui/element.h>` も include できず、できないことを CI で確かめています。
この 2 つを直接使っていたコードは利用契約の対象外で、互換用のヘッダーは置いていません。

- `build()` が受け取る `BuildContext` は、中身の見えない型です。参照を受け取って `FmsTheme::of(ctx)` などへ渡すだけで、
  作ったり、コピーしたり、中を読んだりはできません。
- Widget は `new` でアリーナから取られ、`delete` はしません。前回のビルドの Widget(とそのコールバック)は次のビルドの間も残り、
  その次のビルドで仮想デストラクタが呼ばれます。
- `setThreadId()` に渡す `ThreadId` / `ThreadIdFn` は `app.h` にあります。

## ライフサイクル

```cpp
lv_init();
// display、入力、tick を用意する(利用側)

fmsui::FmsApp &app = fmsui::FmsApp::instance();
app.setClock(micros);          // 任意。最初のフレームより前に
app.setThreadId(thread_id);    // 推奨。最初のフレームより前に

fmsui::runApp([theme] { return my_build(theme); });
// = FmsApp::instance().init(lv_display_get_default(), builder)

for (;;) {
    lv_timer_handler();        // この中でフレームループが走る
    sleep_ms(10);
}

// 終わらせるとき
app.shutdown();                // タイマ停止 → 木と lv_obj の破棄 → Widget とアリーナの解放 → 共有スタイルの解放
lv_display_delete(display);    // shutdown() の後で
```

1. **`runApp(builder)`** は、既定の display で `init()` を呼びます。別の display を使うなら `FmsApp::instance().init(display, builder)` を直接呼びます。
2. **`builder`** は再ビルドのたびに呼ばれ、新しい Widget の木を返します。Widget は `new` で作ります(アリーナから取られるので、`delete` はしません)。
   `builder` の中身はビルドパスの中で走るので、`fmt()` もここで使えます。
3. **フレームループ** は `lv_timer_handler()` を呼んだスレッドで走ります。最初のフレームで、そのスレッドを木の持ち主として記録します。
   何も変わっていないフレームでは、フラグを 1 つ見るだけで戻ります。
4. **`shutdown()`** は、木が作った lv_obj をすべて消し、直近 2 回のビルドの Widget とアリーナのメモリを解放して、
   lv_obj が指していた共有スタイルを最後に解放します。
   LVGL の display を消す前に呼んでください。実機でアプリがプロセスそのものなら、呼ばなくてかまいません。
   シミュレータとテストは、LeakSanitizer に報告を残さないために呼んでいます。

`FmsApp` はプロセスに 1 つです。`shutdown()` の後に、もう一度 `init()` できます。

ESP-IDF で `esp_lvgl_port` を使う場合、LVGL は port が作るタスクで走ります。`runApp()` などの LVGL に触る呼び出しは
`bsp_display_lock()` / `lvgl_port_lock()` の中で行ってください(このリポジトリの `main/main.cpp` の形)。

## スレッド境界

**木、`State`、LVGL はフレームループのスレッドのものです。** ロックはありません。

| どこから | してよいこと |
|---|---|
| フレームループのスレッド(`GestureDetector` などのコールバックもここで走る) | `setState()`、`fmt()`、`Str` の作成、Widget の `new`(`builder` / `build()` の中) |
| 他のタスク | データを自分で同期して公開し、**その後で** `FmsApp::instance().requestFrame()` を呼ぶ |
| どこからでも | `FmsApp::stats()`、`styleCacheStats()`、`RefreshStats::take()`(どれも一貫したコピーを返す) |
| IRAM 割り込みハンドラ | 直接は推奨しない。タスクに通知して、そのタスクから `requestFrame()`。どうしても必要なら `FmsApp::requester()` |

```cpp
// 他のタスク
altitude_.store(v, std::memory_order_relaxed);   // 1. 公開する(置き場はアプリが持つ)
fmsui::FmsApp::instance().requestFrame();        // 2. そのあとで要求する

// build() の中(フレームループのスレッド)
const int alt = altitude_.load(std::memory_order_relaxed);
```

- `requestFrame()` は値を積みません。「次のフレームで入力を読み直せ」という要求で、処理される前に届いた複数の要求は
  1 回のビルドにまとまります。UI は最新の値を 1 つ描きます。確保もロックもせず、呼び出し側を待たせません。
- 他のタスクから `setState()` を呼んではいけません。変更がそのタスクで即座に走り、ビルドと競合します。
  `setThreadId()` を設定していれば、assert で止まります。
- 公開するタスクが複数あるとき、要求フラグは全員のデータの公開を保証しません。データごとに atomic、ロック、キュー、
  不変オブジェクトの受け渡しなどで同期してください。

詳しい理由は [DESIGN.md](DESIGN.md) の「他のタスクから UI を更新する」にあります。

## 最小の Widget tree

```cpp
#include <atomic>
#include <cinttypes>

#include "fmsui/fmsui.h"

using namespace fmsui;

std::atomic<uint32_t> g_sample{0};   // 他のタスクが公開する

class Page : public StatefulWidget {
public:
    FMSUI_WIDGET(Page)
    StateBase *createState() const override;
};

class PageState : public State<Page> {
    uint32_t taps_ = 0;

public:
    Widget *build(BuildContext &ctx) override {
        const FmsThemeData &t = FmsTheme::of(ctx);
        return new Column{{
            .main = MainAxis::Center,
            .spacing = 12,
            .children = {
                new Text{{.text = fmt("SAMPLE %" PRIu32, g_sample.load(std::memory_order_relaxed)),
                          .font = t.font.value,
                          .color = t.color.computed}},
                new FmsButton{{
                    .text = "TAP",
                    .role = FmsRole::Entry,
                    .on_tap = [this] { setState([this] { taps_++; }); },
                }},
                new Text{{.text = fmt("TAPS %" PRIu32, taps_), .font = t.font.body, .color = t.color.entry}},
            },
        }};
    }
};

StateBase *Page::createState() const { return new PageState(); }

// 起動時。フォントは利用側が選ぶ。
const lv_font_t *font = LV_FONT_DEFAULT;
const FmsThemeData theme{
    .color = defaultPalette(),
    .font = {.unit = font, .label = font, .body = font, .value = font, .title = font},
    .metric = defaultMetrics(),
};
runApp([theme] { return new FmsTheme{{.data = theme, .child = new Page{}}}; });
```

- `FmsTheme` は木の上のほうに 1 つ置きます。`FmsLabel` や `FmsButton` などの FMS ウィジェットは `FmsTheme::of()` で色とフォントを読み、見つからなければ assert します。
- Args の `{}` が付いていないフィールド(`Text` の `text` など)は省けません。省くと `-Wmissing-field-initializers` になります。
- `State` はふつうの `new` で作ります。Element が持ち続け、再ビルドのたびに新しい Widget を渡します。
- `uint32_t` は riscv32 では `long unsigned int` なので、`%u` ではなく `PRIu32` を使います。

## 保証しないもの

- ESP Component Registry、GitHub Release、インストール済み CMake package での配布
- SemVer によるソース・ABI の長期互換
- 公開する型のサイズと ABI、内部ヘッダー(`src/internal/`)の中身
- ビルド済みライブラリ、署名、配布アーカイブ
- M5Stack Tab5 以外のボードでの動作と、ボードの初期化(BSP)の提供
- 上の「確認済みの環境」以外の LVGL・コンパイラ・ESP-IDF・ターゲット
- 実機への書き込み、物理タッチ、見た目のスクリーンショット比較(consumer の検証はこれらを含まない)

## 検証しているもの

| プロジェクト | 何を確かめるか | CI job |
|---|---|---|
| [`consumers/host/`](../consumers/host) | ルートの CMake、`sim/`、`demo/`、`tests/`、`fmsui_fonts` を含まないツリーで configure・ビルド・実行する。headless の display と pointer を利用側で持ち、`runApp()`、ビルドと描画、別スレッドからの `requestFrame()`、タップからの `setState()`、利用側のフォント、`shutdown()` を確かめる。公開ヘッダー 10 個を 1 つずつ単独でコンパイルし、内部ヘッダーを include するとヘッダーが見つからずに失敗することを確かめる | `consumer (host)`(pull request、`master` への push、手動) |
| [`consumers/esp-idf/`](../consumers/esp-idf) | `main/`、`demo/`、Tab5 BSP、`fmsui_fonts` を含まないツリーで、同じ Widget tree を ESP32-P4 向けにビルド・リンクする。ビルドに入った component に `fmsui_fonts`、BSP、`esp_lvgl_port` が無いこと、`main` の include path から fmsui の内部ヘッダーに届かないことを確かめる | `consumer (esp-idf, esp32p4)`(`master` への push、手動) |

両方とも `consumers/shared/` の同じページを使い、フレームワークは `<fmsui/fmsui.h>` からしか使いません。
consumer の検証は既存の host job や device-build とは別の job なので、利用契約が壊れたときは、内部テストやデモが通っていてもその名前で失敗します。

手元では次のように実行できます。どちらも、必要なファイルだけを `OUT_DIR/tree` に写してからビルドします。

```bash
bash tools/ci/consumer_host.sh ci-out/consumer-host          # WSL / Linux

. "$IDF_PATH/export.sh"                                      # ESP-IDF 5.5.4 の環境で
bash tools/ci/consumer_idf.sh ci-out/consumer-idf
```
