# 開発引き継ぎ資料 — ETC 制御改善 / 同定実験モード

作成: 2026-09-12。開発マシン移行 (旧 PC → `dev`) にあたっての現状整理。

## 1. いま何をしているか

ETC (電子スロットル) の制御改善プロジェクト。症状「目標開度まで行ききらない /
オーバーシュートが大きい」の根本原因 (バネ保持 duty を遅い位置 I 項が担う構造 +
スティクション + anti-windup 未配線) を特定し、**モデル同定 → FF+PID (または
速度制御カスケード) への移行**を計画。同定実験用のファーム/console 実装まで完了。

- 一次ソース: [etc_control_improvement_plan.md](etc_control_improvement_plan.md)
  (モデル・制御則・ロードマップ・未決一覧 §9)
- 実験モード仕様: [etc_experiment_mode_spec.md](etc_experiment_mode_spec.md)
  (**Q1〜Q18 すべて決定済み**。実装はこの仕様に準拠)

## 2. リポジトリの状態 (重要)

**HEAD = e60e0f5 (develop) のまま、実装一式が意図的に未コミット**。
23 ファイル +1391/−27 行 + 未追跡 4 点 (experiment_runner.{hpp,cpp} /
ExperimentPanel.tsx / docs/)。

- ファーム: 実験 FSM (`etc/experiment_runner`)、アーミング層④統合 (main.cpp)、
  anti-windup 有効化 + `setPidGains` 割り込み保護 + duty getter
  (motor_controller)、`EtcTarget::setManualTarget`、実験中の競合コマンド拒否
  (command_controller)、LogRecord **v3 (136 B)** + decode_log.py
- proto: `StartEtcExperimentCmd` / `StopEtcExperimentCmd` (oneof 34/35)。
  **両側再生成済み** (ファームは pre_build、console は gen:proto)
- console: ExperimentPanel (開始 3 + 停止、確認ダイアログ、実行中 disable)、
  mock-serial の実験モック (`?mock` で実機なし確認可)
- ビルド: `pio run -e teensy41` ✓ / `pnpm build` (tsc + vite) ✓ (2026-08-13 時点)

コミット時の分割案: (1) docs、(2) anti-windup+計測基盤、(3) 実験モード本体、
(4) console。ブランチ例: `feature/etc-experiment-mode`。

## 3. 新マシンでの環境セットアップ

```bash
# PlatformIO (未導入)。公式インストーラ or pipx
pipx install platformio          # または https://platformio.org/install/cli
pio run -d dc-firmware -e teensy41   # 初回はツールチェイン DL に数分

# Node 22 + pnpm (未導入)。旧 PC では ~/.local/opt に tarball 展開 + corepack を使用
corepack enable && cd dc-console && pnpm install
pnpm run gen:proto     # proto 変更時のみ
pnpm dev --host 0.0.0.0   # ?mock 付き URL で実機なし動作確認
```

**pnpm の罠 (対処済みだが知っておくこと)**: pnpm v11 は `package.json` の
`pnpm.*` フィールドを読まない。`pnpm-workspace.yaml` に移設済み:
`allowBuilds: @bufbuild/buf` (gen:proto のバイナリ取得) と
`patchedDependencies: zimmerframe` (無いと `pnpm dev` が
"No exports main defined" で落ちる。`pnpm build` では発症しないので注意)。

## 4. 次のアクション (優先順)

1. **実機確認**: mock → 実機で実験モードの動作確認 (Web Serial は
   localhost/HTTPS のみ)。ストールガード閾値 (仮値: |u|>90% + 乖離>2% + 1s
   不動) の妥当性を見る
2. **ハード準備**: 物理ストッパー移設 (−15/115%)・移設前に TPS 0/100 校正・
   ダブルバレル機構の干渉確認 (spec §6)
3. **ベンチ同定実験**: 滞在 (~8 分) → リリース (~6 分) → ステップ×2 ゲイン
   セット (~14 分、A=(3.0,0.4,0)→`SetEtcPidCmd`→B=(6.0,0.4,0))。SD カード必須
   (無いと開始拒否)。SD ログ回収 → `tools/decode_log.py` で CSV 化
4. **PC フィット**: シミュレーション誤差最小化 (θ̈ の数値微分はしない)。
   静止/非静止の分類は記録窓内の分散で (spec Q3)。フィットスクリプトは未着手
5. **制御則選定**: PID+FF vs 速度制御カスケード (計画書 §7-7 の選定基準。
   分岐条件 = 速度推定器のノイズ床で内側帯域 50–100 Hz が取れるか)
6. **安全検証 (早期・FF 導入前)**: エンジン稼働中のスプリング閉じ復帰確認
   (負圧は常に開き方向なのでフェイルセーフの成立条件。計画書 §4)

未決の外部依存: Vbat の CAN メッセージ定義 (kart-can, 30 Hz 予定。ログの vbat
は定義まで 0) / モーターのデータシート値 (Kt, Ke, R, ギア比) の収集 /
電流換算式 (adc4, ~20 mV/A + 50 mV offset) の実測検証。全リストは計画書 §9。

## 5. 前提知識 (ドキュメント外のコンテキスト)

- **KART はチーム名** (車両はゴーカートではなくフォーミュラスタイル車両)。
  エンジン **2 気筒**、スロットルは**ダブルバレル** (バタフライではない)。
  **負圧によるトルクは常に開き方向** → スプリング閉じ復帰と競合 (上記 4-6)
- 実機は**摩擦帯支配** (低 duty でも 0↔100% を飛び切る = 開ループで中間に
  静的釣り合い点がない)。実験が閉ループ滞在方式なのはこのため
- **CLAUDE.md の古い記述に注意**: センサーサンプリングは 8 kHz ではなく
  **16 kHz** (`SENSOR_SAMPLING_RATE_US 62.5`、MA60 ≒ 3.75 ms 窓/遅れ ~1.9 ms)。
  TODO.md は存在しない。モードノブの対応は can_data.cpp が正
  (未選択=CALIB / First=NORMAL / Second=RESTRICTED / Third=MOTOR_OFF)
- **AI アシスタントと作業する場合**: このプロジェクトは「議論 → 仕様書 →
  明示的な GO → 実装」の順で進める方針 (要件が具体的でも実装開始の合図では
  ない)。旧 PC の Claude メモリにあった内容はこの資料に集約済み
