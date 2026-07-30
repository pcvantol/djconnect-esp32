Import("env")

# ESP8266Audio 2.4.1 configures the PDM data format member introduced for
# ESP-IDF I2S hardware v1. The pinned Arduino ESP32 3.1.1 / ESP-IDF 5.3
# ESP32-S3 framework uses I2S hardware v2, where that member does not exist.
# PDM output is not used by DJConnect; leave the framework's default format.
from pathlib import Path


source = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "ESP8266Audio" / "src" / "AudioOutputPDM.cpp"
if source.is_file():
    contents = source.read_text()
    original = "    pdm_cfg.slot_cfg.data_fmt = I2S_PDM_DATA_FMT_PCM;\n"
    replacement = "#if SOC_I2S_HW_VERSION_1\n    pdm_cfg.slot_cfg.data_fmt = I2S_PDM_DATA_FMT_PCM;\n#endif\n"
    if original in contents:
        source.write_text(contents.replace(original, replacement, 1))
