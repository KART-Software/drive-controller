# セルモーターリレー 仕様書 (ドラフト)

ドライバーのセルスイッチ (CAN 0x740 byte3 `starter`) でセルモーターのリレーを駆動する。
エンジンがかかったら自動で切り、押した直後は CAN が切れても一定時間回し続ける。

## 1. 決定事項

| 項目 | 決定 |
|---|---|
| 出力ピン | 暫定でピン 6 (`constants.hpp` の `STARTER_RELAY_PIN`)。HIGH = リレー ON。config には入れない |
| エンジンがかかった判定 | エンジン回転がしきい値 (既定 2000 rpm) を超えた状態が確認時間 (既定 1 s) 続いた |
| しきい値と時間 | config (`StarterConfig`) に入れ、console から編集する |
| 押したときにエンジンがもう回っていた | ON にしない |
| 押し続けた場合 | 上限なしで回し続ける |
| 他の安全条件 | なし (プラウシビリティ ①、SIG_IN ②、MOTOR_OFF ③ とは無関係に動く) |
| 見える化 | SD ログに載せる。telemetry には載せない |
| ベンチ用の console 上書き | 作らない |

## 2. 動作

`starter::StarterController` を `tickISR` から毎 ms `tick()` する (時間で判定するので tick に置く)。状態は 2 つ。

| 状態 | 条件 | 次の状態 |
|---|---|---|
| Off | セルスイッチが 0 → 1 に変わった、かつ回転がしきい値以下 | Cranking (リレー ON) |
| Off | セルスイッチが 0 → 1 に変わったが、回転がしきい値を超えている | Off のまま (押し直すまで ON にしない) |
| Cranking | 回転がしきい値を超えた状態が確認時間続いた | Off |
| Cranking (開始から保持時間 未満) | — | Cranking のまま。セルスイッチが 0 でも、CAN が途絶えても続ける |
| Cranking (開始から保持時間 以上) | セルスイッチが 0 (途絶による 0 を含む) | Off |

- 「超えた状態が続いた」は連続で数える。途中でしきい値以下に落ちたら数え直す。
- ON にするのは押した瞬間 (0 → 1) だけ。押したまま Off になった場合 (エンジンがかかった、など) は、一度 0 に戻して押し直すまで ON にしない。
- 起動時はリレー OFF。0x740 を受信するまではセルスイッチは 0 として扱うので、受信開始と同時に押されていれば、その時点で押した扱いになる。

## 3. 入出力

- **セルスイッチ**: `ControlInput::starterPressed(nowMs)` を追加。0x740 を受信していて途絶していないときだけ `CanRxData::starter` を返し、途絶 (200 ms) なら false (既存のフォールバックと同じ)。
- **エンジン回転**: `sensorHub.pulseEngine().getRpm()` (直近 100 ms の窓)。
- **リレー出力**: `STARTER_RELAY_PIN` (暫定 6)。起動時に OUTPUT / LOW。
- **CAN 0x60A DC_Status**: `starter_relay` に実際の出力を載せる (今は常に false)。

## 4. config

- proto: `StarterConfig` を追加し、`Config` に `StarterConfig starter = 6`。
  ```proto
  message StarterConfig {
    float  engine_running_rpm = 1;  // これを超えたらエンジンがかかったとみなす (既定 2000)
    uint32 running_confirm_ms = 2;  // かかった状態がこの時間続いたらリレーを切る (既定 1000)
    uint32 min_hold_ms        = 3;  // ON にしてからこの時間はスイッチや CAN に関係なく回す (既定 5000)
  }
  ```
- 既定値は `default_config.hpp` の `DEFAULT_CONFIG` に入れる (flash の config に starter が無ければ既定値のまま)。
- `Configurator::overlayConfig()` に `has_starter` の 1 行を追加、`calibrate()` で `StarterController::setConfig()` に適用する。
- 両側で再生成 (ファームは pre_build、console は `pnpm run gen:proto`)。

## 5. SD ログ

- `LogRecord.flags` の空きビットを 2 つ使う。レコードのサイズとレイアウトは変わらない。
  - bit 6 `LOG_FLAG_STARTER_RELAY`: リレー ON
  - bit 7 `LOG_FLAG_STARTER_SWITCH`: セルスイッチ (`starterPressed`、途絶時は 0)
- `SENSOR_LOG_VERSION` は 4 のまま (v5 は Phase 3 のレイアウト変更で使う予定)。旧ファイルではこのビットは 0 (= OFF) なので、意味は変わらない。
- `tools/decode_log.py` の `FLAG_BITS` に `f_starter_relay` / `f_starter_switch` を追加する。

## 6. console

- config 編集画面を追加 (`AutoShiftTuner` と同じ形の小さなパネル、`DrivetrainView` に置く)。3 項目の入力と保存。

## 7. 確認

- 3 種類のビルド、console の `pnpm build`。
- ベンチ: 起動時にリレーのピンが LOW であること、config の読み書き (console から変更 → 保存 → 再起動後も残る)、SD ログのビットが 0 であること。
- **ベンチでは確かめられないこと**: セルスイッチによる ON / 保持 / OFF (CAN を流す手段がなく、上書きも作らないため)。CAN を流せるようになったら、または実車で確認する。

## 8. 触るファイル

`spec/proto/drive_controller.proto`、`src/starter/starter_controller.{hpp,cpp}` (新規)、`src/control/control_input.{hpp,cpp}`、`src/main.cpp`、`src/constants.hpp`、`src/default_config.hpp`、`src/configurator.{hpp,cpp}`、`src/log_record_builder.{hpp,cpp}`、`src/util/log/log_record.hpp`、`tools/decode_log.py`、`dc-console/src/components/` (新しいパネル + `DrivetrainView.tsx`)、`CLAUDE.md`。
