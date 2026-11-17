"""Host-only validation with simulated Arduino/FreeRTOS interfaces.

No real credentials, Arduino core, physical sensor or server is required.
These tests must never be interpreted as successful ESP32 toolchain builds.
"""
import concurrent.futures
import itertools
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

here = Path(__file__).resolve().parent
firmware = here.parent.parent / "firmware" / "ArduinoGeigerCounterMix"
sketch = firmware / "ArduinoGeigerCounterMix.ino"
core = ["ENABLE_GEIGER", "ENABLE_BMP280", "ENABLE_LCD", "ENABLE_LED"]
extra = ["ENABLE_AHT", "ENABLE_GPS", "ENABLE_NTP", "ENABLE_VOLTAGE",
         "ENABLE_MAG", "ENABLE_LORA", "ENABLE_HALL", "ENABLE_INTTEMP"]
cloud = ["ENABLE_IFTTT", "ENABLE_THINGSPEAK"]
all_flags = core + extra + cloud

def main():
    with tempfile.TemporaryDirectory(prefix="esp32-host-tests-") as temp:
        tmp = Path(temp)
        shutil.copyfile(firmware / "credentials.example.h", tmp / "credentials.h")
        base = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                "-Werror", "-I" + str(here / "mock"), "-I" + str(tmp),
                "-DCONFIG_IDF_TARGET_ESP32=1"]

        def flags(values):
            return ["-D" + key + "=" + str(value) for key, value in values.items()] + ["-DSTATUS_LED_PIN=25"]

        def test_core(bits):
            values = dict(zip(core, bits))
            values.update(dict.fromkeys(extra + cloud, 0))
            binary = tmp / ("core" + "".join(map(str, bits)))
            subprocess.run(base + flags(values) + [str(here / "test_config.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            list(executor.map(test_core, itertools.product([0, 1], repeat=4)))
        print("PASS: 16 hardware configurations", flush=True)

        patterns = [dict.fromkeys(all_flags, 0), dict.fromkeys(all_flags, 1)]
        patterns += [{**dict.fromkeys(all_flags, 0), item: 1} for item in all_flags]

        def check_syntax(case):
            version, values = case
            subprocess.run(base + ["-DESP_ARDUINO_VERSION_MAJOR=" + str(version)] +
                           flags(values) + ["-fsyntax-only", "-x", "c++", str(sketch)], check=True)

        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            list(executor.map(check_syntax, itertools.product([2, 3], patterns)))
        print("PASS: 32 isolated/combined mock syntax configurations", flush=True)

        geiger = tmp / "geigerstats"
        subprocess.run(base + [str(here / "test_geiger.cpp"), "-o", str(geiger)], check=True)
        subprocess.run([str(geiger)], check=True)
        subprocess.run(base + ["-DENABLE_DOSE_ESTIMATE=1", "-DENABLE_GEIGER=1",
                              "-fsyntax-only", "-x", "c++", str(sketch)], check=True)

        for version in (2, 3):
            binary = tmp / ("extras" + str(version))
            values = {**dict.fromkeys(extra, 1), **dict.fromkeys(cloud, 0)}
            subprocess.run(base + ["-DESP_ARDUINO_VERSION_MAJOR=" + str(version)] +
                           flags(values) + [str(here / "test_extra.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

        detection = tmp / "detection"
        subprocess.run(base + ["-DENABLE_BMP280=1", "-DENABLE_BME280=1", "-DENABLE_MAG=1",
                               str(here / "test_detection.cpp"), "-o", str(detection)], check=True)
        subprocess.run([str(detection)], check=True)
        calibration = tmp / "calibration"
        subprocess.run(base + ["-DENABLE_MAG=1", "-DMAG_OFFSET_X=10", "-DMAG_OFFSET_Y=10",
                               "-DMAG_OFFSET_Z=-3", "-DMAG_SCALE_X=2", "-DMAG_SCALE_Y=0.5",
                               "-DMAG_DECLINATION_DEG=10", str(here / "test_calibration.cpp"),
                               "-o", str(calibration)], check=True)
        subprocess.run([str(calibration)], check=True)
        for profile in (0, 1):
            subprocess.run(base + [f"-DSTATION_PROFILE={profile}", "-DENABLE_MAG_CAL_LOG=1",
                                  "-fsyntax-only", "-x", "c++", str(sketch)], check=True)

        negative = subprocess.run(base + ["-DENABLE_LED=1", "-DSTATUS_LED_PIN=22",
                                          "-fsyntax-only", "-x", "c++", str(sketch)],
                                  capture_output=True)
        assert negative.returncode and b"Pin conflict" in negative.stderr
        print("PASS: pin conflict deliberately rejected")

if __name__ == "__main__":
    main()
