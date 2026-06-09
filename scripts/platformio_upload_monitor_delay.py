from SCons.Script import COMMAND_LINE_TARGETS

Import("env")


def delay_before_monitor(source, target, env):
    import time

    print("Waiting for ESP32-S3 USB CDC port to settle before monitor...")
    time.sleep(2.5)


targets = set(COMMAND_LINE_TARGETS)
if "upload" in targets and "monitor" in targets:
    env.AddPostAction("upload", delay_before_monitor)
