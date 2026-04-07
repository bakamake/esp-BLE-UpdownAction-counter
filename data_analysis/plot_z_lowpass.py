from __future__ import annotations

import math
import re
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


ACCEL_LOG_PATTERN = re.compile(
    r"ACCEL:\s+ts_ms=(?P<ts_ms>\d+),x=(?P<x>-?\d+),y=(?P<y>-?\d+),z=(?P<z>-?\d+)"
)
ONE_G_LSB = 16384
LOWPASS_CUTOFF_HZ = 2.0


def resolve_log_path() -> Path:
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent
    candidates = [
        script_dir / "深蹲.log",
        project_root / "深蹲.log",
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate

    tried_paths = "\n".join(str(path) for path in candidates)
    raise FileNotFoundError(f"Could not find 深蹲.log. Tried:\n{tried_paths}")


def parse_accel_log(log_path: Path) -> list[dict[str, int]]:
    records: list[dict[str, int]] = []

    with log_path.open("r", encoding="utf-8") as log_file:
        for line in log_file:
            match = ACCEL_LOG_PATTERN.search(line)
            if not match:
                continue

            records.append(
                {
                    "ts_ms": int(match.group("ts_ms")),
                    "x": int(match.group("x")),
                    "y": int(match.group("y")),
                    "z": int(match.group("z")),
                }
            )

    return records


def build_dataframe(records: list[dict[str, int]]) -> pd.DataFrame:
    if not records:
        raise ValueError("No ACCEL samples found in log file.")

    dataframe = pd.DataFrame.from_records(records, columns=["ts_ms", "x", "y", "z"])
    dataframe["t_s"] = dataframe["ts_ms"] / 1000.0
    dataframe["dt_s"] = dataframe["ts_ms"].diff().fillna(0) / 1000.0
    return dataframe[["ts_ms", "t_s", "dt_s", "x", "y", "z"]]


def first_order_lowpass(samples: pd.Series, dt_seconds: float, cutoff_hz: float) -> pd.Series:
    if dt_seconds <= 0:
        raise ValueError("dt_seconds must be positive.")

    rc = 1.0 / (2.0 * math.pi * cutoff_hz)
    alpha = dt_seconds / (rc + dt_seconds)

    filtered = [float(samples.iloc[0])]
    for value in samples.iloc[1:]:
        filtered.append(filtered[-1] + alpha * (float(value) - filtered[-1]))

    return pd.Series(filtered, index=samples.index, dtype="float64")


def plot_z_waveform(dataframe: pd.DataFrame, output_path: Path, cutoff_hz: float) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)

    median_dt_s = dataframe["dt_s"][dataframe["dt_s"] > 0].median()
    dataframe["z_lowpass"] = first_order_lowpass(dataframe["z"], median_dt_s, cutoff_hz)

    y_limit = int(
        max(
            dataframe["z"].abs().max(),
            dataframe["z_lowpass"].abs().max(),
            ONE_G_LSB,
        )
        * 1.05
    )

    figure, axis = plt.subplots(figsize=(12, 6))
    axis.plot(dataframe["t_s"], dataframe["z"], color="#9bb3c9", linewidth=1.0, label="Raw Z")
    axis.plot(dataframe["t_s"], dataframe["z_lowpass"], color="#2e8b57", linewidth=2.0, label=f"Low-pass Z ({cutoff_hz:.1f} Hz)")
    axis.axhline(ONE_G_LSB, color="#666666", linestyle="--", linewidth=1, label="+1g")
    axis.axhline(-ONE_G_LSB, color="#999999", linestyle="--", linewidth=1, label="-1g")
    axis.set_ylim(-y_limit, y_limit)
    axis.set_title("IMU Z Axis Acceleration with First-Order Low-pass Filter")
    axis.set_xlabel("Time (s)")
    axis.set_ylabel("Raw Accel (LSB)")
    axis.grid(True, linestyle="--", alpha=0.35)
    axis.legend(loc="upper right")

    figure.tight_layout()
    figure.savefig(output_path, dpi=150)
    plt.show()


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    log_path = resolve_log_path()
    output_path = script_dir / "output" / "imu_z_lowpass.png"

    records = parse_accel_log(log_path)
    dataframe = build_dataframe(records)
    plot_z_waveform(dataframe, output_path, LOWPASS_CUTOFF_HZ)

    sample_rate_hz = 1.0 / dataframe["dt_s"][dataframe["dt_s"] > 0].median()
    print(f"Parsed {len(dataframe)} ACCEL samples from {log_path}")
    print(f"Estimated sample rate: {sample_rate_hz:.2f} Hz")
    print(f"Low-pass cutoff: {LOWPASS_CUTOFF_HZ:.2f} Hz")
    print(f"Saved plot to {output_path}")


if __name__ == "__main__":
    main()
