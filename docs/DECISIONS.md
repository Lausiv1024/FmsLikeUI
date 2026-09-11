# 設計判断ログ

今後の変更で前提にする設計判断と、その実装状況を記録する。
状態は上位見出しで **「実装済み」**、**「レビュー待ち」**、
**「レビュー済み・未完了」**、**「計画中・未実装」** に分ける。
計画を実装しただけでは「実装済み」へ移さない。書けたものは「レビュー待ち」に置き、
レビュー後に残件があれば「レビュー済み・未完了」、完了条件をすべて確認できれば
「実装済み」へ移す。
実装済みの仕組みや詳しい背景は [DESIGN.md](DESIGN.md) も参照する。

## 実装済み

### 2026-09-09: 統計スナップショットとスタイルキャッシュの寿命

**状態: 実装済み (2026-09-09)。詳細は [DESIGN.md](DESIGN.md) の
「統計は『1 フレーム分』をまとめて渡す」と「スタイルキャッシュは LVGL セッションの寿命で持つ」。**

FmsLikeUI は FMS アプリケーションそのものではなく、FMS らしい画面を作るための
UI フレームワークである。`demo/` とシミュレータの各画面は、機能を完成させる対象ではなく、
レイアウト、入力、再構築、描画負荷を確認するテスト用フィクスチャとして扱う。

#### 1. 統計情報は一貫したスナップショットとして公開する

`FmsApp::stats()` は参照ではなく `FrameStats` のコピーを返す。

- UI タスクは、1 フレーム分の統計をローカル変数として最後まで組み立てる。
- 完成した統計だけを、短時間のロック内で公開用スナップショットへコピーする。
- 読み手は同じロック内でコピーを取得し、ログ出力などはロック解除後に行う。
- 累積ビルド回数は UI タスクだけが更新し、完成した値をスナップショットに含める。
- LVGL のリフレッシュ統計も `volatile` に頼らず、同じく一括取得できる所有クラスにまとめる。

個々のフィールドを別々の atomic にする方法は採らない。個別の読み書きが安全でも、
異なるフレームの値が混ざった統計を返す可能性が残るためである。ロック範囲にはビルド、
レイアウト、描画、ログ出力を含めず、小さな構造体のコピーだけを置く。

#### 2. スタイルキャッシュは LVGL セッションの寿命で所有する

共有スタイルは途中で追い出さず、LVGL セッションが終わるまでアドレスを固定する。
LVGL オブジェクトが共有スタイルへのポインタを保持するため、単純な LRU 削除は行わない。

- raw pointer のグローバル配列ではなく、内部の `StyleCache` がエントリを所有する。
- 終了時は、すべての利用側オブジェクトを破棄した後で各 `lv_style_t` を
  `lv_style_reset()` し、シミュレータとテストで確実に解放する。
- 実機ではアプリケーション存続中の常駐キャッシュとする。
- 色、フォント、配置、枠線、角丸は、テーマや画面設計で決まる有限種類の値として扱う。
- 毎フレーム新しい色や角丸値を生成する使い方と、スタイルの連続アニメーションは対象外とする。
- テキスト用とボックス用のエントリ数、最大到達数を診断できるようにする。

最初から任意の固定上限は設けない。カタログとテスト画面で実際の最大種類数を測定し、
同じ有限種類のスタイルを繰り返したときにキャッシュ件数が増えないことをテストする。

#### 実装完了の条件

- [x] 同時に統計を更新・取得しても、1 フレームとして矛盾しない値を返す。
- [x] `volatile` を同期手段として使わず、実機ビルドの該当警告をなくす。
- [x] 同じスタイル集合を繰り返してもキャッシュ件数が増えない。
- [x] 通常設定の LeakSanitizer で、スタイルキャッシュ由来のリークを報告しない。
- [x] シミュレータの既存画面と実機ビルドに回帰がない。

#### 実装の結果

| 判断 | 実装 |
|---|---|
| フレーム統計のスナップショット | `FmsApp::stats()` が値返し。`frame()` はローカルに組み立て、`std::mutex` 下でコピー 1 回。`builds` はフレームループ所有の `builds_` |
| LVGL リフレッシュ統計 | `fmsui::RefreshStats` (`components/fmsui/src/refresh.cpp`)。`take()` が区間の回数・合計・最大を 1 回で返し、次の区間を開始する |
| スタイルキャッシュの所有 | `render.cpp` 内の `StyleCache`。エントリは個別ヒープ確保でアドレス固定 |
| 解放 | `FmsApp::shutdown()` が タイマ停止 → ツリー破棄 → `releaseStyleCache()` の順に行う。シムの `--shot` 経路とテスト末尾が呼ぶ |
| 診断 | `fmsui::styleCacheStats()`。件数はロック付き公開スナップショットなので、実機の別タスクから安全に読める。実機の 1 秒ログとシムの `--stats` に出る |

**実測(スタイル種類数)**: `--demo catalog` が 124 lv_objs に対して text 13 / box 5、
`--demo pages` が 252 lv_objs に対して text 9 / box 5。タップを繰り返しても増えない。

**設計判断からの変更点が 1 つある。** 「エントリ数と最大到達数」の 2 つは持たず、件数だけを返す。
追い出しが無いので件数は単調増加であり、**件数そのものが最大到達数**だからである。
常に同じ値になるフィールドを 2 つ並べても読み手を迷わせるだけなので、
`StyleCacheStats` は `text_styles` / `box_styles` の 2 つだけにして、その理由をヘッダに書いた。
将来、追い出しを入れる判断をしたときは、そこで初めて別々の値になる。

統計の並行性は `test_refresh_stats_take_a_consistent_interval` と
`test_frame_and_style_stats_are_safe_to_read_during_frames` で確認する。後者は UI タスクが
新しいスタイルを追加している間にも別スレッドから件数を読み、公開用スナップショットが
ベクタの変更と競合しないことも確認する。

### 2026-09-09: UI 部品のヘッドレス操作テスト

**状態: 実装済み (2026-09-09)。完了条件 7 件をレビューで確認済み。**

フレームワークの入力経路を、デモアプリの機能ではなくテスト専用の最小画面で検証する。
主対象は `GestureDetector`、`FmsDropdown`、`FmsKeypad` とし、LVGL のポインタ入力から
ヒットテスト、コールバック、`setState()`、再構築、再描画までを通す。

#### 1. テストは実際のポインタ入力経路を通す

`lv_event_send()` で対象オブジェクトへイベントを直接送る方法は主経路にしない。
直接送信では、前後関係、クリック可能フラグ、全画面バリア、背後への入力漏れを検証できないためである。

- テスト専用のヘッドレス display と pointer indev を作る。
- 偽の時刻を進めながら press、保持、release、再構築後の安定待ちを行う `tap(x, y)` を用意する。
- テストごとに最小の Widget ツリーを `FmsApp` へ載せ、コールバック回数と公開状態を観測する。
- display、indev、`FmsApp`、スタイルキャッシュを確実に終了し、テスト順序へ依存させない。

この仕組みはまず `tests/fmsui_interaction_test.cpp` のテスト内部に閉じる。
シミュレータの `--tap` と目的は似ているが、コマンドライン処理や PNG 出力までは共有しない。
同じ入力手順の重複が実際に保守上の問題になった場合にだけ、後から小さな共通ヘルパーへ分離する。

#### 2. 最初に固定する操作契約

**GestureDetector**

- 1 回の通常タップで `on_press`、`on_release`、`on_tap` がそれぞれ 1 回だけ呼ばれる。
- コールバックを持たない detector はクリック対象にならず、背後の有効な対象へ入力が届く。
- 再構築でコールバックが有効・無効になっても、古いコールバックを呼ばず、Widget ツリーの形を崩さない。

**FmsDropdown**

- items 付きは本体タップで開き、指定行のタップで正しい index を 1 回通知して閉じる。
- バリアのタップは選択を通知せずに閉じ、背後の部品へ入力を漏らさない。
- items なしはポップアップを作らず、従来どおり `on_tap` を 1 回呼ぶ。
- 開閉後も本体側の永続 RenderObject / LVGL オブジェクトを不必要に作り直さない。
- 開閉を繰り返した後、閉じた状態の生存 LVGL オブジェクト数が基準値へ戻る。
- 一度必要な見た目を作った後は、同じ操作の反復でスタイルキャッシュ件数が増えない。

**FmsKeypad / FmsScratchpad**

- 数字配列と英字配列で、押したキーの文字が `on_key` に正しく渡る。
- `DEL`、`CLR`、`ENTER` はそれぞれ対応するコールバックだけを 1 回呼ぶ。
- 親の状態更新後、scratchpad が通常文字、メッセージ、エラー表示を正しく切り替える。

#### 3. テストの分離と判定範囲

操作テストは既存のレイアウト・差分検出テストとは実行ファイルを分け、CTest から両方を実行する。
これにより LVGL display、input device、`FmsApp` singleton の状態を既存テストから分離し、
失敗時にも「計算上のレイアウト」と「実際の入力連鎖」のどちらが壊れたかを判別しやすくする。

ホスト上のテストで確認できるのは、LVGL が受け取ったポインタ状態からフレームワーク内の処理までである。
実機タッチコントローラの座標変換、割り込み、ドライバ固有の挙動は証明しない。
実機確認を行った場合は、自動テスト結果と分けて記録する。

#### 対象外

- デモアプリへの機能追加や、FMS としての業務ロジック
- ピクセル単位の見た目を固定する golden / screenshot 比較
- `requestFrame()` の高頻度・長時間負荷試験
- 実機タッチドライバそのものの自動試験

これらは今回の操作契約を固定した後に、必要性を個別に判断する。

#### 実装完了の条件

- [x] 独立したヘッドレス操作テスト実行ファイルと決定的な `tap(x, y)` ヘルパーがある。
- [x] `GestureDetector` の通常、有効化・無効化、背後への入力伝達を検証している。
- [x] `FmsDropdown` の開く、選択して閉じる、バリアで閉じる、背後へ漏らさない動作を検証している。
- [x] Dropdown の反復開閉で、生存オブジェクト数とスタイル件数が安定することを検証している。
- [x] `FmsKeypad` の文字、`DEL`、`CLR`、`ENTER` と scratchpad の表示更新を検証している。
- [x] ASan / UBSan をフレームワーク本体にも適用し、その設定で新旧すべてのホストテストが成功する。
- [x] 既存の全ヘッドレスデモが起動・描画でき、操作テスト追加による回帰がない。

#### レビュー結果

2026-09-09 のレビューで、操作テスト単体は **134 checks, 0 failures**。
通常設定の CTest は `fmsui_test` と `fmsui_interaction_test` の 2 件が成功した。
初回レビューでは `FMSUI_SANITIZE` が各実行ファイルだけに適用され、`fmsui` 静的ライブラリが
未計装であることを検出した。`fmsui` 本体にも ASan / UBSan のコンパイルオプションを追加し、
`render.cpp`、`fms.cpp`、`app.cpp` などのコンパイル行に `-fsanitize=address,undefined` が
入ることを確認した。その状態で両テストを 20 回反復し、すべて成功した。

`m0`、`m1`、`catalog`、`pages`、`reorder`、`fplan` の全ヘッドレスデモも、
計装済みの `fmsui` をリンクした実行ファイルで起動・描画・PNG 出力まで成功した。
これにより未完了だった 1 件を満たし、この章を「実装済み」へ移した。

#### 実装の結果

| 判断 | 実装 |
|---|---|
| 実行ファイルの分離 | `tests/fmsui_interaction_test.cpp` → `fmsui_interaction_test`。`sim/CMakeLists.txt` が `fmsui` / `fmsui_fonts` / `lvgl` にリンクし、`add_test` で `fmsui_test` と並べる |
| 入力経路 | テスト専用の 1280x720 ヘッドレス display と `LV_INDEV_TYPE_POINTER` の indev。`lv_event_send()` は使わない |
| 決定的な時刻 | `lv_tick_inc(33)` + `lv_timer_handler()` を 1 ステップとする偽クロック。`FmsApp::setClock()` にも同じ偽マイクロ秒を入れる |
| `tap(x, y)` | press を 3 ステップ保持 → release → 4 ステップ。押下と解放が同じ read に入らないので、PRESSED と RELEASED が別々に上がる |
| 終了順序 | `Session` の RAII。`FmsApp::shutdown()`(ツリー破棄 → スタイル解放) → `lv_indev_delete` → `lv_display_delete` |
| テスト間の分離 | テストごとに `Session` を作り直す。display も indev も要素ツリーもスタイルキャッシュも持ち越さない |
| サニタイザ | `FMSUI_SANITIZE=ON` では各実行ファイルに加えて `fmsui` 静的ライブラリも ASan / UBSan で計装する。サニタイザランタイムは各実行ファイルがリンクする |

**タップ座標はテストに書かず、画面から読む。** 座標を直書きすると、テーマの
`row_height` が変わったときにタップは当たり続けるのに「テストが名指ししている部品」から
ずれる、という壊れ方をする。フレームワークが作る lv_obj は screen の直下に絶対座標で
並ぶので、ラベルの文字列から lv_obj を引き、その中心を叩く形にした
(`labelNamed()` / `tapLabel()`)。同じ文字列が 2 つ以上見つかったときは、
どちらかを黙って叩かずにテスト失敗として報告する。

**開閉の判定には、判定時に本体が表示していない項目を使う。** ドロップダウンの項目は
`CLB` / `CRZ` / `DES` で、`CRZ` をポップアップの存在確認に使う。反復テストでは行操作のため
`CRZ` を一度選ぶが、閉状態を判定する前に本体を `CLB` へ戻す。本体が `CLB` の時点では、
`CRZ` と読めるラベルが存在することが「リストが開いている」の曖昧さのない証拠になる。

**設計判断からの追加が 2 つある。** どちらも計画時に決めていなかった点で、
実装前に確認して決めた。

- フォントは生成済みの実フォント (`fmsui_fonts`) を使う。操作テストの実行ファイルだけが
  `fmsui_test` にない依存を 1 つ持つが、叩く部品が実機と同じ寸法で並ぶ。
- ヘッドレス画面は実機・シムと同じ 1280x720。`fmsui_test` の 320x240 では英字キーパッド
  7 列とポップアップが窮屈で、当たり判定が寸法に左右されやすい。

`FmsScratchpad` の状態は、テスト内の `EntryPage` という `StatefulWidget` が持つ。
scratchpad 自身は状態を持たない部品なので、これは近道ではなく実際のページと同じ形であり、
すべてのキーが `setState()` を通る経路になる。

### 2026-09-11: `requestFrame()` の高頻度・並行負荷試験

**状態: 実装済み (2026-09-11)。完了条件 9 件をレビューで確認済み。**

`requestFrame()` は更新内容を積むキューではなく、「次のフレームで最新の入力を読み直す」ための
集約可能な要求である。現在のテストは、別スレッドからの単発要求と、1 回のビルド中に届いた
要求が次回へ残ることを `BuildOwner` 単体で確認している。これを、継続的な要求、複数 producer、
実際の `FmsApp` フレームループまで含む負荷へ広げる。

#### 1. 固定する契約

- producer は共有値を、その値に必要な同期を行って公開した**後**に `requestFrame()` を呼ぶ。
- UI は要求ごとの履歴ではなく、ビルド時点で読める最新値を描画する。
- UI が処理する前に複数回届いた要求は 1 回のビルドへ集約してよい。
- `takeNeedsBuild()` が要求を取得した後、ビルド中に届いた要求は次のフレームまで残る。
- producer が停止した後に要求を処理し切れば、最後に公開された状態がビルドへ到達する。
- 要求を処理し切った後は dirty flag が下がり、入力が無ければ余分なビルドを続けない。
- `requestFrame()` は producer を待たせず、producer から `State::setState()` は呼ばない。

要求回数とビルド回数の一致は契約にしない。一致を要求すると、意図した coalescing を失敗として
扱ってしまうためである。代わりに、ビルド回数が要求回数を超えないこと、最後の状態が到達すること、
停止後に収束することを判定する。

#### 2. 決定的な負荷テスト

テストは `tests/fmsui_request_frame_test.cpp` の独立した実行ファイルにする。
並行テストが失敗・停止したときに既存のレイアウトテストや操作テストを巻き込まず、CTest の
短い timeout で停止を検出できるようにする。デモアプリは変更しない。

壁時計の sleep や「何秒以内なら成功」という速度閾値には依存しない。atomic の世代番号と
barrier / condition variable による同期点を使い、次のケースを有限回で再現する。

1. **停止中の burst**: UI が要求を取得しない間に多数回要求し、1 回へ集約されることを確認する。
2. **ビルド中の要求**: builder が開始した位置で producer と同期し、その後に届いた要求が
   次のフレームで処理されることを `FmsApp` 全体で確認する。
3. **単一 producer の継続更新**: 世代番号の公開と要求を高頻度に繰り返し、UI と並行にフレームを
   進める。観測値が逆戻りせず、drain 後に最終世代へ到達することを確認する。
4. **複数 producer の競合**: 複数スレッドがそれぞれ固定回数要求し、クラッシュ、停止、破損がなく、
   全 producer 停止後に pending request を処理し切れることを確認する。
5. **停止後の収束**: drain 後にさらに idle frame を進め、ビルド回数が増えないことを確認する。

負荷量は通常テストとサニタイザテストの双方で繰り返せる固定値にする。まず合計 100,000 要求程度を
上限の目安とし、実測でテスト時間が長すぎる場合は、競合状態を十分作れる範囲で下げて結果を記録する。

#### 3. 検証範囲

この試験が確認するのは、ホスト上の C++ スレッドと LVGL タイマーを使ったフレーム要求の
coalescing、公開順序、最終状態、収束である。ASan / UBSan はフレームワーク本体にも適用する。
ThreadSanitizer が実行可能な環境では追加証拠として使えるが、特定環境での TSan 起動可否は
完了条件に含めない。ストレステストの成功だけをデータ競合不存在の証明とはせず、atomic の
release / acquire と所有スレッドの設計もレビューする。

実機の FreeRTOS スケジューリング、ISR / IRAM 経路、レイテンシや fps の保証はこの試験では
証明しない。実機で負荷確認を行った場合は、ホストテストの結果と分けて記録する。

#### 対象外

- すべての中間値を順番に描画するキュー機能
- producer からの `State::setState()` 呼び出し
- 実時間ベースの性能合格値、fps、最大レイテンシ保証
- ISR からの直接要求と IRAM 安全性の追加試験
- デモアプリへの機能追加
- 長時間の実機耐久試験

#### 実装完了の条件

- [x] 独立した負荷テスト実行ファイルを CTest に登録し、停止を検出する timeout を設定する。
- [x] UI 停止中の burst が 1 回へ集約され、要求数より多くビルドしないことを検証する。
- [x] `FmsApp` のビルド中に届いた要求が次のフレームへ残ることを検証する。
- [x] 単一 producer の継続更新で観測値が逆戻りせず、drain 後に最終世代へ到達する。
- [x] 複数 producer の同時要求後に停止・破損せず、pending request を処理し切る。
- [x] drain 後の idle frame でビルド回数が増えず、処理が収束する。
- [x] 通常設定と、`fmsui` 本体を計装した ASan / UBSan 設定で新旧すべての CTest が成功する。
- [x] 固定負荷量、テスト時間、反復回数と結果をこの章へ記録する。
- [x] release / acquire の役割と、複数 producer で要求フラグを公開バリアとして使える範囲を正確に記録する。

#### レビュー結果

2026-09-11 のレビューで、新しい負荷テスト単体は **34 checks, 0 failures**。
通常設定と、`fmsui` 本体を計装した ASan / UBSan 設定の双方で、3 件の CTest を 20 回ずつ
反復してすべて成功した。レビュー時の合計時間は通常 6.01 秒、ASan / UBSan 8.79 秒。
CTest の `TIMEOUT 60`、100,000 要求の配分、各ケースの同期点、停止後の収束判定も
ソース上で確認した。

並行性レビューで見つかった説明の誤りも修正した。atomic 操作の不可分性だけでは、別の atomic や
非 atomic データへの書き込み順序を他スレッドへ公開できない。単一 producer の release/acquire が
作る公開順序と、複数 producer では各データに独立した同期が必要な境界を、公開 API のコメント、
DESIGN.md、テストの説明、下のレビュー用メモへ反映した。これにより 9 件目の完了条件も満たした。

#### 実装の結果

| 判断 | 実装 |
|---|---|
| 実行ファイルの分離 | `tests/fmsui_request_frame_test.cpp` → `fmsui_request_frame_test`。`fmsui` / `lvgl` / `Threads::Threads` にリンクし、CTest に `TIMEOUT 60` 付きで登録する。画面は既定フォントのラベル 1 つなので、`fmsui_fonts` は使わない |
| フレームループ | 実際の `FmsApp` を使う。320x240 のヘッドレス display と、`lv_tick_inc(10)` + `lv_timer_handler()` を 1 step とする偽クロック。`FmsApp` のタイマ周期が 10ms なので、1 step で走るフレームは最大 1 回 |
| スレッドの持ち分 | フレームループ、LVGL、要素ツリーはテストの main スレッドだけが触る。producer が触るのは atomic と `requestFrame()` だけ。`setThreadId()` を入れてあるので、producer が `setState()` を呼べば `markNeedsBuild()` の assert で落ちる |
| ビルド回数の数え方 | `FmsApp::stats().builds` の差分で数える。`init()` 自身の初回要求を数えないよう、各ケースは初回ビルド後の値を基準にする |
| 停止後の収束 (ケース 5) | 独立したケースにはせず、4 ケースすべてで使う共通判定 `drainThenIdle()` にした。producer 停止後の 1 フレームで残りを拾い、続く 32 フレームでビルド回数が増えないことを見る |
| 最終値の到達 | builder が読んだ値に加えて、画面のラベル文字列 (`Session::shows()`) でも確認する。レイアウトと描画を通って初めて「届いた」とする |

**ケースと判定**

| ケース | 負荷 | 判定 |
|---|---|---|
| 1. 停止中の burst | 1 スレッド × 10,000 要求。その間 UI はフレームを進めない | フレームを進めるまでビルドは 0 回。次の 1 フレームでちょうど 1 回ビルドし、builder の呼び出しも 1 回。最終値 10000 が表示される |
| 2. ビルド中の要求 | 1,000 ラウンド。1 ラウンドは 2 要求で、合計負荷には数えない | builder は値を読んだ後、producer が「公開 → `requestFrame()`」を終えるまで condition variable で待つ。1 フレーム目は前ラウンドの値、2 フレーム目で今回の値が表示され、3 フレーム目はビルドしない。不一致はラウンドごとに数え、最後に 0 件であることを判定する |
| 3. 単一 producer | 40,000 要求。フレームを並行に進める | 読んだ値が逆戻りしない。ビルド回数 ≤ 要求回数。drain 後に 40000 が表示され、生存 lv_obj 数は初期値のまま |
| 4. 複数 producer | 4 スレッド × 12,500 = 50,000 要求 | producer ごとの世代も共有の合計も逆戻りしない。全 producer の最終世代と合計 50000 が表示される。ビルド回数 ≤ 要求回数 |

合計負荷はケース 1・3・4 で 100,000 要求。実測で十分に短かったので、目安の量から減らしていない。

**並行性はチェックポイントで保証する。** producer は 1,000 世代ごとに、UI がその世代以上をビルドで読むまで
`std::atomic::wait` で止まる。最終世代では止まらないので、停止時に残った要求は drain が拾う。
これで、スケジューリングによらず負荷中に必ずビルドが挟まり、その回数はケース 3 で 39 回以上、
ケース 4 で 11 回以上になる。この下限も判定に入れている。待つのはテスト側で、`requestFrame()` は
待ち合わせの前に返っている。

**`requestFrame()` がビルドを待たないこと** はケース 2 で確認する。builder はフレームの途中で
producer の `requestFrame()` の完了を待つので、要求経路がビルドを待つ実装ならデッドロックし、
CTest の timeout で失敗になる。

**実測** (WSL2 Ubuntu 22.04 / gcc 11.4 / 8 コア、2026-09-11)

| 設定 | 新テスト 1 回 | CTest 20 回反復 (3 テスト) | 新テストの直接実行 20 回 |
|---|---|---|---|
| 通常 (`build-sim`) | 0.10 s | 全成功、合計 4.58 s | 20/20 成功 |
| ASan / UBSan (`build-asan`、`fmsui` 本体も計装) | 0.17 s | 全成功、合計 8.07 s | 20/20 成功、サニタイザの報告 0 件 |

各回とも 34 checks, 0 failures。負荷中に挟まったビルド回数は環境によって変わるので、下限以外は判定に使わず記録だけする。

| 設定 | 単一 producer (下限 39) | 4 producers (下限 11) |
|---|---|---|
| 通常 | 102〜157 | 44〜72 |
| ASan / UBSan | 69〜89 | 34〜52 |

**追加証拠: ThreadSanitizer (完了条件外)。** CMake は変更していない。一時ディレクトリで
`CMAKE_C_FLAGS` / `CMAKE_CXX_FLAGS` / `CMAKE_EXE_LINKER_FLAGS` に `-fsanitize=thread` を渡し、
LVGL と `fmsui` もまとめて計装した。この環境の gcc 11 の TSan は ASLR と衝突し、
`FATAL: ThreadSanitizer: unexpected memory mapping` で起動しない。そのため
`setarch $(uname -m) -R` で ASLR を切って実行した。`fmsui_test` (167 checks)、
`fmsui_interaction_test` (134 checks)、`fmsui_request_frame_test` (34 checks、0.82 s) の
3 つとも成功し、`WARNING: ThreadSanitizer` は 0 件だった。

**壊れた実装を検出できることの確認 (ミューテーション)。** 一時コピーのフレームワークを 1 か所ずつ壊して、
新テストを走らせた。4 つとも検出した。

| 壊し方 | 結果 |
|---|---|
| フラグをビルド前に取らず、ビルド後に下ろす (ビルド中の要求を失う) | ケース 2 が `request_lost = 1000` などで失敗した。続くケース 3 は、チェックポイントが満たされないまま停止した |
| 要求の有無にかかわらず毎フレームビルドする | 全ケースの idle frame 判定で失敗した (7 failures) |
| 要求をキューとして扱い、1 要求につき 1 ビルドする | ケース 1 の builder 呼び出し回数と、全ケースの idle frame 判定で失敗した (7 failures) |
| `requestFrame()` が、ビルド中に保持されるロックを待つ | ケース 2 で停止し、CTest が `***Timeout 60.04 sec` として失敗にした |

**atomic と所有スレッドについてのレビュー用メモ**

- producer 経路で触る共有状態は、`BuildOwner::needs_build_` (`std::atomic<bool>`) と、アプリ側が持つ公開値の
  atomic だけ。`FmsApp::instance()` は関数内 static なので、初期化を含めて並行に呼んで安全。
- 単一 producer では、共有値への書き込み後に dirty flag を release store し、UI の acquire exchange が
  その store を読んだとき、synchronizes-with と各スレッド内の順序によって書き込みがビルドへ公開される。
  atomic 操作の不可分性だけではこの順序は作れず、`release` / `acquire` は正しさに必要である。
- 複数 producer が同じ dirty flag へ release store しても、UI の 1 回の acquire exchange はすべての
  producer の store と同期するわけではない。新テストでは producer ごとの公開値と合計値そのものが atomic
  なのでデータ競合しない。要求フラグはビルドを集約するためだけに使い、複数の非 atomic な公開元に対する
  汎用のメモリバリアとして扱わない。各公開元には atomic、ロック、キューなど独立した同期が必要である。
- `BuildOwner` の `bound_` / `ui_thread_` は atomic ではない。書くのはフレームループの最初のフレームだけで、
  他スレッドから読むのは、誤って `setState()` したときの診断 (assert) だけ。要求経路はこれらに触らない。

**設計判断からの追加。** 計画に値が無かった次の 4 点は、実装前に確認して決めた。

- CTest の timeout は 60 秒。
- 負荷の配分は burst 10,000 / 単一 producer 40,000 / 4 producers × 12,500。ビルド中の要求は 1,000 ラウンドで、合計には含めない。
- 並行性はチェックポイント同期 (1,000 世代ごと) で保証する。
- TSan は CMake を変えず、一時ビルドで試す。

確認せずに決めたのは次の 3 点。

- 停止後の収束を独立ケースにせず、全ケースの共通判定にした。
- 反復回数は前章に倣って 20 回にした。
- README のテスト表と DESIGN.md の「requestFrame() が安全な理由」に、新しい実行ファイルを 1 段落ずつ追記した。

### 2026-09-11: 実機ビルド設定・警告・文書整合性の整理

**状態: 実装済み (2026-09-11)。完了条件 8 件をレビューで確認済み。**

並行性、ライフタイム、操作経路の自動テストが揃ったため、次は日常のビルドで新しい問題を
見落とさない状態を作る。対象はフレームワークと検証環境の保守性であり、デモアプリの機能追加や
FMS の業務ロジックは含めない。

#### 1. 使用していない LVGL Examples / Demos をビルドしない

現在の実機用 `sdkconfig` は `CONFIG_LV_BUILD_EXAMPLES` と `CONFIG_LV_BUILD_DEMOS` が有効だが、
FmsLikeUI はそれらのソースを使用していない。最終バイナリへリンクされない場合でも、クリーンビルドの
対象と設定の意図を増やすため、再生成可能なプロジェクト設定で明示的に無効化する。

- `sdkconfig.defaults` を設定の基準にし、既存の `sdkconfig` も同じ状態へ同期する。
- クリーンな実機ビルドのログまたは生成されたビルド情報で、`lvgl/examples` と `lvgl/demos` の
  ソースがコンパイル対象に入らないことを確認する。
- ビルド時間は環境差が大きいため合格値にはせず、必要なら変更前後の参考値だけを記録する。
- LVGL 本体、描画、入力、フォントの設定はこの作業では変更しない。

#### 2. プロジェクト側の警告を原因から解消する

既知の警告は主に C++20 の指定初期化で、省略可能な `FmsButtonArgs::text2`、
`FmsFieldBoxArgs::text` / `unit`、`FmsDropdownArgs::text` などを省いた箇所から出ている。

- 本当に省略可能な引数は Args 側の既定値で、その意図を API として表す。
- 必須値または呼び出し側の意図を示すべき値は、利用箇所で明示する。
- `-Wno-missing-field-initializers` の追加や `-Wall` / `-Wextra` の削除で警告を隠さない。
- `third_party/` と `managed_components/` の外にあるプロジェクト所有ソースを警告 0 件にする。
- 警告修正のためだけに Widget の表示、操作契約、既定の見た目を変えない。

#### 3. 現在の実装に文書を合わせる

少なくとも次の既知のずれを修正する。

- `FmsDropdown` は開閉状態を内部に持つ `StatefulWidget` なので、`fms.h` と DESIGN.md の
  「FMS ウィジェットはすべて StatelessWidget」という説明を訂正する。
- README の `tests/` の説明へ `requestFrame()` の並行負荷テストを含める。
- `PLAN.md` は当初計画として残し、完了状況を後付けで混在させない。現在の判断と結果は
  DECISIONS.md と DESIGN.md に記録する。

#### 対象外

- デモアプリのボタン、画面遷移、入力検証などの機能追加
- クリッピング、スクロール、アニメーション、GlobalKey、dirty サブツリー再ビルド
- LVGL のバージョン更新、PPA 描画設定、描画バッファやフレーム周期の再調整
- CI の導入、配布パッケージ、プロジェクト本体のライセンス決定
- 実機の性能最適化と、ビルド時間に対する固定の合格値

CI はこの整理後に、警告の無い再現可能なビルドと既存の 3 CTest を自動化する別計画として扱う。

#### 実装完了の条件

- [x] 再生成可能な設定で LVGL Examples / Demos を無効化する。
- [x] クリーンな実機ビルドで `lvgl/examples` / `lvgl/demos` がコンパイルされないことを確認する。
- [x] 通常のシミュレータビルドと実機ビルドで、プロジェクト所有ソースの警告を 0 件にする。
- [x] 警告を無効化するコンパイラオプションを追加せず、省略可能な値と必須値の意図をコードで表す。
- [x] 通常設定と、`fmsui` 本体を計装した ASan / UBSan 設定で 3 件の CTest がすべて成功する。
- [x] 6 種類の既存デモをヘッドレスで起動・描画でき、表示または操作契約に回帰がない。
- [x] `fms.h`、DESIGN.md、README の既知の説明ずれを修正する。
- [x] 実装結果、検証コマンド、警告件数、Examples / Demos 除外の証拠をこの章へ記録する。

実機への書き込みと物理タッチ確認は、この作業がビルド対象と既定値・文書だけを整理する限り
完了条件に含めない。表示や操作を変える必要が生じた場合は、その変更について別途実機確認を行う。

#### レビュー結果

2026-09-11 のレビューで、実装と記録に未解決の指摘は無かった。

- `sdkconfig` と `build-clean/config/sdkconfig.cmake` で Examples / Demos が無効なことを確認した。
  `build-clean/compile_commands.json` を再集計すると全 1594 件のうち `lvgl/examples` と
  `lvgl/demos` はともに 0 件で、LVGL 本体 530 件、`fmsui` 8 件、フォント 5 件、main + demo 7 件は残っていた。
- 通常設定と ASan / UBSan 設定をそれぞれ `--clean-first` で再構築した。どちらも 561 ステップを
  完了し、ビルドログの `warning:` は 0 件だった。ASan / UBSan 側の compile commands では
  `fmsui` の 8 ソースすべてに sanitizer と frame-pointer のオプションが付いていた。
- 両設定で 3 件の CTest がすべて成功した。6 デモを両設定で計 12 回ヘッドレス描画し、同じデモの
  PNG は 6 組とも一致した。ASan / UBSan / LeakSanitizer の報告も無かった。
- 実装時の実機クリーンビルドの生成物とログを確認したうえで、現行ソースを
  `tools\\idf.bat -B build-clean build` でも再確認した。ESP-IDF 5.5.4 のビルドは警告なく成功し、
  `fmslikeui.bin` は 906,832 bytes、アプリ領域は 41% 空きだった。
- 警告を無効化する設定変更は無く、Args の既定値、`render.cpp` の型と未使用引数、`main.cpp` の
  デモ別コンパイル範囲を原因から修正している。文書の 3 件のずれと PLAN.md の履歴上の混在も解消されている。

#### 実装の結果

| 判断 | 実装 |
|---|---|
| Examples / Demos の無効化 | `sdkconfig.defaults` に `# CONFIG_LV_BUILD_EXAMPLES is not set` と `# CONFIG_LV_BUILD_DEMOS is not set` を追加し、既存の `sdkconfig`(git 管理外)も同じ値へ同期した。LVGL 本体の設定 (`third_party/lv_conf.h`) は変えていない |
| 無効化が効く理由 | 最上位 CMakeLists は `LV_KCONFIG_IGNORE` で LVGL の Kconfig を設定としては使わないが、LVGL の `env_support/cmake/esp.cmake` はこの 2 つを読んでコンパイル対象を決める。Kconfig の既定値は両方 `y`。この理由を `sdkconfig.defaults` のコメントに書いた |
| 省略可能な値 | `fms.h` の Args で、空に意味がある `Str` 6 つに `{}` を付けた。`FmsButtonArgs::text2`、`FmsValueArgs::unit`、`FmsFieldBoxArgs::text` / `unit`、`FmsDropdownArgs::text`、`FmsScratchpadArgs::message` |
| 必須値 | Label / Button / Radio / Value / Text の `text` などには既定値を持たせず、省略すれば `-Wmissing-field-initializers` が出るままにした。この方針を `fms.h` の冒頭と、DESIGN.md「省略してよいフィールドには `{}` を書く」に書いた |
| シムだけで出ていた警告 | `render.cpp` の 3 件。LVGL 9.5 の `lv_obj_get_index()` は `int32_t` を返すので、`uint32_t` へのキャストを外した (`-Wsign-compare`、比較結果は変わらない)。未使用の `ctx` は既存の書き方に合わせて `(void)ctx;` にした (`-Wunused-parameter`)。ESP-IDF は `-Wno-sign-compare -Wno-unused-parameter` を付けるので、実機では出ていなかった |
| m0 デモだけで出ていた警告 | `main.cpp` の `thread_id()` は `FMSUI_DEMO_M0` では使われず、`-Wunused-function` になっていた。使う側と同じ `#if !defined(FMSUI_DEMO_M0)` で定義を囲んだ。既定のデモでは出ないので、`FMSUI_DEMO` を切り替えたビルドで初めて見つかった |
| 警告を隠す設定 | CMakeLists、`sdkconfig`、コンパイルオプションの警告フラグには手を入れていない |
| 文書 | `fms.h` 冒頭と DESIGN.md「FMS ウィジェット」を、`FmsDropdown` だけが StatefulWidget だという記述に訂正した。README の `tests/` に並行負荷テストを加えた。PLAN.md から後付けの注記(`fmsui_port_esp` を作らなかった理由、ebc7837 で追加)を消し、DESIGN.md の新しい節「実機用のポート層は作っていない」へ移した |

**警告件数。** プロジェクト所有ソース(`third_party/`、`managed_components/`、ESP-IDF 本体の外)の `warning:` 行を数えた。

| ビルド | 変更前 | 変更後 |
|---|---|---|
| シム (`build-sim`、WSL2 Ubuntu 22.04 / gcc 11.4、`-Wall -Wextra`) | 36 件。`-Wmissing-field-initializers` 33 件(demo 29、`fms.cpp` 4)と `render.cpp` 3 件 | 0 件。LVGL も含めたクリーンビルド全体で 0 件 |
| ASan / UBSan (`build-asan`、`fmsui` 本体も計装) | 数えていない(コンパイラも警告フラグもシムと同じ) | 0 件。クリーンビルド全体で 0 件 |
| 実機・既定デモ (ESP-IDF 5.5.4) | 33 件。すべて `-Wmissing-field-initializers`(9/9 のログ `build/log/idf_py_stdout_output_8824`) | 0 件。ESP-IDF 本体を含むクリーンビルド全体で 0 件 |
| 実機・`FMSUI_DEMO` = `m0` / `m1` / `catalog` / `fplan` / `reorder` / 既定 | `fms.h` と `render.cpp` を直した後、`main.cpp` を直す前の時点で、`m0` だけ 1 件 | 6 通りとも 0 件。どれも `main.cpp` を再コンパイルしたビルドで確認した |

シムの変更前の件数は、ソースを変える前にプロジェクト所有ターゲットのオブジェクト 23 個だけを消して再ビルドし、そのログから数えた。

**Examples / Demos を除外した証拠。** 変更前は 9/9 の実機ビルド `build/` の値、変更後は新しいディレクトリ `build-clean/` のクリーンビルドの値。
どちらも `compile_commands.json` を JSON として読み、ファイルのパスで分類して数えた。

| | 変更前 (`build/`、9/9) | 変更後 (`build-clean/`) |
|---|---|---|
| コンパイル対象の総数 | 1852 | 1594 |
| `third_party/lvgl/examples/` | 257 | 0 |
| `third_party/lvgl/demos/` | 1 (`lv_demos.c`) | 0 |
| `lvgl/src` / `fmsui` / `fmsui_fonts` / `main` + `demo` | 530 / 8 / 5 / 7 | 530 / 8 / 5 / 7 |

総数の差 258 件は、examples と demos の合計とちょうど一致する。それ以外の分類の件数は変わっていない。
`build-clean/config/sdkconfig.cmake` でも、`CONFIG_LV_BUILD_EXAMPLES` と `CONFIG_LV_BUILD_DEMOS` はどちらも空値で、ビルドログに examples / demos のコンパイル行は 0 件だった。
ninja のステップ数は、9/9 のフルビルドのログが 1973、`build-clean/` が 1729 だった。ただし別の日の別の実行なので、この差 (244) は参考値にとどめ、除外の判定には使っていない。

**ビルド時間(参考)。** `build-clean/` のクリーンビルドは 403.5 秒 (09:00:03〜09:06:47) だった。
ただし 09:02:50 以降は、WSL でシムのクリーンビルドを並行して走らせていたため、単独で走らせたときより長く出ている。
変更前との比較はしていない。

**テストとデモ**

- CTest は、通常設定と ASan / UBSan 設定のそれぞれで、クリーンビルドの後に `--repeat until-fail:20` で実行した。どちらも 3 件すべて成功した(所要時間は 4 秒と 12 秒)。
- テストを直接実行した結果は、両設定とも `fmsui_test` 167 checks、`fmsui_interaction_test` 134 checks、`fmsui_request_frame_test` 34 checks で、いずれも 0 failures。
- 変更前のソースでヘッドレス PNG を 9 枚撮った。内訳は 6 デモ、`reorder --rows 40`、README の catalog タップ連鎖、pages へのタップ。変更後の `build-sim` と `build-asan` でも同じ 9 枚を撮り、合計 18 組をバイト比較して、すべて一致した。9 枚どうしは互いに異なることも確認してある。ASan 版のデモ実行で、サニタイザの報告は 0 件だった。
- pages へのタップ (`--tap 100,97`) は画面を変えず、pages と同じ PNG になった。この組で分かるのは「表示が回帰していない」ことまでで、タップによる表示の変化は確かめていない。タップ連鎖で表示が変わることは、catalog の組で確認した。
- `main.cpp` は実機専用で、シムもテストもコンパイルしない。そのため `main.cpp` を直した後にシム側の再検証はしていない。

**検証コマンド**

```bash
# WSL: シムと ASan / UBSan
cmake --build build-sim  --clean-first
cmake --build build-asan --clean-first
ctest --test-dir build-sim  --output-on-failure --repeat until-fail:20
ctest --test-dir build-asan --output-on-failure --repeat until-fail:20
./build-sim/fmsui_sim --demo catalog --shot catalog.png   # m0 / m1 / pages / reorder / fplan も同じ
```

```bat
REM Windows: 実機
tools\idf.bat -B build-clean build
tools\idf.bat -B build-clean -DFMSUI_DEMO=m0 build
REM m1 / catalog / fplan / reorder と、既定 (-DFMSUI_DEMO=) も同じ
```

**実装前に確認して決めたこと**

- 既定値 `{}` は、空に意味がある値すべてに付ける。警告の出ていた 5 つに、`FmsScratchpadArgs::message` を加える。
- `FmsFieldBoxArgs::text` は省略可能にする(`empty` のときは使わないため)。
- PLAN.md の後付けの注記は消して、DESIGN.md へ移す。
- 実機のクリーンビルドは `build/` に触れず、別ディレクトリ `build-clean/` で変更後の分だけ取る。

**確認せずに決めたこと**

- `render.cpp` と `main.cpp` の警告は、計画にあった既知の一覧には入っていなかった。どちらもプロジェクト所有ソースの警告なので、同じ方針で原因から直した。
- 実機の警告確認は、既定のデモだけでなく、`FMSUI_DEMO` の 6 通りすべてで行った。
- DESIGN.md のポート層の節は、「触れないもの / まだ無いもの」の直前に置いた。
- `build-clean/` は消さずに残してある(`/build*/` なので git の管理外)。その `FMSUI_DEMO` は既定に戻してある。

### 2026-09-11: GitHub Actions による継続的インテグレーション

**状態: 実装済み (2026-09-11)。完了条件 10 件をレビューで確認済み。**

ローカルでは通常・sanitizer・実機向けのビルドとテストが揃ったが、変更のたびに人がすべてを
再実行しなければ回帰を検出できない。GitHub Actions で同じ品質ゲートを再現し、pull request では
ホスト上のフレームワーク検証を必須の基準にする。ESP-IDF の実機向けコンパイルは時間と依存取得が
大きいため、最初は `master` への push と手動実行に分離する。

#### 1. Pull request と push のホスト CI

Linux の GitHub-hosted runner で、次の二つを独立した job として実行する。

1. **通常設定**: Debug 構成でクリーン configure / build を行い、3 件の CTest を実行する。
2. **ASan / UBSan 設定**: `FMSUI_SANITIZE=ON` で同じ 3 件を実行し、`fmsui` 静的ライブラリ本体も
   sanitizer と frame-pointer 付きでコンパイルされていることを確認する。

共通の契約は次のとおり。

- `pull_request`、`master` への `push`、`workflow_dispatch` で起動する。
- checkout 時に `third_party/lvgl` サブモジュールを再帰的に取得する。
- CMake、Ninja、SDL2 の必要最小限の依存だけを導入する。
- `fmsui`、シミュレータ、3 テストなどプロジェクト所有の target だけを警告エラー扱いにする。
  `third_party/lvgl` の警告をプロジェクト側の責任として固定しない。
- `m0`、`m1`、`catalog`、`pages`、`reorder`、`fplan` の 6 デモをヘッドレス描画し、PNG が空でないことを確認する。
  sanitizer job でも同じ経路を通し、sanitizer の報告を失敗にする。
- CTest と job の timeout を設定し、並行負荷テストの停止を有限時間で失敗へ変える。
- 同じ workflow / ref の古い実行は `concurrency` でキャンセルする。
- workflow の権限は `contents: read` を基本とし、書き込み権限や secret を要求しない。

初版ではキャッシュを入れない。依存・サブモジュール・生成物を含めた素の再現性と実測時間を先に確認し、
時間が問題になった場合だけ、キーと無効化条件を設計して追加する。

#### 2. ESP-IDF の device-build CI

ESP-IDF **5.5.4** を固定した公式環境で ESP32-P4 向けにビルドする。実機への flash は行わない。

- `master` への `push` と `workflow_dispatch` で起動し、初版では pull request の必須 job にしない。
- `sdkconfig.defaults` から新しい build directory を構成し、既存のローカル `sdkconfig` に依存しない。
- 最初に既定デモをクリーンビルドし、続けて `m0`、`m1`、`catalog`、`fplan`、`reorder` と既定の
  6 構成を同じ環境でコンパイルする。
- `main`、`demo`、`components/fmsui` などプロジェクト所有コードの警告を失敗にする一方、ESP-IDF、
  managed component、LVGL の外部コードへ一律の `-Werror` は掛けない。
- 生成された `compile_commands.json` を検査し、`third_party/lvgl/examples` と
  `third_party/lvgl/demos` が 0 件であることを機械判定する。
- `fmslikeui.bin` のサイズと最小 app partition の空き容量を job summary に残す。
- component manager が取得する依存は `dependencies.lock` ではなく `main/idf_component.yml` が現在の基準なので、
  初回CIの解決結果を確認してから lock file を管理対象に戻すかを別途判断する。

device-build の実行時間と安定性が確認できた後で、pull request の必須 job に昇格するか、変更パスで
起動を絞るかを判断する。最初から path filter を入れて未検証の変更を取りこぼさない。

#### 3. workflow の保守と証拠

- GitHub Action とESP-IDF環境はバージョンまたは不変の参照へ固定し、選定理由をworkflow内のコメントに残す。
- ローカル専用の絶対パス、COMポート、WSL依存のコマンドはworkflowへ持ち込まない。
- 成功時に大量のPNGや中間生成物を保存しない。失敗調査に必要なログや画像だけを、短い保持期間のartifactにする。
- README にCIの対象範囲を記載する。badgeは実際のGitHub-hosted runが成功してから追加する。
- ローカル実行、workflow構文確認、GitHub-hosted runを別の証拠として記録し、YAMLを書いただけで完了扱いにしない。

#### 対象外

- 実機への自動flash、USB接続されたself-hosted runner、物理タッチ試験
- PNGのgolden画像をリポジトリへ保存するスクリーンショット差分試験
- ThreadSanitizer、長時間耐久試験、定期スケジュール実行
- release作成、署名、配布パッケージ、GitHub Pages
- branch protectionやrequired checkのGitHubリポジトリ設定変更

#### 実装完了の条件

- [x] 通常設定とASan / UBSan設定の独立したホストjobがあり、pull request・`master` push・手動で起動できる。
- [x] checkoutがLVGLサブモジュールを含み、クリーンなGitHub-hosted runnerで依存導入から完走する。
- [x] 両ホストjobで3件のCTestと6デモのヘッドレス描画が成功する。
- [x] sanitizer jobで`fmsui`本体の計装を確認し、ASan / UBSan / LeakSanitizerの報告を失敗にする。
- [x] プロジェクト所有targetの警告をエラーにし、外部コードへ同じ方針を強制しない。
- [x] ESP-IDF 5.5.4のdevice-build jobが、`master` pushと手動実行で6種類の実機向け構成をビルドする。
- [x] device-buildがLVGL Examples / Demos 0件を機械判定し、binサイズとapp領域の空きをsummaryへ記録する。
- [x] workflowの権限、timeout、concurrencyが明示され、secretやローカル固有値へ依存しない。
- [x] READMEへCIの対象と非対象を記載し、GitHub-hosted run成功後にbadgeを追加する。
- [x] ローカル検証とGitHub-hosted runnerの実行結果、所要時間、job名、失敗時artifactの内容をこの章へ記録する。

GitHub-hosted runとbadge確認には、workflowを含むcommitをGitHubへpushする必要がある。実装レビューでは
ローカルのビルド成功とGitHub上のrun成功を分け、pushされていない段階では最後の完了条件をチェックしない。

#### 実装の結果

| 判断 | 実装 |
|---|---|
| workflow | `.github/workflows/ci.yml` の 1 ファイルに 3 job。`host (debug)` と `host (asan-ubsan)` は matrix の 2 job で、`fail-fast: false` なので互いに止め合わない。3 つ目が `device-build (esp32p4)` |
| 起動 | `pull_request`、`master` への `push`、`workflow_dispatch`。device-build は `if` で push と手動のときだけ走る |
| 権限・停止 | `permissions: contents: read`。checkout は `persist-credentials: false`、secret は使わない。`concurrency` は `${{ github.workflow }}-${{ github.ref }}` で古い実行をキャンセルする。timeout はホスト job 30 分、device job 60 分、CTest は `--timeout 120`(TIMEOUT を持たないテストの既定値。`fmsui_request_frame_test` は自前の 60 秒のまま) |
| runner と依存 | `ubuntu-22.04`。導入するのは `ninja-build` と `libsdl2-dev` だけで、CMake とコンパイラは runner image のものを使う |
| 固定 | actions/checkout と actions/upload-artifact はどちらも v7.0.1 を commit SHA で指定する。ESP-IDF は `espressif/idf:v5.5.4@sha256:b9f2d6ea…` と digest で指定する。tag は動かせるが SHA / digest は動かないという理由を、workflow 冒頭のコメントに書いた |
| 警告のエラー化 | CMake オプション `FMSUI_WERROR`(既定 OFF、CI で ON)。シムでは `sim/CMakeLists.txt` が `fmsui` / `fmsui_fonts` / `fmsui_sim` / 3 テストに `-Werror` を付ける。実機では最上位 `CMakeLists.txt` の `FMSUI_WERROR_FLAGS` を、`main`(`demo/` を含む)/ `fmsui` / `fmsui_fonts` の各コンポーネントが ESP-IDF の既定オプションの後ろへ足す |
| sanitizer の報告を失敗にする | job の環境変数 `ASAN_OPTIONS=halt_on_error=1:detect_leaks=1` と `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`。加えて `tools/ci/check_no_sanitizer_reports.sh` が、CTest の `LastTest.log` と各デモのログに報告が無いことを確かめる |
| `fmsui` 本体の計装 | `tools/ci/check_sanitized.py`。`compile_commands.json` で、`components/fmsui/src/*.cpp` の全ソースに `-fsanitize=address,undefined` と `-fno-omit-frame-pointer` が付いていることを確かめる |
| デモ | `tools/ci/render_demos.sh`。6 デモをそれぞれ `timeout 120` 付きで `--shot` し、終了コード 0、空でない、PNG シグネチャ、sanitizer 報告なし、の 4 点で判定する。1 つ失敗しても残りを走らせる |
| device-build | `tools/ci/device_build.sh`。存在しない build dir から始め、`-DSDKCONFIG=<build>/sdkconfig -DIDF_TARGET=esp32p4 -DFMSUI_WERROR=ON` を付けて、既定 → `m0` → `m1` → `catalog` → `fplan` → `reorder` の順にビルドする。サイズは各構成の後に `check_sizes.py` を直接呼んで取る |
| Examples / Demos 0 件 | `tools/ci/check_lvgl_sources.py`。既定構成の `compile_commands.json` を置き場所で分類し、`third_party/lvgl/examples/` か `third_party/lvgl/demos/` が 1 件でもあれば失敗にする |
| job summary | 6 構成の bin サイズ・最小 app 領域・空き、コンパイル対象の内訳、lock が変わったかどうか、ビルド後の `dependencies.lock` |
| 失敗時の artifact | 失敗した job だけが保存し、保持は 7 日。ホスト job は `LastTest.log` と `ci-out/demos/`(PNG と各デモのログ)。device job は各構成のログ、summary、lock(コミット時点とビルド後)、`build-ci/sdkconfig`、`build-ci/log/` |
| 依存の固定 | `dependencies.lock` をコミットし、`.gitignore` から外した。`device_build.sh` はビルドの前後で lock を比べ、書き換わっていたら失敗にする |

**ESP-IDF 側の警告は `-Werror` を足すだけではエラーにならない。** ESP-IDF 5.5.4 の既定
(`CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS=y`)では、すべてのソースが
`-Wall -Werror=all -Wno-error=unused-function -Wno-error=unused-variable -Wno-error=unused-but-set-variable -Wno-error=deprecated-declarations -Wextra -Wno-error=extra`
でコンパイルされる。gcc は、後ろに付けた `-Werror` で個別の `-Wno-error=<名前>` を取り消さない。
gcc 11 に同じフラグ列で試すと、`-Werror` を足しただけでは `missing-field-initializers` しかエラーにならず、
`unused-variable` と `unused-function` は警告のまま残った。そこで `FMSUI_WERROR_FLAGS` には、
`-Werror` に加えて ESP-IDF が戻した 5 つを `-Werror=<名前>` で並べ直した。実機のコンパイラ (gcc 14) でも
同じように効くことは、下のミューテーション D1 で確認した。

**依存を解決し直すと、device-build が壊れていた。** 計画では「`dependencies.lock` ではなく
`main/idf_component.yml` が基準なので、初回 CI の解決結果を見てから lock を管理対象に戻すか判断する」としていた。
その初回の解決は、GitHub に push する前の、同じイメージを使ったローカル再現で起きた。lock の無いクリーンなツリーでは、
`managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c:160` が
`'esp_lcd_dpi_panel_event_callbacks_t' has no member named 'on_frame_buf_complete'` で失敗した
(1729 ステップ中 1646 ステップ目で停止)。手元の lock (2026-07-12) と、解決し直した結果の差は次の 5 件だった。

| component | 手元の lock | 解決し直した結果 |
|---|---|---|
| `espressif/m5stack_tab5` | 1.2.0~1 | 1.3.0 |
| `espressif/esp_lvgl_port` | 2.8.0~1 | 2.9.0 |
| `espressif/usb` | 1.4.1 | 1.5.0 |
| `espressif/esp_lcd_touch_gt911` | 1.2.0~2 | 1.2.1 |
| `espressif/esp_sccb_intf` | 0.0.8 | 0.0.9 |

`esp_lvgl_port` 2.9.0 は `ESP_IDF_VERSION >= 5.5.0` のときに `on_frame_buf_complete` を使う。
しかし ESP-IDF v5.5.4 の `esp_lcd_mipi_dsi.h` にあるのは `on_color_trans_done` と `on_refresh_done` だけで、
2.9.0 側の版判定が 5.5.x のリリース系列と合っていない。BSP 1.3.0 は `esp_lvgl_port: ^2` を要求するので、
範囲指定だけではこの版を避けられない。確認のうえ、実機で動いている 7/12 の lock をコミットすることにした。
lock を置いた同じツリーでは 6 構成すべてが通り、既定構成の `fmslikeui.bin` は前章の Windows でのクリーンビルドと同じ
906,832 bytes / 空き 41% になった。

**ローカル検証**(2026-09-11。WSL2 Ubuntu 22.04 / gcc 11.4 / CMake 3.22.1 / 8 コア、Docker Desktop 29.7.2)

GitHub のチェックアウトに近づけるため、作業ツリーをそのまま使わずにスナップショットから検証した。
スナップショットは、`git ls-files -co --exclude-standard` のファイルと LVGL サブモジュールの追跡ファイルだけを tar にしたもので、
ホストは WSL の ext4 上、device はコンテナの中へ展開した。コマンドと環境変数は workflow と同じものを使った。

| 対象 | 結果 |
|---|---|
| actionlint 1.7.12 + shellcheck 0.11.0 | `ci.yml` はエラー 0 件。`tools/ci/*.sh` への shellcheck も指摘 0 件 |
| `host (debug)` 相当 | 561 ステップ、ビルド 19 秒、`warning:` 0 件。CTest 3/3 成功 (0.17 秒)。6 デモ成功 |
| `host (asan-ubsan)` 相当 | 561 ステップ、ビルド 20 秒、`warning:` 0 件。`fmsui` 8/8 ソースが計装済み。CTest 3/3 成功 (0.27 秒)。6 デモ成功、sanitizer の報告 0 件 |
| `-Werror` が付いた範囲(ホスト) | `fmsui` 8、`fmsui_fonts` 5、`demo/` 6、`sim/` 1、`tests/` 3 の各ソースに付き、LVGL の 531 ソースには付いていない |
| `FMSUI_WERROR_FLAGS` が付いた範囲(実機) | 6 フラグすべてが `fmsui` 8、`fmsui_fonts` 5、`demo/` 6、`main/` 1 に付き、LVGL 530 と managed component 105 には 1 つも付いていない。ESP-IDF 本体では 141 ソースに `-Werror` があったが、すべて mbedtls 自前のもので、`FMSUI_WERROR` なしの `build-clean/` にも同じ 141 件がある |
| device-build 相当(lock なし) | 上記のとおり `esp_lvgl_port` 2.9.0 で失敗 (118 秒) |
| device-build 相当(手元の lock) | 6 構成成功 (222 秒)。コンパイル対象 1594 件、Examples / Demos 0 / 0 件、6 構成のログで `warning:` 0 件。ビルド後も lock は変わらなかった |
| device-build 相当(最終ツリー) | lock をコミット対象に含め、`device_build.sh` に lock の判定を入れた後のツリーを、あらためて空の状態から実行した。6 構成成功 (221 秒)。サイズ、コンパイル対象の内訳、`warning:` 0 件は上と同じで、summary は「dependencies.lock: unchanged by the build」 |

6 デモの PNG はどれも 2,765,798 bytes だった。`sim/png_write.h` が deflate を無圧縮ブロックで書くので、
1280x720 ならサイズは同じになる。中身の md5 は 6 枚とも異なっていた。

device-build の bin サイズ(手元の lock、最小 app 領域はどれも 1,536,000 bytes)

| 構成 | `fmslikeui.bin` | 空き |
|---|---:|---:|
| 既定 | 906,832 bytes | 629,168 bytes (41%) |
| `m0` | 878,464 bytes | 657,536 bytes (43%) |
| `m1` | 912,688 bytes | 623,312 bytes (41%) |
| `catalog` | 934,272 bytes | 601,728 bytes (39%) |
| `fplan` | 901,792 bytes | 634,208 bytes (41%) |
| `reorder` | 918,336 bytes | 617,664 bytes (40%) |

所要時間は、ホストと device を同じマシン(同じ WSL2 VM)で並行して走らせた回を含むので、参考値にとどめる。

**壊れた実装を検出できることの確認 (ミューテーション)。** スナップショットを 1 か所ずつ壊し、CI と同じ手順で
インクリメンタルに再ビルドした。どれも元に戻した後で、再び全手順が成功することも確認した。

| # | 壊し方 | 結果 |
|---|---|---|
| M1 | `components/fmsui/src/theme.cpp` に未使用の static 変数を追加 | `FMSUI_WERROR=ON` ではビルドが `-Werror=unused-variable` で失敗した。OFF では警告 1 件でビルドもテストも通った |
| M2 | `third_party/lvgl/src/core/lv_obj.c` に `#warning` を追加 | 警告 1 件が出たが、ビルド・CTest・デモはすべて成功した(外部コードの警告では落ちない) |
| M3 | `FmsApp::init` に符号付き整数のオーバーフローを追加 | asan-ubsan で CTest 3/3 が失敗し、ログ検査も失敗した。デモは `m0`(`FmsApp` を使わない)以外の 5 つが失敗した。`UBSAN_OPTIONS` から `halt_on_error` を外すと CTest は通ったが、ログ検査とデモ 5 つが「sanitizer report」で失敗した。debug 設定はすべて通った |
| M4 | `FmsApp::init` に `new int[16]` のリークを追加 | CTest 3/3 が LeakSanitizer で失敗し、ログ検査とデモ 5 つも失敗した |
| M5 | `--shot` のファイルを空にする / PNG でない内容を書く / 何も書かない、偽のシミュレータ | 3 つとも `render_demos.sh` が失敗した(「no PNG, or an empty one」「not a PNG」) |
| D1 | `main/main.cpp` に、初期化子の不足・未使用変数・未使用関数を追加 | riscv32-esp-elf-gcc 14.2.0 で、`FMSUI_WERROR=ON` では 3 つとも `-Werror=missing-field-initializers` / `-Werror=unused-variable` / `-Werror=unused-function` のエラーになった。OFF では警告 3 件でビルドが通った |
| D2 | `components/fmsui/src/theme.cpp` に未使用の static 変数を追加(実機) | `-Werror=unused-variable` で失敗した |
| D3 | `third_party/lvgl/src/core/lv_obj.c` に未使用の static 変数を追加(実機) | `-Wunused-variable` の警告 1 件で、ビルドは成功した |
| D4 | `main/idf_component.yml` の `m5stack_tab5` を `">=1.2.0,<1.3.0"` に変え、lock と食い違わせる | `device_build.sh` が「dependencies.lock was rewritten during the build」で失敗した。manifest_hash が変わったことで推移的な依存がすべて解決し直され、m5stack_tab5 は 1.2.0~1 のままでも、esp_lvgl_port 2.9.0・usb 1.5.0・gt911 1.2.1・sccb_intf 0.0.9 に上がった。その結果、既定構成のビルド自体も同じ `on_frame_buf_complete` のエラーで失敗した。範囲指定を少し変えるだけで依存が上がって壊れるので、lock を監視する理由の実例になった。(手元のツリーは lock が CRLF だったため diff が全行になったが、CI のチェックアウトは LF なので、CI では変わった行だけが出る) |
| D5 | 前章の 9/9 の実機ビルド `build/` の `compile_commands.json` を判定にかける | `check_lvgl_sources.py` が examples 257 件 / demos 1 件で失敗した。現行の `build-clean/` は 0 / 0 件で成功した |

**GitHub-hosted run**

最初の run は、`552299e` の push による
[run #1 (34549917868)](https://github.com/Lausiv1024/FmsLikeUI/actions/runs/34549917868) で、
2026-09-11 01:15〜01:24 UTC に実行された。3 job とも success で、run 全体は 558 秒。
失敗が無かったので、失敗時の artifact は作られていない(3 job とも「Upload failure evidence」は skipped)。

| job | 結果 | 所要時間 | 内訳 |
|---|---|---:|---|
| `host (debug)` | success | 111 s | 依存導入 45 s、ビルド 40 s (561/561)、CTest 3/3 成功 (0.21 s)、6 デモ成功 |
| `host (asan-ubsan)` | success | 99 s | 依存導入 38 s、ビルド 39 s (561/561)、`fmsui` 8/8 ソースが計装済み、CTest 3/3 成功 (0.40 s)、6 デモ成功 |
| `device-build (esp32p4)` | success | 554 s | コンテナの初期化(イメージの pull を含む)105 s、checkout 12 s、6 構成のビルド 434 s |

- ホスト job の環境: runner image `ubuntu-22.04` 20260907.292.1、CMake 3.31.6(ローカルの 3.22.1 より新しい)、
  gcc 11.4.0、`ninja-build` 1.10.1、`libsdl2-dev` 2.0.20。
- device job が pull したイメージの digest は、固定した `sha256:b9f2d6ea…` と一致した。
  コンパイル対象は 1594 件、Examples / Demos は 0 / 0 件。6 構成の bin サイズは、上の表のローカル再現と同じ値だった
  (既定 0xdd650 = 906,832 bytes / 空き 41% など)。
- 3 job のログのどれにも `warning:` の行は無く、device job のログには `::error::` も無い。
  lock が書き換わっていれば `device_build.sh` がエラーを出して job を失敗にするので、コミットした lock のままビルドされている。
- job summary の中身は REST API では取得できないため、ここに書いた値はジョブログから取った。
  サイズは `check_sizes.py` の出力、コンパイル対象の内訳は `check_lvgl_sources.py` の出力による。
- run の成功を確認した後で、README の先頭に badge を追加した。

GitHub 上でまだ通していない経路: `pull_request` と `workflow_dispatch` による起動、`concurrency` によるキャンセル、
timeout による停止、失敗時の artifact の保存。検出の仕組みそのものは、上のミューテーションでローカルに確認した。

**レビュー確認** (2026-09-11)

- `master` の badge 追加後の commit `c328115` でも
  [run #2 (34550709001)](https://github.com/Lausiv1024/FmsLikeUI/actions/runs/34550709001) が起動し、
  3 job とも success (全体 9 分 46 秒、device-build 9 分 41 秒)。device summary で6構成のサイズ、
  LVGL Examples / Demos 0 / 0件、`dependencies.lock` が不変であることを確認した。
- 既存の通常ビルドとsanitizerビルドに対し、レビュー側でもCTest 3/3と6デモを再実行して成功した。
  sanitizer側は`fmsui` 8/8ソースの計装と、CTest・デモログにsanitizer報告がないことも確認した。
- `tools/ci/*.sh` はshellcheckで指摘0件。差分の`git diff --check`も問題なし。
- ActionのSHAは各v7.0.1 releaseと一致する。workflowの条件式にある`!cancelled()`はstatus check functionなので、
  build成功後の検査が一つ失敗しても後続のCTestとデモを実行する意図と一致する。
- 実際のpull request・手動起動・キャンセル・timeout・失敗artifact uploadは未実行のまま。これは上記の
  成功run、workflow構造、ローカルのミューテーション確認と区別して残し、運用で初めて発生したときに追記する。

**検証コマンド**

```bash
# ホスト job(workflow の run: と同じ)
export ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
cmake -S sim -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFMSUI_SANITIZE=ON -DFMSUI_WERROR=ON   # debug は OFF
cmake --build build
python3 tools/ci/check_sanitized.py build                                                   # asan-ubsan だけ
ctest --test-dir build --output-on-failure --timeout 120
bash tools/ci/check_no_sanitizer_reports.sh build/Testing/Temporary/LastTest.log
bash tools/ci/render_demos.sh build/fmsui_sim ci-out/demos

# device job(ESP-IDF 5.5.4 のコンテナ内)
. "$IDF_PATH/export.sh"
bash tools/ci/device_build.sh build-ci ci-out/device
```

**実装前に確認して決めたこと**

- push は、ローカル検証が通った後にこちらで行う。先行していた 12 コミットも一緒に origin/master へ送る。
- 警告のエラー化は CMake オプション `FMSUI_WERROR` で行い、既定は OFF にして CI だけ ON にする。
- ホスト job は `ubuntu-22.04`。timeout はホスト 30 分・device 60 分、CTest の既定は 120 秒。
- device-build のローカル検証は、Docker で CI と同じイメージを使って行う。
- 依存の固定は `dependencies.lock` のコミットで行う(上記の失敗を見つけてから確認した)。

**確認せずに決めたこと**

- ホストの 2 job は、別々の job を書かずに matrix にした。`fail-fast: false` なので独立性は変わらない。
- Action は、確認時点の最新 v7.0.1 を選んだ。ESP-IDF イメージは、tag を残したうえで digest で固定した。
- artifact の保持期間は 7 日にした。
- sanitizer は、halt させる環境変数と、ログ検査の二重で失敗させる。
- 検査は workflow にインラインで書かず、`tools/ci/` のスクリプトにした。ローカルで同じものを走らせるためである。
  実行ビットには頼らず、`bash` / `python3` で呼ぶ。
- device-build のビルド順は、既定(クリーン)→ `m0` → `m1` → `catalog` → `fplan` → `reorder` の 6 回にした。最後に既定へ戻すビルドはしない。
- 生成物の `fmsui_fonts` もプロジェクト所有として `-Werror` の対象に入れた。
- CTest とデモの step は、ビルドが成功していれば、途中の step が失敗しても走らせる。
- lock のコミットに合わせて、ビルド中に lock が書き換わったら失敗にする判定を `device_build.sh` に入れた(計画には無い)。
- README には、CI の節、`tools/ci/` の行、実機節に lock での依存固定と更新手順の段落を追加した。DESIGN.md は変えていない。
- job summary の文言は英語にした。

### 2026-09-11: 外部利用契約と consumer smoke test

**状態: 実装済み (2026-09-11)。完了条件 8 件をレビューで確認済み。**

現在のCIは、このリポジトリのルートからシミュレータ、テスト、デモ、ESP32-P4向けアプリを
ビルドする経路を検証している。一方、主成果物はFMSアプリケーションではなくUIフレームワークなので、
次は別プロジェクトから`components/fmsui`を利用する契約を固定する。デモは引き続きテスト用の
fixtureであり、この計画でデモアプリ固有の機能は増やさない。

#### 1. 初版で正式に扱う導入方法

初版はソースを組み込む次の2経路だけを正式な対象とする。

- 通常CMakeでは、利用側がLVGLのtargetを用意し、`components/fmsui`を`add_subdirectory()`する。
- ESP-IDFでは、`fmsui`と、必要な場合だけ`fmsui_fonts`をlocal componentまたはGit submoduleとして
  `EXTRA_COMPONENT_DIRS`へ追加する。LVGL、display、入力、BSPの初期化は利用側が所有する。

ESP Component Registryへの公開、インストール済みCMake package、バイナリ配布、SemVerの
互換保証はこの段階では行わない。まずソース組み込みの契約を実際のconsumerで固定してから判断する。

`fmsui`本体の依存は引き続きLVGLだけとし、生成済みB612 Monoを含む`fmsui_fonts`は任意とする。
利用側が独自の`lv_font_t`を`FmsThemeData`へ渡せる現在の分離を維持する。

#### 2. 公開APIの範囲

初回はヘッダーの移動や大規模な名前変更をせず、利用契約を文書とconsumerのコンパイルで固定する。

- 通常のアプリが使うAPI: `app.h`、`foundation.h`、`widget.h`、`widgets.h`、`theme.h`、`fms.h`、`str.h`
- 診断または高度な拡張で使うAPI: `refresh.h`、`render.h`
- フレームワーク内部として扱い、互換性を約束しないAPI: `element.h`、`arena.h`

入口は`<fmsui/fmsui.h>`とする。内部ヘッダーが現時点でumbrella headerや他の公開ヘッダーから
推移的に見えることと、それを利用契約に含めることは分ける。実際に非公開ディレクトリへ移すかは、
consumerを作って必要な依存を確認した後の別判断とする。

#### 3. 独立consumerによる検証

デモとは別に、フレームワークの利用者としてだけ振る舞う最小プロジェクトを置く。

**通常CMake consumer**

- リポジトリのルートCMake、`sim/`、`demo/`、既存`tests/`に依存せずconfigureできる。
- `<fmsui/fmsui.h>`から独自の`StatefulWidget`とWidget treeを定義し、`fmsui::fmsui`へリンクする。
- headlessなLVGL displayを利用側で用意し、初期化、1回以上のbuild/paint、`requestFrame()`、
  `shutdown()`までを公開APIだけで実行する。
- `fmsui_fonts`を使わず、利用側が選んだフォントまたは`LV_FONT_DEFAULT`で成立する経路を含める。

**ESP-IDF consumer**

- ルートの`main/`、`demo/`、M5Stack Tab5 BSPに依存しない最小ESP-IDFプロジェクトとする。
- `fmsui`をlocal componentとして認識し、同じ最小Widget treeをESP32-P4向けにコンパイル・リンクする。
- これはcomponentの取り込みとリンク契約の試験であり、display初期化、flash、物理タッチは行わない。

加えて、通常利用向けに分類した各ヘッダーを、それぞれ翻訳単位の最初に単独includeしてコンパイルする。
別ヘッダーが偶然先にincludeされた場合だけ通る状態をCIで検出する。

#### 4. 利用文書とCI

`docs/USING.md`を追加し、少なくとも次を記録する。

- 通常CMakeとESP-IDF local componentの導入手順
- 利用側が用意するLVGL、display/input port、フォントの境界
- `runApp()`、LVGL timer、`requestFrame()`、`shutdown()`のライフサイクル
- UI thread上の`setState()`と、別taskからのデータ公開・`requestFrame()`の境界
- 初版で確認したLVGL / ESP-IDFの版と、保証しない配布・実機範囲
- デモのコピーではない最小Widget treeの例

ホストconsumerは既存ホストCIから独立してconfigure/build/runしたことがログで分かるようにする。
ESP-IDF consumerはdigest固定済みのdevice-build環境で別build directoryからビルドする。
consumerの失敗を既存デモや既存アプリの成功で隠さない。

#### 対象外

- ESP Component Registry、GitHub Release、インストール済みCMake packageへの公開
- SemVerによる長期のソース・ABI互換保証
- prebuilt library、署名、配布archiveの作成
- M5Stack Tab5以外のboard port実装と、consumerへのBSP初期化の提供
- デモアプリへの画面、入力、ドメインロジックの追加
- 実機flash、物理タッチ、スクリーンショットの見た目比較

#### 実装完了の条件

- [x] 通常CMakeの独立consumerが、ルートCMakeやデモに依存せずconfigure、build、実行できる。
- [x] ESP-IDFの独立consumerが、既存`main/`やBSPに依存せずESP32-P4向けにリンクできる。
- [x] consumerが`<fmsui/fmsui.h>`だけから独自のStatefulWidgetとWidget treeを定義できる。
- [x] 通常利用向けの各公開ヘッダーが、暗黙のinclude順序に依存せず単独でコンパイルできる。
- [x] `fmsui`本体が`fmsui_fonts`へ必須依存せず、consumer側のフォント選択で動く。
- [x] 導入、所有範囲、ライフサイクル、thread境界、確認済みバージョンが`docs/USING.md`に記録される。
- [x] hostとESP-IDFのconsumer検証がCIに入り、既存の内部テストとは別の失敗として判別できる。
- [x] 既存3件のCTest、6デモのheadless描画、6構成のdevice-buildに回帰がない。

#### レビュー結果

2026-09-11 のレビューで、実装に未解決の指摘は無かった。

- 必要なファイルだけを置いた新しい一時ツリーからhost consumerをconfigure・build・実行し、CTest 1/1と
  10 checksが成功した。通常利用向け7ヘッダーと入口の`fmsui.h`も、それぞれ単独の翻訳単位として
  `-Werror`付きでコンパイルされた。
- 既存の通常ビルドとASan / UBSanビルドを再構成・再ビルドし、両方でCTest 3/3と6デモを再実行して成功した。
  sanitizer側は`fmsui` 8/8ソースの計装と、CTestログにsanitizer報告が無いことも再確認した。
- `tools/ci/*.sh`はshellcheckで指摘0件。`check_consumer_components.py`は構文検査に成功し、
  `14058ee..2c7e5b3`の`git diff --check`にも問題は無かった。
- ESP-IDF consumer、既存6構成のdevice-build、GitHub上の5 jobについては、このレビューでは再実行せず、
  下記の固定ESP-IDF環境によるローカル結果とGitHub-hosted run #5の成功記録、および実装内容を照合して判定した。
- 実装記録にhost consumerを「11 checks」とする数え間違いが3か所あった。実行ファイルの出力と
  `check()`呼び出しは10件で一致し、計画が要求したbuild / paint、`requestFrame()`、`setState()`、
  利用側フォント、`shutdown()`はすべて含むため、数値だけを10へ訂正した。

#### 計画時に確認して決めたこと

- 初版の正式経路はlocal componentまたはソース組み込みとし、Registry公開は後段にする。
- 公開範囲はまず文書とconsumerテストで固定し、ヘッダーの物理的な再配置は同時に行わない。
- `fmsui_fonts`は任意依存のままにし、フレームワーク本体へ統合しない。
- consumerは製品デモではなく利用契約のテストfixtureとし、デモアプリの機能追加は行わない。

#### 実装の結果

| 判断 | 実装 |
|---|---|
| 置き場所 | `consumers/host/`(通常 CMake)、`consumers/esp-idf/`(ESP-IDF)、両者が使う `consumers/shared/consumer_page.{h,cpp}` |
| 最小 Widget tree | `consumers/shared/` の `ConsumerPage`。独自の `StatefulWidget`(タップ回数を `setState()` で更新)、他タスクが公開する `std::atomic<uint32_t>`、`FmsTheme`、`Column` / `Text` と `FmsLabel` / `FmsButton`。フレームワークは `<fmsui/fmsui.h>` だけから使う |
| LVGL の用意 | 利用側がリポジトリの `third_party/lvgl` (v9.5.0) と `third_party/lv_conf.h` を取り込む。host は `add_subdirectory()` と `LV_BUILD_CONF_PATH`、ESP-IDF は `EXTRA_COMPONENT_DIRS` に `components/fmsui` と `third_party/lvgl` を個別に並べ、`LV_KCONFIG_IGNORE` を付ける |
| 依存しないことの保証 | `tools/ci/stage_consumer.sh` が `components/fmsui`、`third_party/lv_conf.h`、`third_party/lvgl`、`consumers/` だけを別ディレクトリへ写し、consumer はそこから configure する。ルートの `CMakeLists.txt`、`sdkconfig.defaults`、`dependencies.lock`、`main/`、`demo/`、`sim/`、`tests/`、`tools/`、`components/fmsui_fonts` はツリーに存在しない |
| host consumer の実行内容 | `consumers/host/main.cpp`。480x320 の headless display、pointer indev、tick、フォントを利用側で持ち、(1) `runApp()` の初回ビルドと描画、(2) 別スレッドの公開 → `requestFrame()` が次フレームで表示に届き、その後ビルドしない、(3) `FmsButton` のタップが `setState()` を通る、(4) 全ラベルがテーマに渡したフォント、(5) `shutdown()` 後に screen の子が 0、の 10 checks。CTest に `TIMEOUT 60` で登録 |
| フォント | host は `LV_FONT_DEFAULT` を別アドレスへコピーした `lv_font_t` をテーマに渡し、ラベルのフォントがそのアドレスであることを判定する(フォールバックの `LV_FONT_DEFAULT` と区別するため)。ESP-IDF は `LV_FONT_DEFAULT` をそのまま渡す。どちらも `fmsui_fonts` をビルドに含めない |
| ヘッダーの単独コンパイル | `consumers/host/CMakeLists.txt` が `header_check.cpp.in` から `app` / `foundation` / `widget` / `widgets` / `theme` / `fms` / `str` と入口の `fmsui` の 8 TU を生成し、OBJECT ライブラリとして `-Wall -Wextra`(CI では `-Werror` も)でコンパイルする |
| ESP-IDF consumer | `consumers/esp-idf/`。ボードを持たない。`app_main` が LVGL の display をバッファだけで作り、同じページを `runApp()` し、別タスクが公開と `requestFrame()` を行う。`sdkconfig.defaults` はターゲット、LVGL Examples / Demos の無効化、main タスクのスタック 8192 だけ。ビルドのみで、実機では動かしていない |
| component の検査 | `tools/ci/check_consumer_components.py`。`project_description.json` で `fmsui` と `lvgl` がビルドに入り、`fmsui_fonts`、`m5stack_tab5`、`esp_lvgl_port` が入っていないことを判定し、job summary 用の表を出す |
| 警告 | host は consumer 自身の target(2 ソース + 8 ヘッダー TU)に `FMSUI_WERROR=ON` で `-Werror`。`fmsui` のソースは既存 host job が同じコンパイラで `-Werror` 済みなので、ここでは付けない。ESP-IDF は `FMSUI_WERROR=ON` で `fmsui` と consumer の `main` に `FMSUI_WERROR_FLAGS` を付ける |
| CI | `consumer (host)`(pull request / `master` push / 手動、timeout 30 分)と `consumer (esp-idf, esp32p4)`(`master` push / 手動、timeout 60 分、device-build と同じ digest のイメージ)を独立 job として追加。host 側は `ninja-build` だけを入れ、SDL2 は入れない。失敗時だけ 7 日の artifact を残す。ESP-IDF 側は bin サイズと component 検査を job summary に出す |
| 文書 | `docs/USING.md` を追加。README に USING.md への導線、CI の 2 job、`consumers/` の行を追加。DESIGN.md は変えていない |
| フレームワーク本体 | ソースとヘッダーは変更なし。通常利用向けの 7 ヘッダーは実装前の時点ですでに単独でコンパイルできた(gcc 11.4 で 12 ヘッダーすべてを確認) |
| LVGL の SDL ドライバ | GitHub の最初の run で見つかった依存(下記)を外した。`tools/gen_lv_conf.py` の `LV_USE_SDL` を「`#ifndef LV_USE_SDL` なら 0」に変えて `third_party/lv_conf.h` を再生成し、`sim/CMakeLists.txt` が `lvgl` target に `LV_USE_SDL=1` を PUBLIC で渡す。実機は以前と同じく 0 |

**実装して分かった利用契約上の事実**

- 変更前の同梱 `lv_conf.h` は `ESP_PLATFORM` 以外で `LV_USE_SDL 1` だったため、ホストで LVGL をビルドするだけで
  `SDL2/SDL.h` が必要だった(リンクは不要)。WSL には SDL2 が入っていたのでローカル検証では見えず、SDL2 を入れない
  `consumer (host)` job が GitHub で初めて検出した。確認のうえ、SDL をシミュレータ側の opt-in に変えた。
- `fmsui` の ESP-IDF 用 `CMakeLists.txt` は、`FMSUI_WERROR` と `FMSUI_WERROR_FLAGS` を**取り込んだプロジェクト側の変数**として読む。どちらも設定しないプロジェクトでは ESP-IDF 既定の警告方針でビルドされるだけなので、外部利用の妨げにはならない。consumer は同じ 6 フラグの一覧を自分の `CMakeLists.txt` に持ち、その理由をコメントに書いた。
- ESP-IDF で `EXTRA_COMPONENT_DIRS` に `components/` 全体を指定すると、どこからも `REQUIRES` されていない `fmsui_fonts` までビルドに入る(ミューテーション D4 で確認)。USING.md に「`components/fmsui` を個別に書く」と記載した。
- component manager は、managed component が無くても consumer のプロジェクト直下に `dependencies.lock`(中身は ESP-IDF の版だけ)を作る。ESP-IDF の版で変わるだけのファイルなので、`/consumers/esp-idf/dependencies.lock` を `.gitignore` に入れた。ステージングでも写さない。

**ローカル検証**(2026-09-11。WSL2 Ubuntu 22.04 / gcc 11.4 / CMake 3.22.1 / Ninja 1.10.1 / 8 コア、Docker Desktop 29.7.2)

前章と同じく、`git ls-files -co --exclude-standard` のファイルと LVGL サブモジュールの追跡ファイルだけを tar にしたスナップショットから検証した。ホストは WSL の ext4 上、ESP-IDF はコンテナの中へ展開した。

| 対象 | 結果 |
|---|---|
| actionlint 1.7.12 + shellcheck 0.11.0 | `ci.yml` はエラー 0 件。`tools/ci/*.sh`(新規 3 本を含む)への shellcheck も指摘 0 件 |
| `consumer (host)` 相当 | `consumer_host.sh` が rc 0。ステージ・configure・ビルド (552 ステップ)・CTest で 23 秒、`warning:` 0 件。直接実行で 10 checks すべて ok。compile commands では consumer の 10 TU すべてに `-Werror` が付き、`fmsui` 8 ソースと LVGL 531 ソースには付いていない |
| `consumer (esp-idf, esp32p4)` 相当 | `espressif/idf:v5.5.4@sha256:b9f2d6ea…` で `consumer_idf.sh` が rc 0。単独実行で 101 秒、1598 ステップ、`warning:` 0 件。`fmsui_consumer.bin` 600,720 bytes、最小 app 領域 1,048,576 bytes に対して空き 447,856 bytes (43%)。ビルドに入った component は 108 個で、`fmsui` と `lvgl` はステージしたツリーから、`fmsui_fonts` / `m5stack_tab5` / `esp_lvgl_port` / managed component は 0 |
| USING.md のコード例 | 「最小の Widget tree」の例を、起動部分だけ関数で包んで `-std=c++20 -Wall -Wextra -Werror -fsyntax-only` でコンパイルし、成功した |
| 既存 `host (debug)` 相当 | 561 ステップ、`warning:` 0 件。CTest 3/3 成功、6 デモ成功 |
| 既存 `host (asan-ubsan)` 相当 | 561 ステップ、`warning:` 0 件。`fmsui` 8/8 ソースが計装済み、CTest 3/3 成功、sanitizer の報告 0 件、6 デモ成功 |
| 既存 `device-build (esp32p4)` 相当 | `device_build.sh` が rc 0。6 構成とも成功し、各ログの `warning:` 0 件。bin サイズは 6 構成とも前章の表と同じ(既定 906,832 bytes / 空き 41% など)。コンパイル対象 1594 件、Examples / Demos 0 / 0 件、`dependencies.lock` は不変。所要時間 394 秒は ESP-IDF consumer のミューテーションと並行して走らせた値 |

**壊れた利用契約を検出できることの確認 (ミューテーション)。** スナップショットのコピーを 1 か所ずつ壊し、CI と同じスクリプトで確かめた。

| # | 壊し方 | 結果 |
|---|---|---|
| H1 | `theme.h` から `#include "fmsui/widget.h"` を消す | consumer 本体は `fmsui.h` 経由でコンパイルできるが、`header_check/theme.cpp` が `'Widget' does not name a type` などで失敗した |
| H2 | 通常 CMake の `fmsui` が `fmsui_fonts` にもリンクする | ステージしたツリーに `fmsui_fonts` が無く、`cannot find -lfmsui_fonts` でリンクに失敗した |
| H3 | consumer が `${FMSUI_DIR}/demo` を include path に足し、`catalog.h` を include する | リポジトリの中でそのままビルドすると**成功**し、`consumer_host.sh` では `catalog.h: No such file or directory` で失敗した。ステージングが無いと見逃す依存の実例 |
| H4 | `render.cpp` がテーマのフォントを無視して常に `LV_FONT_DEFAULT` を使う | check 4「each in the font the theme was given」が失敗した |
| H5 | `requestFrame()` を空にする | check 2 の 3 件が失敗した |
| H6 | `shutdown()` が要素ツリーを破棄しない | check 5 が失敗した |
| H7 | `StateBase::markNeedsBuild()` を空にする | check 3「tapping it rebuilt the page」が失敗した |
| D1 | ESP-IDF consumer の `main.cpp` に未使用の static 変数 | `-Werror=unused-variable` で失敗した |
| D2 | `components/fmsui/src/theme.cpp` に未使用の static 変数(ESP-IDF consumer) | `-Werror=unused-variable` で失敗した。ルート以外のプロジェクトでも `FMSUI_WERROR_FLAGS` の仕組みが効く |
| D3 | `fmsui` の component が `REQUIRES lvgl fmsui_fonts` になる | `Failed to resolve component 'fmsui_fonts' required by component 'fmsui'` で configure に失敗した |
| D4 | consumer が `components/` 全体を指定し、そこに `fmsui_fonts` もある | ビルドは成功し、`fmsui_fonts` のソースがコンパイル対象に入った。`check_consumer_components.py` が「built as fmsui_fonts」で失敗にした |

D1〜D4 はすべて元に戻した後、再ビルドと component 検査が再び成功することを確認した。

**GitHub-hosted run #4: `consumer (host)` だけが失敗**

`556e341` の push による
[run #4 (34556079180)](https://github.com/Lausiv1024/FmsLikeUI/actions/runs/34556079180)
(2026-09-11 02:49〜02:58 UTC)。

| job | 結果 | 所要時間 | 内訳 |
|---|---|---:|---|
| `host (debug)` | success | 97 s | CTest 3/3 成功、6 デモ成功 |
| `host (asan-ubsan)` | success | 90 s | `fmsui` 8/8 ソースが計装済み、CTest 3/3 成功、6 デモ成功 |
| `consumer (host)` | **failure** | 60 s | 依存導入 31 s。ステージ後のビルド 11 秒目に、LVGL の `src/drivers/sdl/lv_sdl_*.c` 3 件が `third_party/lv_conf.h:1303: fatal error: SDL2/SDL.h: No such file or directory` で失敗。失敗時の artifact は保存された |
| `device-build (esp32p4)` | success | 569 s | 6 構成の bin サイズは前章の表と同じ(既定 0xdd650 = 906,832 bytes / 空き 41% など) |
| `consumer (esp-idf, esp32p4)` | success | 367 s | コンテナの初期化 99 s、ビルド 246 s。`warning:` 0 件、イメージ digest は `sha256:b9f2d6ea1c19…`。`fmsui_consumer.bin` 0x93220 = 602,656 bytes、空き 43% |

- runner の CMake は 3.31.6、コンパイラは gcc 11.4.0。
- ESP-IDF consumer の bin はローカル (600,720 bytes) より 1,936 bytes 大きい。device-build はローカルと同じサイズになる。
  consumer は `fmsui` と LVGL がプロジェクトディレクトリの外にあり、ソースパスの埋め込みがビルド場所に左右されると考えられる
  (GitHub のパス `/__w/FmsLikeUI/FmsLikeUI/ci-out/…` はローカルの `/w/ci-out/…` より 22 文字長く、22 × 88 = 1,936)。
  中身の比較はしていないので、推定にとどめる。
- ジョブのログは REST API の job logs(リダイレクト先を認証ヘッダーなしで取得)で読んだ。

**SDL を opt-in にした後のローカル検証**(同じ環境で、新しいスナップショットから)

| 対象 | 結果 |
|---|---|
| `third_party/lv_conf.h` | `tools/gen_lv_conf.py` で再生成し、差分は `LV_USE_SDL` の 5 行だけ |
| `consumer (host)` 相当 | rc 0、20 秒、`warning:` 0 件、10 checks すべて ok。`ninja -t deps` で SDL2 のヘッダーに依存するオブジェクトの行は 0、compile commands に `-DLV_USE_SDL` は 0 件 |
| `host (debug)` 相当 | `warning:` 0 件。`ninja -t deps` の SDL2 ヘッダー行は 300 で、LVGL 531、`components/` 13、`demo/` 6、`sim/` 1、`tests/` 3 の全 TU に `-DLV_USE_SDL=1` が付く。CTest 3/3 成功。6 デモの PNG は変更前のスナップショットで撮ったものと 6 枚ともバイト一致 |
| `host (asan-ubsan)` 相当 | `warning:` 0 件、`fmsui` 8/8 ソースが計装済み、CTest 3/3 成功、sanitizer の報告 0 件。6 デモの PNG は変更前と 6 枚ともバイト一致 |
| `device-build (esp32p4)` 相当 | `device_build.sh` rc 0(229 秒)。6 構成とも `warning:` 0 件、bin サイズは 6 構成とも変更前と同じ、コンパイル対象 1594 件、Examples / Demos 0 / 0 件、lock 不変。compile commands に `LV_USE_SDL` は 0 件 |
| `consumer (esp-idf, esp32p4)` 相当 | `consumer_idf.sh` rc 0(81 秒)、`warning:` 0 件。`fmsui_consumer.bin` 600,720 bytes は変更前と同じ。component 検査も同じ結果 |

**GitHub-hosted run #5: 5 job すべて成功**

`603f82e`(SDL を opt-in にした commit)の push による
[run #5 (34557528871)](https://github.com/Lausiv1024/FmsLikeUI/actions/runs/34557528871)
(2026-09-11 03:11〜03:21 UTC)。run 全体は 570 秒。

| job | 結果 | 所要時間 | 内訳 |
|---|---|---:|---|
| `host (debug)` | success | 100 s | CTest 3/3 成功、6 デモ成功、`warning:` 0 件 |
| `host (asan-ubsan)` | success | 118 s | `fmsui` 8/8 ソースが計装済み、CTest 3/3 成功、6 デモ成功、`warning:` 0 件 |
| `consumer (host)` | success | 72 s | 依存導入 15 s(Ninja だけ)、ステージ・configure・ビルド・CTest 40 s。CTest 1/1 成功、`warning:` 0 件、ログに SDL2 の文字列は 0 件。CMake 3.31.6 / gcc 11.4.0 |
| `device-build (esp32p4)` | success | 567 s | 6 構成の bin サイズは run #4 とローカルと同じ。Examples / Demos 0 / 0 件、`warning:` 0 件 |
| `consumer (esp-idf, esp32p4)` | success | 362 s | コンテナの初期化 102 s、ビルド 245 s。`fmsui_consumer.bin` 0x93220 = 602,656 bytes / 空き 43%(run #4 と同じ)、`warning:` 0 件 |

- component 検査の表と bin サイズの表は job summary と `summary.md` へ出力し、ジョブログには出さない。
  `consumer_idf.sh` は検査に失敗すると非 0 で終わるので、job の成功は検査の通過を意味する。
- 前章で「GitHub 上でまだ通していない」とした経路のうち、**失敗時の artifact の保存**は run #4 の `consumer (host)` で
  実際に動いた(「Upload failure evidence」が success)。`pull_request` と `workflow_dispatch` による起動、`concurrency` による
  キャンセル、timeout による停止は、consumer の 2 job でもまだ通していない。

**検証コマンド**

```bash
# ホスト(workflow の run: と同じ)
bash tools/ci/consumer_host.sh ci-out/consumer-host

# ESP-IDF(ESP-IDF 5.5.4 のコンテナ内)
. "$IDF_PATH/export.sh"
bash tools/ci/consumer_idf.sh ci-out/consumer-idf
```

**実装前に確認して決めたこと**

- consumer は `consumers/` 直下に置く(`consumers/host/`、`consumers/esp-idf/`)。
- LVGL と `lv_conf.h` は、リポジトリの固定版 (`third_party/lvgl`、`third_party/lv_conf.h`) を consumer が取り込む。
- ESP-IDF consumer の job は device-build と同じく `master` への push と手動実行だけで起動する。
- ローカル検証の後にこちらで commit・push し、GitHub-hosted run の結果を記録してからレビュー待ちにする。
- 同梱 `lv_conf.h` が SDL2 のヘッダーを要求する問題(run #4 で発見)は、consumer に SDL2 を入れたり consumer 専用の
  `lv_conf.h` を置いたりせず、SDL をシミュレータ側の opt-in にして直す。

**確認せずに決めたこと**

- 「依存しない」をソースの記述ではなく、必要なファイルだけを写したツリーからのビルドで保証した(`stage_consumer.sh`)。
- host の Widget tree は計画の必須項目(build / paint、`requestFrame()`、`shutdown()`)に加えて、pointer indev からのタップで `setState()` も通すことにした。
- host consumer は通常設定だけで走らせ、ASan / UBSan 版は作っていない。フレームワーク本体の sanitizer 検証は既存の host job が担う。
- 単独コンパイルの対象は通常利用向けの 7 ヘッダーと入口の `fmsui.h` にし、`refresh.h` / `render.h` / `element.h` / `arena.h` は含めなかった。
- consumer 自身の target の警告だけを `-Werror` にし、host 側の `fmsui` ソースには付けなかった(既存 host job と重複するため)。
- ESP-IDF consumer の main タスクのスタックを 8192 にした。実機では動かしていないので、この値は検証していない。
- host consumer の job では SDL2 を入れない。
- 既存 job と同じく、検査は `tools/ci/` のスクリプトにし、workflow にはインラインで書かなかった。job summary の文言は英語にした。
- `/ci-out/`、`/consumers/*/build*/`、`/consumers/esp-idf/dependencies.lock` を `.gitignore` に加えた。

## レビュー待ち

### 2026-09-11: 公開 API 境界の物理的な分離

**状態: レビュー待ち (2026-09-11)。実装と検証の結果を章末に記録した。完了条件のチェックはレビューで行う。**

外部 consumer によってソース組み込みの利用契約は確認できたが、公開 API と内部実装の境界は
まだ文書上の分類にとどまっている。現在の `<fmsui/fmsui.h>` は `arena.h` と `element.h` を直接
includeし、`widget.h` は `ArenaObject`、`app.h` は `BuildArenas` / `BuildOwner` / Element treeを
公開クラスの定義へ含めている。このため、内部扱いと記載した型をconsumerが直接使えてしまい、
内部構造の変更が公開ヘッダーの変更になる。

この計画では、既に決めたAPI分類を実際のディレクトリ、include path、consumerテストで保証する。
FMSアプリケーションやデモの機能を増やす計画ではない。

#### 1. 公開する範囲

初版の公開範囲を次のように固定する。

- 通常のアプリ向け: `app.h`、`foundation.h`、`widget.h`、`widgets.h`、`theme.h`、`fms.h`、`str.h`
- 診断・高度な拡張向け: `refresh.h`、`render.h`
- 入口: `fmsui.h`。上の通常向けと高度向けだけをまとめ、内部ヘッダーは含めない
- フレームワーク内部: 現在の `arena.h` と `element.h`

`render.h` は単なる内部実装ではない。`CustomPaint` の `Painter` / `Canvas` と、独自の
RenderObjectWidgetを作る高度な拡張点を持つため、変わりやすいことを明記した公開APIとして残す。
`refresh.h`も診断用の公開APIとして残す。

`arena.h`と`element.h`は、まだSemVer互換を約束しておらず、既に内部扱いと記録している。
旧パス `<fmsui/arena.h>` / `<fmsui/element.h>` の互換ラッパーやdeprecated期間は設けず、
公開include領域から削除する。直接利用していたコードは公開契約の対象外とする。

#### 2. 内部型を公開ヘッダーから外す

内部ヘッダーは、例えば `components/fmsui/src/internal/` のようなライブラリ自身だけが使う場所へ移し、
CMake / ESP-IDFのビルドではPRIVATEなinclude pathとして与える。最終的なディレクトリ名は実装時に
既存ソースのinclude関係を見て決めるが、consumerへPUBLICに渡さないことを条件とする。

- `Widget`は公開の`ArenaObject`を継承しない形へ変える。一方、`new Widget`が現在のbuild arenaから
  確保されること、構築済みWidgetのvirtual destructorがreset時に逆順で呼ばれること、2つのarenaを
  交互に使う寿命は変えない。
- `BuildContext`はconsumerが`build()`で受け取り、`FmsTheme::of(ctx)`などへ渡せる不透明型にする。
  Elementのフィールドやreconcile処理は公開しない。
- `Element`を返す`createElement()`など、Widget基底クラスが内部実装と接続する箇所は、前方宣言と
  ライブラリ内部の定義だけで成立させる。通常のStatelessWidget / StatefulWidget利用者に
  Element定義を要求しない。
- `ThreadId` / `ThreadIdFn`は`setThreadId()`の公開契約なので、内部の`BuildOwner`ではなく
  `app.h`側で宣言する。
- `FmsApp`の`BuildArenas`、`BuildOwner`、Element tree、統計mutexなどを`.cpp`側へ移す。
  `requestFrame()`、`requester()`、`screenSize()`、`setClock()`、`setThreadId()`は内部型へ触れない
  公開宣言と外部定義にする。
- `FmsApp`はsingletonである現在の契約を維持し、PIMPL用の動的確保を追加しない。内部状態は
  ライブラリ側の静的なセッション状態として持ち、初期化、`shutdown()`、再初期化の順序を変えない。

これはカプセル化の変更であり、Widget / Element / RenderObjectの動作、レイアウト、描画差分、
統計値を変えるための変更ではない。公開型のサイズやABIも、この段階では保証対象にしない。

#### 3. consumerで境界を検査する

既存の独立consumerを、利用契約だけでなく公開範囲の検査にも使う。

- 通常向け7ヘッダー、高度向け2ヘッダー、入口の`fmsui.h`を、それぞれ翻訳単位の最初に単独includeし、
  合計10 TUを`-Wall -Wextra -Werror`でコンパイルする。
- consumerが`<fmsui/fmsui.h>`だけから、現在と同じ独自StatefulWidget / Widget treeを構築できることを
  維持する。consumerページやデモへ新しい機能は足さない。
- ステージしたツリーのPUBLIC include pathに`fmsui/arena.h`と`fmsui/element.h`が存在せず、
  consumerからその旧パスをincludeできないことを機械判定する。
- `components/fmsui`自身と内部単体テストだけには内部include pathを与える。consumer targetへ
  誤って伝播した場合に検査が失敗するようにする。
- 現在のhost consumerの10 checksをそのまま通し、build / paint、別スレッドからの`requestFrame()`、
  タップからの`setState()`、利用側フォント、`shutdown()`の公開動作が変わっていないことを確認する。

Arenaの内部化で既存テストに不足が見つかった場合は、次をframeworkの単体テストへ追加する。

- Widgetのvirtual destructorがarena reset時に1回だけ呼ばれる。
- 前回buildのWidgetが次のbuild中も生存し、その次のarena再利用で破棄される。
- `shutdown()`が両arenaとElement treeを解放し、同じプロセスで再`init()`できる。

既に同じ事実を直接判定するテストがあれば重複追加せず、そのテストを完了条件の証拠にする。

#### 4. 文書と検証

`docs/USING.md`のAPI表、umbrella headerの説明、非保証範囲を新しい物理境界へ合わせる。
`docs/DESIGN.md`には、BuildContextを不透明にする理由、Widgetのarena寿命を公開継承なしで維持する方法、
FmsApp内部状態の所有場所を記録する。READMEの利用導線は変えず、必要なら構成表だけを更新する。

検証は次を別の証拠として残す。

1. 公開／内部ヘッダーの配置とinclude pathの機械検査
2. host consumerのconfigure、build、公開ヘッダー10 TU、CTest 1/1、10 checks
3. 通常設定とASan / UBSan設定のビルド、CTest 3/3、6デモのheadless描画
4. sanitizer設定で`fmsui`本体が計装され、報告が無いこと
5. ESP-IDF consumerのビルドとcomponent検査
6. 既存device-buildの6構成
7. GitHub Actionsの5 job

#### 対象外

- デモアプリへの画面、入力、ドメインロジックの追加
- Widget / Element / RenderObjectのアルゴリズム変更と性能最適化
- clipping、scroll、animation、GlobalKey、UI taskへの関数queueの追加
- CMake install package、ESP Component Registry、GitHub Release、配布archive
- SemVer、ソース互換、ABI互換の保証開始
- 実機flash、物理タッチ、スクリーンショットの見た目比較

#### 実装完了の条件

- [ ] `arena.h`と`element.h`がPUBLIC include領域から外れ、framework内部だけのinclude pathに置かれる。
- [ ] `<fmsui/fmsui.h>`が通常向けと高度向けの公開APIだけを提供し、内部ヘッダーを直接includeしない。
- [ ] Widgetのarena割り当て、virtual destructor、2世代の寿命が公開の`ArenaObject`なしで維持される。
- [ ] BuildContextとFmsAppの公開定義がElement、BuildOwner、BuildArenasなどの内部定義を要求しない。
- [ ] 通常向け7、高度向け2、umbrellaの公開ヘッダー10 TUが単独で警告なくコンパイルできる。
- [ ] consumerで旧`<fmsui/arena.h>`と`<fmsui/element.h>`が利用できないことを機械判定できる。
- [ ] host consumerのCTest 1/1と10 checksが成功する。
- [ ] 通常／sanitizerのCTest 3/3と6デモが成功し、`fmsui`本体の計装と報告0件を確認できる。
- [ ] ESP-IDF consumerと既存device-build 6構成が成功する。
- [ ] `docs/USING.md`、`docs/DESIGN.md`、必要なREADME記述が新しい境界と一致する。
- [ ] GitHub Actionsの5 jobが成功し、実装結果とレビュー結果がこの章へ記録される。

#### 計画時に確認して決めたこと

- 旧`arena.h` / `element.h`の互換ラッパーは残さない。
- `render.h` / `refresh.h`は高度・診断向けの公開APIとして維持する。
- FmsApp内部化のための追加の動的確保は行わない。
- consumerは既存の最小Widget treeを使い続け、デモ機能を追加しない。
- 配布方法と互換バージョン方針は、公開境界を固定した後の別判断とする。

#### 実装の結果

| 判断 | 実装 |
|---|---|
| 内部ヘッダーの置き場所 | `components/fmsui/include/fmsui/{arena,element}.h` を `components/fmsui/src/internal/fmsui/` へ移した。include の書き方は `"fmsui/arena.h"` / `"fmsui/element.h"` のまま |
| include path | 通常 CMake は `target_include_directories(fmsui PUBLIC include PRIVATE src/internal)`、ESP-IDF は `PRIV_INCLUDE_DIRS src/internal`。ライブラリの外で与えるのは `sim/CMakeLists.txt` の `fmsui_test` だけ。`fmsui_interaction_test` と `fmsui_request_frame_test` は公開 API だけでビルドできた |
| `fmsui.h` | `arena.h` と `element.h` の include を消した。残りは通常向け 7 と `refresh.h` / `render.h` |
| `widget.h` / `widgets.h` | `Widget` から `ArenaObject` の継承と `arena.h` の include を消し、`virtual ~Widget()` を持たせた。`BuildContext` は `using BuildContext = Element;` から前方宣言 `class BuildContext;` にした。`widgets.h` は使っていなかった `element.h` の include を消した |
| アリーナ | `ArenaObject` を削除し、`Arena` は `Widget *` を記録して `reset()` で `~Widget()` を逆順に呼ぶ。`Arena::release()`(reset に加えてチャンクと記録用ベクタを解放)と `BuildArenas::release()`(両面を新しいビルドから解放し、`Arena::current()` が自分を指していれば null に戻す)を追加した。`~Arena()` は `release()` を呼ぶ(以前と同じ動作) |
| `BuildContext` の定義 | `element.h` の中身の無いクラス(コンストラクタとデストラクタは protected)で、`Element` だけが継承する。`Element::of(BuildContext &)` が Element へ戻し、`FmsTheme::of()` はこれで親を辿る |
| `ThreadId` / `ThreadIdFn` | 説明のコメントごと `element.h` から `app.h` へ移した。`BuildOwner` は `app.h` を include して使う |
| `FmsApp` | データメンバと private の `frame()` / `timerCb()` を削除した。状態は `app.cpp` の無名名前空間の `AppState`(`BuildArenas`、`BuildOwner`、builder、根の Element、LVGL の root / display / timer、画面サイズ、クロック、ビルド回数、統計と mutex)で、関数内 static を返す `state()` から使う。`requestFrame()`、`requester()`、`screenSize()`、`setClock()`、`setThreadId()` は `app.cpp` で定義した。コンストラクタは private、コピーは delete |
| `shutdown()` | タイマ停止 → ツリー破棄 → `arenas.release()` → `releaseStyleCache()`。ビルド回数、統計、クロック、スレッド識別子は以前どおり残す |
| 追加した単体テスト | `fmsui_test` に 3 件、32 checks(167 → 199)。`test_the_arena_destroys_each_widget_once_newest_first`、`test_a_build_lives_until_the_one_after_next`、`test_shutdown_releases_the_tree_and_both_arenas_and_init_works_again`。既存テストに同じ事実を直接判定するものは無かった(`test_arena_resets_between_builds` は件数とバイト数だけ、再 `init()` は操作テストの `Session` が毎回行うが、解放そのものは見ていない) |
| 公開ヘッダーの単独コンパイル | `consumers/host/CMakeLists.txt` の対象を 8 TU から 10 TU にした(`refresh` と `render` を追加) |
| 旧パスの機械判定(host) | `consumers/host/` に `EXCLUDE_FROM_ALL` の OBJECT ライブラリ `fmsui_internal_probe_arena` / `fmsui_internal_probe_element` を置いた(`internal_header_probe.cpp.in` から生成し、`fmsui::fmsui` にリンク)。`tools/ci/check_internal_headers.py` が (1) `include/` に 2 つが無く `src/internal/` にあること、(2) `fmsui` の全ソースに `src/internal` があること、(3) consumer の全 TU に無く、その include ディレクトリのどれにも 2 つが無いこと、(4) probe のビルドが「ヘッダーが見つからない」エラーで失敗すること、を判定する。`consumer_host.sh` が CTest の後に呼ぶ |
| 旧パスの機械判定(ESP-IDF) | `check_consumer_components.py` に 3 行を追加した。`fmsui` の `include/` に 2 つが無いこと、`compile_commands.json` で `__idf_fmsui` の全ソースに `src/internal` があること、`__idf_main` のどのソースの include path からも 2 つに届かないこと |
| 共通処理 | `compile_commands.json` の読み取りを `tools/ci/compile_commands.py` にまとめ、上の 2 つが import する |
| 文書 | USING.md の API 表、内部ヘッダーの説明、`shutdown()` の順序、保証しないもの、検証表。DESIGN.md に節「公開ヘッダーと内部ヘッダー」(置き場所、BuildContext、ArenaObject をやめた方法、FmsApp の状態の所有、shutdown の順序)を追加し、アリーナ節とスタイルキャッシュ節の記述を合わせた。README の構成表に 2 行を足し、`tools/ci/` の説明を更新した。`consumers/esp-idf/main/main.cpp` のコメントの参照先を `app.h` に直した |
| 変えていないもの | `ci.yml`(新しい検査は既存 job のスクリプトの中で走る)、デモ、`main/`、`consumers/shared/`、`fmsui_interaction_test` / `fmsui_request_frame_test` のソース |

**計画からの変更点が 1 つある。** 計画は「初期化、`shutdown()`、再初期化の順序を変えない」「動作を変えるための変更ではない」としていたが、
`shutdown()` にアリーナの解放を加えた。実装前の確認で、変更前の `shutdown()` はアリーナに触らず、直近 2 回のビルドの Widget
(コールバックのキャプチャを含む)とチャンクが次の `init()` 後の最初のビルドかプロセス終了まで残ることが分かり、計画の追加テスト
「`shutdown()` が両 arena と Element tree を解放し、同じプロセスで再 `init()` できる」がそのままでは成り立たなかったためである。
確認のうえ、ツリー破棄の後・スタイル解放の前に解放することにした。レイアウト、描画、統計値、表示は変わっていない(下のデモ PNG の比較)。

**ローカル検証**(2026-09-11。WSL2 Ubuntu 22.04 / gcc 11.4 / CMake 3.22.1 / Ninja 1.10.1 / 8 コア、Docker Desktop 29.7.2)

作業ツリーは `core.autocrlf=true` で CRLF のファイルを含むため、tar にせず、一時 index に `git add -A` した tree(`26063bc`)と
LVGL サブモジュール(`85aa60d`)をそれぞれ `git archive` したスナップショットから検証した。ホストは WSL の ext4 上、
ESP-IDF は `espressif/idf:v5.5.4@sha256:b9f2d6ea…` のコンテナ内の `/w` へ展開した。コマンドと環境変数は workflow と同じ。
番号は計画の「4. 文書と検証」の番号。

| 検証 | 結果 |
|---|---|
| 1. ヘッダーの配置と include path | `check_internal_headers.py` の 7 行すべて成功。`include/fmsui/` は公開の 10 ヘッダーだけで、`src/internal/fmsui/` に 2 つ。`fmsui` の 8 ソースすべてに `src/internal` があり、consumer の 14 TU(consumer 2、ヘッダー検査 10、probe 2)には無い。probe は 2 つとも `fmsui/arena.h: No such file or directory` / `fmsui/element.h: No such file or directory` で失敗した |
| 2. `consumer (host)` 相当 | `consumer_host.sh` rc 0、23 秒、554 ステップ、`warning:` 0 件。公開ヘッダー 10 TU が `-Werror` 付きでコンパイルされた。CTest 1/1、直接実行で 10 checks すべて ok |
| 3. `host (debug)` 相当 | 561 ステップ、`warning:` 0 件。CTest 3/3。`fmsui_test` 199 checks(うち新規 32)、`fmsui_interaction_test` 134 checks、`fmsui_request_frame_test` 34 checks、いずれも 0 failures。6 デモ成功 |
| 3・4. `host (asan-ubsan)` 相当 | 561 ステップ、ビルド 27 秒、`warning:` 0 件。`check_sanitized.py` で `fmsui` 8/8 ソースが計装済み。CTest 3/3、`check_no_sanitizer_reports.sh` 成功。直接実行でも 199 / 134 / 34 checks、0 failures、sanitizer の報告 0 件。6 デモ成功 |
| デモの回帰 | 変更前の `HEAD`(`d5242f1`、`fmsui_test` 167 checks)を同じ環境でビルドして 6 デモを撮り、変更後の debug と asan-ubsan の PNG と比べた。6 デモとも 3 枚の md5 が一致した |
| 5. `consumer (esp-idf, esp32p4)` 相当 | `consumer_idf.sh` rc 0、282 秒(device-build と並行)、1598 ステップ、`warning:` 0 件。component 検査の 5 行と内部ヘッダーの 3 行がすべて成功(`main` の 2 ソースから届かない)。`fmsui_consumer.bin` 600,624 bytes / 空き 447,952 bytes (43%)。前章のローカル値 600,720 bytes より 96 bytes 小さい |
| 6. `device-build (esp32p4)` 相当 | `device_build.sh` rc 0、513 秒(consumer と並行)。6 構成とも `warning:` 0 件、コンパイル対象 1594 件、Examples / Demos 0 / 0 件、`dependencies.lock` は不変 |
| workflow と検査スクリプト | actionlint 1.7.12 で `ci.yml`(変更なし)はエラー 0 件。shellcheck 0.11.0 で `tools/ci/*.sh` の指摘 0 件。`tools/ci/*.py` は `py_compile` に成功 |

device-build の bin サイズ(最小 app 領域はどれも 1,536,000 bytes)。差の中身は比較していない。

| 構成 | 前章 | この実装 | 空き |
|---|---:|---:|---:|
| 既定 | 906,832 bytes | 906,960 bytes (+128) | 629,040 bytes (41%) |
| `m0` | 878,464 bytes | 878,464 bytes (0) | 657,536 bytes (43%) |
| `m1` | 912,688 bytes | 912,688 bytes (0) | 623,312 bytes (41%) |
| `catalog` | 934,272 bytes | 934,272 bytes (0) | 601,728 bytes (39%) |
| `fplan` | 901,792 bytes | 901,792 bytes (0) | 634,208 bytes (41%) |
| `reorder` | 918,336 bytes | 918,464 bytes (+128) | 617,536 bytes (40%) |

**壊れた実装を検出できることの確認 (ミューテーション)。** 1 か所ずつ壊して、CI と同じスクリプトで確かめた。

| # | 壊し方 | 結果 |
|---|---|---|
| T1 | `shutdown()` から `arenas.release()` を消す | shutdown のテストが 2 回とも `g_live_tracers == 0` と `Arena::current() == nullptr` で失敗した(4 failures) |
| T2 | `Arena::reset()` が `~Widget()` を呼ばない | 新しい 3 テストすべてが失敗した(13 failures) |
| T3 | `beginBuild()` が前回のビルドの面も reset する | 2 世代のテスト(次のビルドの間に前のビルドの Widget が消えている)と shutdown のテストが失敗した(8 failures) |
| C1 | 通常 CMake の `fmsui` が `src/internal` を PUBLIC にする | `consumer_host.sh` rc 1。consumer の 14 TU すべてに `src/internal` があり、probe 2 つがコンパイルできた |
| C2 | `element.h` を `include/fmsui/` にもコピーする | rc 1。`include/` に内部ヘッダーがあり、`element` の probe がコンパイルできた |
| C3 | consumer の target 自身に `src/internal` を足す | rc 1。`main.cpp` と `consumer_page.cpp` に `src/internal` があると判定した。probe は失敗したままなので、この形の漏れを捕まえるのは include path の行 |
| C4 | `fmsui.h` が `fmsui/element.h` を再び include する | consumer のビルドそのものが `fatal error: fmsui/element.h: No such file or directory` で失敗した |
| D1 | ESP-IDF の `fmsui` が `src/internal` を `INCLUDE_DIRS` に入れる | ビルドは通ったが、`consumer_idf.sh` が rc 1。`main` の 2 ソースから内部ヘッダーに届くと判定した |

T1〜T3 は最初、元に戻したファイルの mtime が壊したファイルのオブジェクトより古く、ninja が再コンパイルしなかったため、前のミューテーションが残ったまま走っていた。
元に戻すたびにスナップショットから展開し直して `touch` する形でやり直し、表はやり直した結果を載せた。戻した後はソースがスナップショットとバイト一致し、
`fmsui_test` 199 checks・0 failures、CTest 3/3 を確認した。C1〜C4 は毎回新しいコピー、D1 は新しいコンテナで行ったので、この問題の影響は無い。

**GitHub-hosted run**

この実装の commit を push した後の run を、ここに記録する。

**検証コマンド**

```bash
# ホスト(workflow の run: と同じ)
export ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
cmake -S sim -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFMSUI_SANITIZE=ON -DFMSUI_WERROR=ON   # debug は OFF
cmake --build build
python3 tools/ci/check_sanitized.py build                                                   # asan-ubsan だけ
ctest --test-dir build --output-on-failure --timeout 120
bash tools/ci/render_demos.sh build/fmsui_sim ci-out/demos
bash tools/ci/consumer_host.sh ci-out/consumer-host      # 最後に check_internal_headers.py を呼ぶ

# ESP-IDF(ESP-IDF 5.5.4 のコンテナ内)
. "$IDF_PATH/export.sh"
bash tools/ci/consumer_idf.sh ci-out/consumer-idf        # check_consumer_components.py に内部ヘッダーの 3 行
bash tools/ci/device_build.sh build-ci ci-out/device
```

**実装前に確認して決めたこと**

- 内部ヘッダーは `components/fmsui/src/internal/fmsui/` に置き、include の書き方は変えない。内部の include path が漏れると
  旧パスがコンパイルできてしまうので、「旧パスを include できない」判定がそのまま漏れの検出になる。
- `shutdown()` で両アリーナを解放する(上記)。
- ローカル検証の後にこちらで commit・push し、GitHub-hosted run の結果を記録してからレビュー待ちにする。未 push の `d5242f1` と、この計画章の追加も一緒に送る。

**確認せずに決めたこと**

- `BuildContext` は公開側では不完全型(前方宣言だけ)にし、定義を `element.h` の空のクラスにした。
- アリーナは基底クラスを介さず `Widget *` を記録する形にし、`ArenaObject` は内部にも残さず削除した。
- `FmsApp` のコンストラクタを private にし、コピーを delete にした。状態がインスタンスのものではなくなったため。`instance()` 以外で作っているコードは無かった。
- `AppState` は `FmsApp::instance()` と同じく関数内 static にした。
- `BuildArenas::release()` はチャンクまで free し、`Arena::current()` を null に戻す。`highWaterMark()`(統計の `arena_bytes`)は戻さない。
- `Widget::createElement()` は public の純粋仮想のまま残した(`Element` の前方宣言で足りるため)。
- 旧パスの判定は CTest にせず、スクリプトからビルドして失敗の理由まで見る形にした。完了条件の「CTest 1/1」を保つためでもある。
- ESP-IDF consumer にも内部ヘッダーの判定を足した。計画には host 側しか書かれていないが、ESP-IDF は `PRIV_INCLUDE_DIRS` という別の経路で渡すため。
- `compile_commands.py` を共通モジュールにした。既存の `check_sanitized.py` / `check_lvgl_sources.py` は変えていない。
- ローカル検証のスナップショットは、`core.autocrlf=true` の作業ツリーを tar にせず、一時 index に `git add -A` した tree を `git archive` して作った。
  GitHub のチェックアウトと同じ LF の内容になる。

## 計画中・未実装

いまのところ無し。次の判断が決まったらここに書く。
