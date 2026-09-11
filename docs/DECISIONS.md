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

## 計画中・未実装

いまのところ無し。次の判断が決まったらここに書く。
