import type { ComponentChildren } from "preact";

// アプリ内の確認ダイアログ (ブラウザの window.confirm の代わり)。背景クリックとキャンセルで閉じる。
// 見た目は既存の FS フル警告 (App.tsx) と同じ。危険な操作は danger=true で確定ボタンを赤にする
export function ConfirmDialog(props: {
  open: boolean;
  title: string;
  children: ComponentChildren;
  confirmLabel: string;
  cancelLabel?: string;
  danger?: boolean;
  confirmDisabled?: boolean;
  onConfirm: () => void;
  onCancel: () => void;
}) {
  if (!props.open) return null;
  return (
    <div class="dialog-backdrop" onClick={props.onCancel}>
      <div class="dialog" onClick={(e) => e.stopPropagation()}>
        <h2 class={props.danger ? "dialog-title danger" : "dialog-title"}>{props.title}</h2>
        <div class="dialog-body">{props.children}</div>
        <div class="dialog-actions">
          <button onClick={props.onCancel}>{props.cancelLabel ?? "キャンセル"}</button>
          <button class={props.danger ? "danger" : ""} disabled={props.confirmDisabled} onClick={props.onConfirm}>
            {props.confirmLabel}
          </button>
        </div>
      </div>
    </div>
  );
}
