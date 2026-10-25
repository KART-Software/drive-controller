# SD ログビューア (console) 仕様

SD カードのログ (`dc_NNNN[_YYYYMMDD_HHMMSS]/NNNN.bin`、docs/loop_nonblocking_spec.md §7) を console から開き、
run の一覧を見て CSV に書き出す。チャート表示は次の段階 (§6)。

- 状態: v1 (一覧 + CSV) と v2 (チャート、§6) 実装済み (2026-10-25、feature/log-viewer)。
- 前提: SD カードは dc-sd-reader (USB Mass Storage) で PC にマウントされているか、ログをコピーしたフォルダがある。
  console は Chrome / Edge (Web Serial と同じ前提。File System Access API を使う)。

## 1. ファーム側の変更: rpm / km/h をログに足す

ログには回転のパルス周波数 (Hz) しか無く、rpm / km/h への換算には歯数とタイヤ径 (config) が要る。持ち帰ったログを
単体で読めるように、センサーが CAN (0x607 / 0x609) 向けに計算している値をレコードにも入れる。Hz は生の計測値として残す
(歯数の設定ミスの切り分けに要る)。

`LogRecord` の末尾 (`reserved` の後、オフセット 152) に追加。既存のオフセットは変えない。152 → **176 B**。

| オフセット | 型 | 名前 | 内容 |
|---|---|---|---|
| 152 | `float[4]` | `wheel_kmh` | FL, FR, RL, RR の車速 [km/h] (`WheelSpeedSensor::kmh()`。タイヤ径が未設定なら 0) |
| 168 | `float` | `engine_rpm` | エンジン回転数 [rpm] (`RotationSensor::rpm()`) |
| 172 | `float` | `clutch_rpm` | クラッチ後 (出力軸) [rpm] |

- `log_record.hpp`: フィールド追加、`static_assert(sizeof(LogRecord) == 176)`、`offsetof(wheel_kmh) == 152`。
- `log_record_builder.cpp`: `hub.pulseWheelFL().kmh()` … `hub.pulseEngine().rpm()`、`hub.pulseClutchRpm().rpm()`。
- `tools/decode_log.py`: `RECORD_FMT` に `6f` を追加、列 `wheel_fl_kmh, wheel_fr_kmh, wheel_rl_kmh, wheel_rr_kmh, engine_rpm, clutch_rpm`
  を `clutch_hz` の後ろに。
- `docs/loop_nonblocking_spec.md` §7.2 の表、CLAUDE.md の「152 B」。
- `SENSOR_LOG_VERSION` は 0 のまま (開発中)。`LogHeader.record_size` が 176 になるので、古いファイルは `record_size` で見分けられる。
- 1 ファイル (100 MB) は 11.3 分 → **9.7 分**。リング (2048 レコード) は 360 KB (DMAMEM)。

## 2. console の入口

- 3 つ目のタブ **Logs** (`/logs`)。シリアル接続は不要 (接続中でも使える)。
- SD リーダー接続中の画面 (「SD カードを PC にマウント中」) に「ログを開く」ボタン。押すと `/logs` に移り、そのクリックを
  ユーザー操作としてディレクトリ選択を開く。
- `showDirectoryPicker()` が無いブラウザでは、Chrome / Edge が要る旨を表示して終わり (v1 ではフォールバック無し)。

## 3. ディレクトリと run の一覧

- 「フォルダを開く」で `showDirectoryPicker({ mode: "read" })`。選ぶのはドライブのルート (KART SD Reader) か `dc_NNNN` フォルダ。
  - ルートなら、直下の `^dc_\d{4}` にマッチするディレクトリを run とする (ファームと同じ規則)。
  - `dc_NNNN` フォルダそのものなら、その 1 つを run とする。
  - 中の `^\d{4}\.bin$` を名前順 (= `file_index` 順) に並べる。
- ハンドルは IndexedDB (`kart-console` / `log-viewer` / `dir`) に保存し、次回は「前回のフォルダを開く」で 1 クリック。
  権限が切れていれば `requestPermission({ mode: "read" })` をそのクリックで呼ぶ。
- run ごとに表示する項目 (ファイルの先頭と末尾だけ読む。全走査はしない):

| 項目 | 出し方 |
|---|---|
| 名前 | ディレクトリ名 |
| 日時 | 先頭ファイルの `rtc_epoch` (0 なら名前の `_YYYYMMDD_HHMMSS`、それも無ければ「—」)。ローカル時刻で表示 |
| 長さ | 最後の有効レコードの `t_us` − 先頭レコードの `t_us`。最後の位置は末尾ファイルをゼロ領域に対して二分探索 (有効なレコードは `t_us` ≠ 0。ファイルは事前確保でゼロ埋め) |
| ファイル数 / サイズ | ファイル数と合計バイト。事前確保サイズ (100 MB) ではなく有効データの長さ (最後の有効レコードまで) |
| drops | 最後の有効レコードの `log_drops` (65535 で飽和) |
| 状態 | header の検査結果: `magic` 不一致 / `version` ≠ 0 / `record_size` ≠ 176 は赤で理由を出し、書き出し不可 |

- 空のファイル (header 無し = 作っただけで電源が切れた) は飛ばす (Python と同じ)。

## 4. CSV 書き出し

- 列と順序は `tools/decode_log.py` の `COLUMNS` と**同一** (両者の出力が同じになるようにする)。
- 1 run (ディレクトリ) = 1 CSV。`file_index` 順につなぐ。
- 終端と欠けの判定は Python と同じ: 全バイトがゼロのレコード、ファイルの半端、`t_us` の後退で止める。`t_us` の飛び
  (> 1500 µs) は止めずに数え、完了時に「欠け N 回」と表示する。
- 数値の表記: 整数はそのまま、float は JS の `String()` (最短表現。Python の `repr` と同じ値を表し、1e-4 未満と 1e16 以上の
  指数表記だけが異なる)。`abs_time` は `rtc_epoch + (t_us - rtc_t_us) / 1e6`、`rtc_epoch` が 0 なら空。
- オプション: 間引き (1 / 10 / 100 行に 1 行。`t_us` の順で先頭から数える)。
- 書き込みは `showSaveFilePicker({ suggestedName: "<run 名>.csv" })` で得た `FileSystemWritableFileStream` に
  ストリームで流す (1 本の文字列を作らない)。デコードは Web Worker で、`File.slice()` を 8 MB (レコード境界に揃える)
  ずつ読み、行をまとめて (数千行ごとに) メインスレッドへ渡して書く。
- 進捗 (バイト数 / レコード数) とキャンセル。失敗 (途中で読めない、ファイルが変わった) はエラー表示で止め、部分ファイルは
  そのまま残す (書き直せる)。
- 100 MB のファイル 1 つで約 657k レコード、CSV は約 150 MB。目標: 読み + 書きで 10 秒以内 (USB 15 MB/s なら読みは 7 秒)。

## 5. 実装の置き場

- `dc-console/src/log/log_record.ts`: レイアウト表 (オフセット・型・名前)、`VERSION = 0`、`RECORD_SIZE = 176`、`HEADER_SIZE = 32`、
  `GAP_WARN_US = 1500`、CSV の列。`log_record.hpp` と `decode_log.py` と揃える旨をコメントに。
- `dc-console/src/log/decoder.ts`: header / record の読み取り、終端判定、`findLastRecord()` (二分探索)。
- `dc-console/src/log/csv.worker.ts`: run → CSV の行 (Worker)。
- `dc-console/src/log/dir-handle.ts`: IndexedDB の保存と権限。
- `dc-console/src/components/LogViewer.tsx`: 一覧と書き出しの UI。`App.tsx` にルート `logs` とタブ。
- 検証: 実機のログ 1 run を `decode_log.py` と console の両方で CSV にし、数値として比較して一致すること
  (`tools/compare_csv.py`。表記の差は無視: 整数値の float は Python が `1.0`、JS が `1`。改行は Python が CRLF、console が LF)。
  console 側のデコーダは Node でも走る: `cd dc-console && pnpm dlx tsx scripts/export_csv.mts <dc_NNNN> out.csv`。
  ブラウザの通し (一覧 → CSV、開く → ズーム → 範囲 CSV、いずれも Python の出力と突き合わせ) は `scripts/e2e_logs.py`
  (Playwright のヘッドレス Chrome。picker は OPFS で代替。使い方は冒頭のコメント)。
  2026-10-25 のベンチ: 42,274 レコードの run で Python / Node / ブラウザの 3 つが 65 列すべて一致。Node でのデコードは 0.1 s。

## 6. チャート (v2)

run を開いて、チャネルを時系列で見る。1 run の全体から 1 kHz の生の値まで、同じ画面でズームして行き来できること。

### 6.1 画面

- 一覧の run 行に「開く」。開くと一覧は 1 行のヘッダ (run 名・日時・長さ・「一覧に戻る」) に畳まれ、その下にパネルが縦に並ぶ。
- パネルは固定のグループ。表示するパネルをチェックで選ぶ (選択は localStorage に覚える)。パネル内の系列は uPlot の凡例クリックで消せる。

| パネル | 系列 | 単位 |
|---|---|---|
| スロットル | apps1, apps2, ittr, tps1, tps2, target_tp, duty | % |
| 車速 | wheel_fl/fr/rl/rr_kmh | km/h |
| 回転 | engine_rpm, clutch_rpm | rpm |
| ブレーキ / クラッチ | bps, clutch | (値そのまま) |
| 加速度 | ax, ay, az | mg |
| 角速度 | gx, gy, gz | dps |
| 状態 | gear, mode, autoshift_state, flags の各ビット | 段状 (stepped) |
| システム | loop_max_us, sd_max_us, tick_max_us, log_drops, can_fault, can_tec, can_rec | µs / 回 |
| ADC 生値 | adc0〜adc7 | raw |

- x 軸は run 開始からの秒 (`t_us` − 先頭の `t_us`)。header に `rtc_epoch` があれば時計表示 (ローカル時刻) に切り替えられる。
- x 軸と十字カーソルは全パネルで同期 (uPlot の `cursor.sync`)。凡例にカーソル位置の値。
- 操作: ドラッグで範囲を拡大、ホイールでカーソル位置を中心に拡大縮小、Shift + ドラッグで移動、「全体」で run 全体に戻す。
- 欠け (`t_us` の飛び > 1500 µs) は半透明の帯で全パネルに描く。
- 「この範囲を CSV」: いまの表示範囲 [t0, t1] だけを §4 の経路で書く (`encodeRun` に範囲を足す。範囲外のファイルは header と先頭 / 末尾の `t_us` で飛ばす)。

### 6.2 データ

- チャネル表 `src/log/channels.ts`: 名前・単位・レコード内オフセット・型・パネル。`log_record.ts` のレイアウトに従う唯一の場所で、
  CSV の列とは独立 (CSV は `recordToRow`)。
- **概観**: run を開いたとき Worker (`src/log/chart.worker.ts`) が全ファイルを 1 回走査し、全数値チャネルのバケットごとの min / max、
  欠けの一覧、先頭 / 末尾の `t_us` を作って保持する。バケット幅 = `max(1 ms, run の長さ / 20000)` (10 分の run で 30 ms、1 時間で 180 ms)。
  メモリは 2 万バケット × 約 50 ch × min / max × 4 B ≈ 8 MB。走査中は進捗 (バイト数) を出し、終わるまでチャートは出さない
  (USB 直読み 15 MB/s で 100 MB ≈ 7 s、1 GB ≈ 70 s)。
- **範囲クエリ** `query(t0, t1, width, channels)`: 必要な分解能 (t1 − t0) / width が概観のバケット幅以上なら概観から切り出す。
  それより細かければファイルからその範囲のバイト列だけ読む (固定長なので `t_us` → レコード番号は二分探索。読むのは
  範囲 + 両端 1 バケット)。範囲が width × 2 レコード以下なら生の 1 kHz の値、それより多ければ画素ごとの min / max。
  結果は `{ t: Float64Array, series: { min: Float32Array, max: Float32Array }[] }` (生のときは min = max)。
- クエリはズーム / パン / リサイズのたびに発行し、前のクエリが終わる前に次が来たら前の結果は捨てる (世代番号)。
  Worker は 1 run につき 1 つで、ファイルと概観を持ち続ける。別の run を開いたら作り直す。
- uPlot への渡し方: min / max の 2 系列を同色で描き (max を線、min との間を薄く塗る)、生のときは線 1 本。

### 6.3 置き場

- `src/log/channels.ts`、`src/log/chart.worker.ts` (概観・範囲クエリ)、`src/log/overview.ts` (走査とバケット化の純粋ロジック。
  Node でも走らせて検証する)、`components/LogChart.tsx` (パネルの束と操作)、`components/LogPanel.tsx` (uPlot 1 枚)。
- `LogViewer.tsx`: 行の「開く」、ヘッダへの畳み込み、範囲 CSV。
- 検証: `cd dc-console && pnpm dlx tsx scripts/check_chart.mts <dc_NNNN>` が、概観の min / max と範囲クエリ (生 / 画素バケット) を
  `recordToRow` (CSV と同じ経路、`decode_log.py` と一致済み) からの総当たりと比べる。ベンチの run (42 s、19,997 バケット × 2.1 ms) で
  一致、概観の作成は Node で 0.08 s。`scripts/e2e_logs.py` で 開く → ドラッグ拡大 → ホイールで 1.7 s (生) → 範囲 CSV 1,656 行が
  Python の同じ範囲の行と全列一致 (2026-10-25)。

## 7. 次の段階 (この仕様の範囲外)

- レイアウトを 1 つの定義 (JSON) から C++ / Python / TS に生成する (フォーマットが頻繁に変わるようになったら)。
- 複数 run の比較、チャネルの演算 (例: 前後輪の差)。
