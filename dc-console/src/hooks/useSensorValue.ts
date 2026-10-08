import { useEffect, useRef, useState } from "preact/hooks";
import { sensorStore } from "../sensor-store";
import type { SensorData } from "../types";

/**
 * sensorStore.latest を intervalMs ごとに読み、select した値を返す。未受信の間は initial。
 * - 一度受信した後は、latest が null になっても直前の値を保つ (従来の各部品の挙動と同じ)
 * - select の結果が前回と同じ (Object.is) なら再描画しない。プリミティブを選べば変化時だけ描画される
 */
export function useSensorValue<T>(select: (d: SensorData) => T, initial: T, intervalMs = 100): T {
  const [value, setValue] = useState<T>(initial);
  const selectRef = useRef(select);
  selectRef.current = select;
  useEffect(() => {
    const iv = setInterval(() => {
      const d = sensorStore.latest;
      if (d) setValue(selectRef.current(d));
    }, intervalMs);
    return () => clearInterval(iv);
  }, [intervalMs]);
  return value;
}

/** 最新の SensorData 全体 (未受信なら null)。フレームごとに新しいオブジェクトなので毎周期描画される */
export function useLatestSensor(intervalMs = 100): SensorData | null {
  return useSensorValue<SensorData | null>((d) => d, null, intervalMs);
}
