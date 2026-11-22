"""Real AetherApp restarts using its standard filesystem object storage."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def run(exe, directory):
    observations = []
    for boot in range(4):
        command = [str(exe.resolve()), str(boot)]
        if boot == 1:
            command.append("--abrupt-after-save")
        result = subprocess.run(command, cwd=directory, capture_output=True,
                                text=True, timeout=90)
        (directory / f"boot-{boot}.log").write_text(
            result.stdout + result.stderr, encoding="utf-8")
        expected_logs = 400 if boot == 3 else (boot + 1) * 400
        checkpoint = f"CHECKPOINT {boot + 1} BUSINESS {boot + 1} TELEMETRY {expected_logs}"
        if result.returncode or checkpoint not in result.stdout:
            raise RuntimeError(
                f"boot {boot} failed ({result.returncode}): "
                f"{result.stdout[-6000:]} {result.stderr}")
        files = [path for path in (directory / "state").rglob("*") if path.is_file()]
        if not files:
            raise RuntimeError("ordinary object storage did not persist any files")
        observations.append(dict(boot=boot, writers=4, logs=expected_logs, business_counter=boot + 1,
                                 telemetry_schema_reset=boot == 3,
                                 object_files=len(files),
                                 object_bytes=sum(path.stat().st_size for path in files),
                                 abrupt_after_save=boot == 1))
        print(f"boot {boot}: standard object save/load, 4 writers, "
              f"{expected_logs} telemetry logs, business counter {boot + 1}, "
              f"schema reset={boot == 3}", flush=True)
    (directory / "report.json").write_text(
        json.dumps(observations, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, help="new directory for state, logs and report")
    args = parser.parse_args()
    if args.output_dir:
        directory = args.output_dir.resolve()
        directory.mkdir(parents=True, exist_ok=False)
        run(args.exe, directory)
    else:
        with tempfile.TemporaryDirectory(prefix="aether-tele-object-") as temporary:
            run(args.exe, Path(temporary))


if __name__ == "__main__":
    main()
