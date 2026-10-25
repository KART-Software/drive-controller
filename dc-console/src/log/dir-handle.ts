// 開いたフォルダのハンドルを IndexedDB に覚えて、次回は 1 クリック (権限の再確認だけ) で開けるようにする
const DB = "kart-console";
const STORE = "log-viewer";
const KEY = "dir";

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB, 1);
    req.onupgradeneeded = () => req.result.createObjectStore(STORE);
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

function request<T>(r: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error);
  });
}

export async function saveDirHandle(h: FileSystemDirectoryHandle): Promise<void> {
  try {
    const db = await openDb();
    await request(db.transaction(STORE, "readwrite").objectStore(STORE).put(h, KEY));
  } catch { /* プライベートウィンドウなど。覚えないだけ */ }
}

export async function loadDirHandle(): Promise<FileSystemDirectoryHandle | null> {
  try {
    const db = await openDb();
    return (await request(db.transaction(STORE, "readonly").objectStore(STORE).get(KEY))) ?? null;
  } catch {
    return null;
  }
}

/** ユーザー操作 (クリック) の中で呼ぶこと: 権限が切れていれば確認ダイアログが出る */
export async function ensureReadPermission(h: FileSystemDirectoryHandle): Promise<boolean> {
  if ((await h.queryPermission({ mode: "read" })) === "granted") return true;
  return (await h.requestPermission({ mode: "read" })) === "granted";
}
