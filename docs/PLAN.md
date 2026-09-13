# FmsLikeUI — 現行ロードマップ

更新: 2026-09-12

## 1. この文書の役割

この文書は、FmsLikeUI の**現在地と今後の作業順**を示す。
当初の M0〜M5 実装計画は、実装、実機計測、consumer 検証、CI 導入まで進み、現状と合わない記述が増えたため、
2026-09-12 に現行ロードマップへ置き換えた。

文書の役割は次のように分ける。

| 文書 | 役割 |
|---|---|
| [PLAN.md](PLAN.md) | 現在地、次に判断・実装すること、未着手候補 |
| [DECISIONS.md](DECISIONS.md) | 合意した設計判断、実装状況、完了条件と検証記録 |
| [DESIGN.md](DESIGN.md) | 現在の内部設計と、採用しなかった方式 |
| [USING.md](USING.md) | 外部プロジェクトから使うときの公開契約 |
| [PERF.md](PERF.md) | シミュレータと実機の性能測定 |
| [M0-NOTES.md](M0-NOTES.md) | Tab5 のブリングアップで確認した事実 |

方針が決まったら、実装前に `DECISIONS.md` の「計画中・未実装」へ完了条件付きで記録する。
実装しただけでは完了にせず、「レビュー待ち」からレビューを経て「実装済み」へ移す。

## 2. ゴール

FmsLikeUI の主成果物は、**FMS 風の視覚表現を持つ UI を組み立てられる、組込み向け宣言的 UI フレームワーク**である。

- C++20 で Widget → Element → RenderObject の 3 層モデルを提供する。
- 制約ベースのレイアウトを行い、描画、フォント、dirty area、ポインタ入力は LVGL に任せる。
- フレームワーク本体は ESP-IDF や特定 BSP に依存せず、通常の CMake と ESP-IDF component の両方で使えるようにする。
- 同じ UI コードを host シミュレータと M5Stack Tab5 で使えるようにする。
- 公開 API と内部実装を物理的に分離し、外部 consumer から利用契約を継続的に検証する。

FMS アプリケーションそのものを作ることはゴールではない。`demo/` の画面は、レイアウト、描画、入力、差分更新、
性能、実機ビルドを確認するための fixture であり、業務機能やドメインロジックを追加しない。

## 3. 非目標

- 航法、飛行計画計算、機体データ管理など、FMS アプリケーションのドメイン機能
- デモアプリを製品として完成させるための画面・機能追加
- Flutter の全機能を再現すること
- BSP、display、touch、回転、PSRAM 設定など、ボード初期化のフレームワークへの取り込み
- 利用例が無い段階で clipping、scroll、animation、GlobalKey、UI task queue などを先回りして追加すること
- 内部ヘッダーや公開型の ABI を、決定なしに互換保証すること

## 4. 現在の基準

| 項目 | 現在の状態 |
|---|---|
| フレームワーク | `components/fmsui/`。LVGL のみに依存する C++20 static library / ESP-IDF component |
| 描画基盤 | LVGL v9.5.0 を `third_party/lvgl` submoduleで固定。実機と host で `third_party/lv_conf.h` を共有 |
| 実機基準 | M5Stack Tab5 (ESP32-P4)、ESP-IDF 5.5.4。BSP と managed components は `dependencies.lock` で固定 |
| host | SDL2 シミュレータ、1280x720 の headless PNG、ポインタ入力の合成 |
| 公開 API | `components/fmsui/include/fmsui/` の 10 ヘッダー。通常向け 7、高度・診断向け 2、umbrella 1 |
| 内部実装 | `arena.h` と `element.h` は `components/fmsui/src/internal/fmsui/`。consumer へ include path を渡さない |
| メモリ | Widget は 2 面の bump arena、Element / State / RenderObject は永続。`shutdown()` で tree、両 arena、style cache を解放 |
| 更新 | `setState()` と thread-safe な `requestFrame()`。フレーム単位で全 Widget tree を再構築し、RenderObject / LVGL 更新は差分化 |
| フォント | 利用側が `lv_font_t` をテーマへ渡す。`components/fmsui_fonts` の B612 Mono は任意依存 |
| 外部利用 | 通常 CMake と ESP-IDF local component / Git submodule の source integration を consumer で検証 |
| 自動検証 | host 3 CTest、ASan / UBSan / LeakSanitizer、6デモ、host / ESP-IDF consumer、ESP32-P4 6構成ビルド |
| CI | GitHub Actions 5 job。host debug、host sanitizer、host consumer、ESP-IDF consumer、device-build |

公開 API の正確な分類、所有範囲、ライフサイクル、スレッド境界、確認済み環境は `USING.md` を正とする。
テスト数や binary size、CI run の時点値は `DECISIONS.md` に残し、このロードマップへ固定値として重複させない。

## 5. 完了済みの基盤

### 5.1 実機・シミュレータ共通基盤

- Tab5 の display、PPA 回転、GT911 touch、部分描画を実機で確認した。
- SDL2 シミュレータと headless PNG 出力を用意し、実機と同じ UI コードを使えるようにした。
- LVGL と ESP-IDF / BSP の組み合わせを固定し、ブリングアップと性能の実測を文書化した。

### 5.2 宣言的 UI フレームワーク

- Widget / Element / State / RenderObject、制約ベースレイアウト、key による再突合を実装した。
- 基本 Widget、FMS 風 Widget、theme、任意フォント、gesture、scratchpad / keypad、固定窓 paging を実装した。
- Widget arena の 2 世代寿命、virtual destructor、Element tree、shared style の解放順を自動テストで固定した。

### 5.3 操作・並行性・診断

- LVGL pointer の press / hold / release を通す headless 操作テストを用意した。
- `requestFrame()` の複数 producer、高頻度要求、取りこぼし、終了条件を独立テストで検証した。
- frame / refresh / style cache の snapshot と simulator diagnostics を実装した。
- 通常設定と sanitizer 設定で、レイアウト、入力、並行負荷、6デモを継続検証できるようにした。

### 5.4 外部利用と継続的インテグレーション

- host と ESP-IDF の独立 consumer を用意し、リポジトリ本体の `main/`、`demo/`、BSP、任意フォントへ
  偶然依存しないことを検査した。
- 公開ヘッダー 10 個の単独コンパイルと、旧内部ヘッダーを consumer から include できないことを機械判定した。
- GitHub Actions で host、sanitizer、consumer、ESP32-P4 6構成の検証を自動化した。
- 公開 API と内部ヘッダーを物理的に分離した。

各項目の判断、変更内容、検証結果は `DECISIONS.md` の「実装済み」を参照する。

## 6. 次期計画: 配布方法と互換性方針

**状態: 今後の計画。方針未決定・未実装。**

公開 API の物理境界と consumer 検証が整ったため、次は「何を正式な配布物とし、どの互換性を約束するか」を決める。
ここでは忘れないために判断事項を残す。結論と完了条件は、着手時に `DECISIONS.md` へ記録してから実装する。

### 6.1 決めること

1. **正式な導入方法**
   - 現在検証している source integration (`add_subdirectory`、ESP-IDF local component / Git submodule)を
     当面の正式手段とするか。
   - ESP Component Registry、CMake install package、GitHub Release、source archive のどこまでを提供するか。
   - ビルド済み library を配布対象にするか。対象にする場合、toolchain、LVGL、target、ABI をどう固定するか。

2. **バージョン方針**
   - `v0.x` から SemVer を始めるか、安定版の条件を別に設けるか。
   - 公開 10 ヘッダーのソース互換、動作互換、ABI 互換のうち、どれを保証対象にするか。
   - `render.h` / `refresh.h` の高度・診断 API を通常 API と同じ安定度にするか。
   - 破壊的変更、deprecated 期間、移行手順をどう記録するか。

3. **バージョンの表現**
   - Git tag、CMake project version、ESP-IDF component metadata、公開 macro / accessor のどれを正とするか。
   - `CHANGELOG` を置くか。変更を API、内部、検証、device configuration に分けるか。

4. **配布物の中身**
   - `components/fmsui`、任意の `fmsui_fonts`、LVGL の扱い、設定例、ライセンス、導入文書をどうまとめるか。
   - demo と test fixture を配布物に含めるか。含めても、製品機能や依存として扱わないことをどう保証するか。
   - B612 Mono の OFL、LVGL の MIT、その他同梱物の notice をどう検査するか。

5. **リリース検証**
   - tag / release candidate から、host と ESP-IDF consumer をクリーンに構築できることをどう検証するか。
   - GitHub Actions の release job、artifact、checksum、署名、再現性をどこまで求めるか。
   - device build と実機確認を、リリース必須条件にするか別の手動 gate にするか。

### 6.2 この計画で守る前提

- 互換性の候補は公開 10 ヘッダーと文書化した動作に限り、`src/internal/` は対象にしない。
- `fmsui` 本体は LVGL のみに依存し、BSP、demo、`fmsui_fonts` を必須依存へ戻さない。
- demo に製品機能を足して配布品質を示すのではなく、独立 consumer と自動検査を証拠にする。
- 配布方式を増やす前に、維持コストと利用者が実際に必要とする導入経路を確認する。
- 方針未決定の間は、`USING.md` に記載した source integration だけを確認済み経路として扱う。

## 7. 未着手候補

次は必要性が明確になってから計画する。現時点では実装約束ではない。

### 7.1 ライフサイクル契約の追加整理

`shutdown()` 後に同じプロセスで `init()` し直す経路は検証済みだが、前セッションと異なる frame thread へ
所有権を移す場合の `BuildOwner` 再バインドは別契約として決めていない。必要なら、対応範囲、thread-id callback の寿命、
再初期化テストを先に定義する。

### 7.2 検証ゲートの拡張

- 実機への自動 flash と物理 touch の回帰試験
- PNG の golden image / 見た目比較
- ThreadSanitizer の常設化
- 長時間の host / 実機耐久試験
- 定期実行による依存更新・toolchain drift の検出

これらは現在の CI の対象外である。導入するときは、誤検出、実行時間、runner / 実機管理まで含めて判断する。

### 7.3 UI 機能の追加

clipping、scroll、animation、focus、GlobalKey、UI task queue、追加 gesture などは、具体的な framework consumer の
要求が出た時点で優先順位を付ける。デモを充実させること自体を追加理由にしない。

### 7.4 対応環境の拡張

M5Stack Tab5 以外の board、別 LVGL / ESP-IDF 版、別 compiler、install 済み CMake package は現在の確認範囲外である。
対応を広げる場合は、その環境の consumer と CI / 実機証拠を追加する。

## 8. 変更ごとの検証ゲート

| 変更 | 最低限の確認 |
|---|---|
| framework の実装 | 通常／sanitizer の host build、CTest 3/3、sanitizer 報告 0、6デモ headless 描画 |
| 公開 API / build system | 上記に加え、公開10ヘッダー、内部ヘッダー拒否、host / ESP-IDF consumer |
| LVGL / ESP-IDF / component 更新 | 固定参照の更新、ESP32-P4 6構成、LVGL examples / demos の除外、警告0、必要な実機確認 |
| 入力・描画・device configuration | host の操作／描画確認と、変更した挙動に対応する実機確認を別々に記録 |
| 文書だけ | リンク、現行ソースとの一致、`git diff --check`。実装済みの事実を変更する場合は根拠も照合 |

ビルド成功、host の描画、実機動作は同じ証拠ではない。完了記録では、どこまで確認したかを分けて書く。

## 9. 次に行うこと

次の設計作業は、第6節の配布方法と互換性方針である。

1. 想定する利用者と必要な導入経路を確認する。
2. 配布形式、互換性、version、release gate の選択肢を比較する。
3. 合意した方針と実装完了条件を `DECISIONS.md` の「計画中・未実装」へ記録する。
4. その後に、必要な metadata、package、CI、文書を実装する。

この判断が済むまでは、既存の source integration、公開 API、consumer test を現在の利用契約として維持する。
