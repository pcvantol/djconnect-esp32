from SCons.Script import COMMAND_LINE_TARGETS, SetOption

Import("env")


def wait_for_monitor_port(source, target, env):
    import time

    port = env.subst("$MONITOR_PORT") or env.subst("$UPLOAD_PORT")
    print("Waiting for ESP32-S3 USB CDC port to settle before monitor...")
    time.sleep(1.0)
    if not port:
        time.sleep(5.0)
        return

    try:
        import serial
    except Exception:
        time.sleep(6.0)
        return

    deadline = time.time() + 12.0
    last_error = None
    while time.time() < deadline:
        try:
            with serial.Serial(port=port, baudrate=int(env.subst("$MONITOR_SPEED") or 115200), timeout=0.05) as handle:
                handle.dtr = False
                handle.rts = False
            time.sleep(0.75)
            return
        except Exception as error:
            last_error = error
            time.sleep(0.35)

    print(f"Monitor port still busy after wait: {port}: {last_error}")


targets = set(COMMAND_LINE_TARGETS)
if "upload" in targets and "monitor" in targets:
    SetOption("num_jobs", 1)
    env.AddPreAction("monitor", wait_for_monitor_port)
