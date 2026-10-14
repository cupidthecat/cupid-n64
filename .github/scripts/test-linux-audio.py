import os
from pathlib import Path
import selectors
import struct
import subprocess
import sys
import tempfile
import time


def stop(process):
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def recovery(binary, env):
    module = subprocess.check_output(
        ["pactl", "load-module", "module-null-sink", "sink_name=cupid_preferred",
         "format=float32le", "rate=48000", "channels=2",
         "sink_properties=device.description=CupidTestPreferred"], env=env, text=True, timeout=3,
    ).strip()
    subprocess.run(["pactl", "set-default-sink", "cupid_test"], env=env, check=True, timeout=3)
    process = subprocess.Popen(
        [binary, "--audio-recovery"], env=env, text=True, stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, bufsize=1,
    )
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    try:
        for phase in ("remove", "restore", "default"):
            if not selector.select(timeout=10):
                raise RuntimeError(f"Audio recovery did not reach {phase}.")
            line = process.stdout.readline().strip()
            if line != phase:
                raise RuntimeError(f"Audio recovery expected {phase}, received {line!r}.")
            if phase == "remove":
                subprocess.run(["pactl", "unload-module", module], env=env, check=True, timeout=3)
                sinks = subprocess.check_output(["pactl", "list", "short", "sinks"], env=env,
                                                text=True, timeout=3)
                if "cupid_preferred" in sinks:
                    raise RuntimeError("The host did not remove the preferred playback sink.")
            elif phase == "restore":
                module = subprocess.check_output(
                    ["pactl", "load-module", "module-null-sink", "sink_name=cupid_preferred",
                     "format=float32le", "rate=48000", "channels=2",
                     "sink_properties=device.description=CupidTestPreferred"],
                    env=env, text=True, timeout=3,
                ).strip()
            else:
                subprocess.run(["pactl", "set-default-sink", "cupid_preferred"], env=env,
                               check=True, timeout=3)
            process.stdin.write(phase + "\n")
            process.stdin.flush()
        process.wait(timeout=10)
        print(process.stdout.read(), end="")
        if process.returncode:
            raise RuntimeError("Audio endpoint recovery failed.")
    finally:
        selector.close()
        stop(process)


def main():
    with tempfile.TemporaryDirectory(prefix="cupid-audio-") as temporary:
        root = Path(temporary)
        runtime = root / "runtime"
        runtime.mkdir(mode=0o700)
        socket = runtime / "native"
        config = root / "server.pa"
        config.write_text(
            f"load-module module-native-protocol-unix socket={socket} auth-anonymous=1\n"
            "load-module module-null-sink sink_name=cupid_test format=float32le "
            "rate=48000 channels=2 sink_properties=device.description=CupidTestDefault\n"
            "set-default-sink cupid_test\n"
            "set-default-source cupid_test.monitor\n"
        )
        env = dict(
            os.environ,
            XDG_RUNTIME_DIR=str(runtime),
            PULSE_RUNTIME_PATH=str(runtime),
            PULSE_STATE_PATH=str(root / "state"),
            PULSE_SERVER=f"unix:{socket}",
            SDL_AUDIODRIVER="pulseaudio",
        )
        with (root / "server.log").open("wb") as log:
            server = subprocess.Popen(
                ["pulseaudio", "--daemonize=no", "--exit-idle-time=-1", "--use-pid-file=no",
                 "--log-target=stderr", "-nF", str(config)],
                env=env, stdout=log, stderr=log,
            )
            recorder = None
            try:
                for _ in range(100):
                    if server.poll() is not None:
                        raise RuntimeError((root / "server.log").read_text())
                    if socket.exists():
                        ready = subprocess.run(
                            ["pactl", "list", "short", "sinks"], env=env,
                            capture_output=True, text=True, timeout=3,
                        )
                        if ready.returncode == 0 and "cupid_test" in ready.stdout:
                            break
                    time.sleep(0.05)
                else:
                    raise RuntimeError("The playback test server did not start.")
                with (root / "output.pcm").open("wb") as capture:
                    recorder = subprocess.Popen(
                        ["parec", "--raw", "--format=float32le", "--rate=48000", "--channels=2",
                         "--device=cupid_test.monitor"],
                        env=env, stdout=capture, stderr=log,
                    )
                    subprocess.run([sys.argv[1], "--audio"], env=env, check=True, timeout=15)
                    time.sleep(0.1)
                    stop(recorder)
                data = (root / "output.pcm").read_bytes()
                nonzero = sum(byte != 0 for byte in data)
                if len(data) < 48000 or nonzero < 1000:
                    raise RuntimeError("Audio did not reach the playback sink.")
                frames = struct.iter_unpack("<ff", data[:len(data) - len(data) % 8])
                precise = sum(5e-9 < left < 1.5e-8 and -1.5e-8 < right < -5e-9
                              for left, right in frames)
                if precise < 1000:
                    raise RuntimeError("The playback stream lost samples below PCM16 precision.")
                print(f"Playback sink received {len(data)} float PCM bytes "
                      f"({precise} frames below PCM16 precision).")
                recovery(sys.argv[1], env)
            finally:
                if recorder is not None:
                    stop(recorder)
                stop(server)


if __name__ == "__main__":
    main()
