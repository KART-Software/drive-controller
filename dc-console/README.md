# ETC Console

Web Serial API を使ったブラウザベースの ETC (Electronic Throttle Control) モニタリング・設定ツール。

## 技術スタック

- **Preact** + **TypeScript**
- **Vite** (ビルド / dev server)
- **uPlot** (時系列チャート)
- **protobuf-es** (Protocol Buffers ランタイム / コード生成)
- **buf** (proto コード生成ツール)

## セットアップ

```bash
pnpm install
```

## 開発

```bash
pnpm dev
```

### Proto コード生成

`spec/proto/drive_controller.proto` を変更した場合:

```bash
pnpm run gen:proto
```

`src/proto/drive_controller_pb.ts` が再生成される。

## ビルド

```bash
pnpm build
```

`dist/` に静的ファイルが出力される。

## 機能

- **センサーモニター** — APPS, TPS, ITTR, BPS, Target のリアルタイム表示 (50Hz)
- **時系列チャート** — uPlot による 5 チャンネルのライブグラフ
- **エラーステータス** — 9 種類のプラウシビリティチェックの LED 表示
- **キャリブレーション** — センサー Min/Max 設定、アイドリング設定、手動制御
- **プラウシビリティフラグ** — 個別チェックの有効/無効切り替え
- **設定管理** — デバイスからの取得、JSON エクスポート/インポート
- **デバッグログ** — デバイスからのメッセージ表示

## 対応ブラウザ

Web Serial API が必要。**Chrome** または **Edge** で動作。

## プロトコル

ファームウェアとの通信は **Protocol Buffers** + **COBS** フレーミング + **CRC16-CCITT** 誤り検出で行う。

### ワイヤフォーマット

```
COBS( proto_bytes ‖ crc16_le ) 0x00
```

- `proto_bytes` — protobuf エンコードされたメッセージ
- `crc16_le` — proto\_bytes の CRC16-CCITT (リトルエンディアン 2 バイト)
- COBS エンコード後に `0x00` デリミタを付加

### メッセージ

| 方向 | メッセージ | 説明 |
|------|-----------|------|
| FW → Console | `DeviceToHost.sensor` | センサーデータ (50Hz) |
| FW → Console | `DeviceToHost.debug` | デバッグログ |
| FW → Console | `DeviceToHost.response` | コマンドレスポンス |
| Console → FW | `HostToDevice.command` | コマンド送信 |

スキーマ定義は `spec/proto/drive_controller.proto` を参照。
