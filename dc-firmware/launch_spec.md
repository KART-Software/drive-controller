# Launch Control 実装メモ (超ざっくり)

## 概要

クラッチモーターを自動制御してローンチ（発進）を最適化する。
クラッチ位置の絶対値よりも **スリップ率をフィードバック変数** とすることで、
ミートポイントのドリフトに自然に適応する。

## 制御ループ構造

```
目標スリップ率 (設定値 e.g. 15%)
      │
      ▼
 [外側 PID]  ← 実スリップ率 (RPM vs 車輪速から算出)
      │
      │ クラッチ位置指令 (0-100%)
      ▼
 [内側 PID]  ← ClutchSensor (実クラッチ位置)
      │
      ▼
 ClutchMotor (PWM)
```

## 状態機械

```
Idle
 └─ ドライバ操作 (ボタン等) ─→ Ready
                                  │
                                  │ 発進条件成立 (高回転 + クラッチ踏み込み)
                                  ▼
                               Approach  ← クラッチをミートポイント付近まで素早く移動
                                  │
                                  │ スリップ開始検出
                                  ▼
                            Slip Control  ← 外側PID でスリップ率を目標値に維持
                                  │
                                  │ 車輪速がエンジンRPMに追いついた (スリップ≈0)
                                  ▼
                             Full Engage  ← クラッチ全接続
                                  │
                                  ▼
                                Idle
```

## ミートポイントの学習

- Approach フェーズでスリップ開始を検出した時点のクラッチ位置を記録
- 次回の Approach 時の初期目標位置として使用 → 高速アプローチが可能
- Flash に保存して電源断をまたいで保持

## 実装コンポーネント (将来)

```
dc-firmware/src/launch/
  launch_controller.hpp/.cpp   — 状態機械 + 外側PID
  clutch_motor.hpp/.cpp        — 内側PID + DcMotor (DcMotorと同パターン)
  bite_point_estimator.hpp     — ミートポイント学習・保持
```

## 今回実装済み

- `ClutchSensor` — ADC ch9 のアナログ入力を 0-100% に変換
- `SensorHub` への統合
- proto `Sensor` メッセージへの送信 (`clutch_raw`, `clutch`)
- キャリブレーション (Min/Max) を ConfigModel + flash に保存
