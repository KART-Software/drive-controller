# Drive Controller

全日本学生フォーミュラ大会 ドライブコントローラ。Teensy 4.1 上で電子制御スロットル
(ETC)・オートシフター・各種センサー処理を担い、Web コンソールからライブモニタリングと
キャリブレーションを行う。

## サブシステム

- **ETC (電子スロットル)** — APPS / ITTR(IST コントローラ指令) からターゲット開度を決め、
  PID でスロットルモーターを制御。モード (Normal / Restricted / Calibration / MotorOff) は
  CAN `MODE_SELECT` で選択。
- **オートシフター** — クイックシフター/シーケンシャルの UP/DOWN を制御。CAN `AUTO_SHIFT`
  で ON(自動)/OFF(手動) 切替。設計は [`dc-firmware/auto_shifter_spec.md`](./dc-firmware/auto_shifter_spec.md)。
- **Launch Control** — 現在ビルドフラグ (`-DLAUNCH_CONTROL_ENABLED`) で **凍結中**。

## 使い方

キャリブレーション・設定・モニタリングは Web コンソールから行う。

1. **Chrome / Edge** でコンソールのページを開く。
2. ボックスと PC を USB 接続し、**Connect** でポートを選択。
3. ヘッダーの **ETC / Drivetrain** タブでページ切替 (接続は維持される)。

| ページ | 内容 |
|---|---|
| **ETC** | APPS/TPS/ITTR/BPS/Target のモニタ・時系列チャート、プラウシビリティフラグ、APPS/TPS/アイドリングのキャリブレーション、PID・ターゲットカーブ・モード設定 |
| **Drivetrain** | ギア/クラッチ/車輪速/エンジン RPM/IMU のモニタ・チャート、ギア・クラッチのキャリブレーション、IST/NORMAL ミッション切替、オートシフター設定 |

- キャリブレーションは各センサーを所定位置にした状態でボタン押下 → 現在値をキャプチャ。
- 変更は上部の **Save** で flash に永続化 (`Revert` で取消)。再起動後も保持される。
- `?mock` を URL に付けると実機なしでモックデータで動作確認できる。

> ⚠️ キャリブレーション中もモーターは動作する (電スロが動く)。スロットルに指を入れる際は
> モーター電源を切る等、細心の注意を払うこと。

---

## 開発者向け

### リポジトリ構成

2 つの連携サブプロジェクトが `spec/proto/` の Protobuf スキーマを共有する:

| ディレクトリ | 内容 |
|---|---|
| `dc-firmware/` | Teensy 4.1 ファームウェア (PlatformIO + Arduino)。ETC、オートシフター、Launch Control (凍結中)、センサーサンプリング、CAN、シリアルプロトコル |
| `dc-console/` | Preact + Vite の Web アプリ。Web Serial API で USB シリアル通信し、ライブモニタ・キャリブレーション |
| `spec/proto/drive_controller.proto` | ホスト ↔ デバイス間ワイヤフォーマットと永続化 `Config` の単一ソース |

ホスト ↔ デバイスのワイヤフォーマットは `COBS( protobuf_bytes ‖ crc16_le ) 0x00`。

> アーキテクチャ・並行性モデル・各サブシステムの設計の詳細は [`CLAUDE.md`](./CLAUDE.md) を参照。

### ビルド

ファームウェア (PlatformIO):

```bash
git submodule update --init --recursive      # FlexCAN_T4 取得 (初回)
pio run -d dc-firmware -e teensy41            # ビルド
pio run -d dc-firmware -e teensy41 -t upload  # アップロード
```

コンソール (Vite + Preact):

```bash
cd dc-console
pnpm install
pnpm dev      # 開発サーバー
pnpm build    # tsc --noEmit && vite build → dist/
```

`spec/proto/drive_controller.proto` を編集したら両側で再生成が必要 (firmware は
再ビルドで自動、console は `pnpm run gen:proto`)。詳細は `CLAUDE.md`。

## ハードウェア

- アクセルペダルポジションセンサー [AS-3](https://www.ipros.jp/product/detail/2000527534/)
- ブレーキ圧センサー [MLH01KPGB06A](https://sps.honeywell.com/jp/ja/products/advanced-sensing-technologies/industrial-sensing/industrial-sensors/industrial-pressure-sensors/mlh-series)
- DC モーター [モータ DCX26L GB KL 12V ギアヘッド GPX26 A 35:1 B7FEDFA2887B](https://www.maxongroup.co.jp/maxon/view/configurator?from=%2Fmaxon%2Fview%2Fcontent%2Fcart&configId=B7FEDFA2887B) or [モータ DCX26L GB KL 12V ギアヘッド GPX26 A 26:1 B8162356240B](https://www.maxongroup.com/camroot/pdf//b8162356240b/b8162356240b_3.pdf)
- DC モータードライバー [G2 ハイパワーモータードライバ 18v17](https://www.pololu.com/product/2991)
- スロットルポジションセンサー [CP-20H](https://www.midori.co.jp/products/potentiometer/angle_sensor/orange_pot/cp-20h) or [CP-3HABS](https://www.midori.co.jp/products/cp-3habs/)
