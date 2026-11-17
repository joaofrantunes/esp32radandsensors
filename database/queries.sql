-- Read-only examples: execute after schema.sql and sample collection.
USE nodered;

-- Radiation counts per hour (CPH), latest 48 observations
SELECT `time`, radiation FROM ESP32RADIATION
ORDER BY `time` DESC, id DESC LIMIT 48;

-- Average CPM per UTC hour for the last 24 hours
SELECT DATE_FORMAT(`time`, '%Y-%m-%d %H:00:00') AS utc_hour,
       AVG(cpm) AS avg_cpm, COUNT(*) AS samples
FROM ESP32CPM
WHERE `time` >= UTC_TIMESTAMP() - INTERVAL 24 HOUR
GROUP BY utc_hour ORDER BY utc_hour;

-- Latest temperature readings
SELECT `time`, temperature FROM ESP32TEMPERATURE
ORDER BY `time` DESC, id DESC LIMIT 60;

-- V1.6: exposure-weighted observed CPM and coverage per reception day.
-- MQTT gaps/duplicates and delayed receipt affect this summary; not a dose.
SELECT DATE(`time`) AS utc_day, SUM(`count`) AS recorded_pulses,
       SUM(elapsed_ms)/1000 AS recorded_seconds,
       60000.0*SUM(`count`)/NULLIF(SUM(elapsed_ms),0) AS weighted_cpm,
       MIN(cpm) AS min_interval_cpm, MAX(cpm) AS max_interval_cpm
FROM ESP32GEIGERSTATS
WHERE `time` >= UTC_TIMESTAMP() - INTERVAL 7 DAY
GROUP BY utc_day ORDER BY utc_day;

-- Distinguish projected CPH from completed rolling-window rate.
SELECT `time`, cph_est, estimate_ms, cph_rolling, window3600_ms
FROM ESP32GEIGERSTATS ORDER BY `time` DESC, id DESC LIMIT 240;

-- Latest device readings and other optional metrics (CSV export in SQL client).
SELECT `time`, rssi_dbm, heap_bytes, uptime_s FROM ESP32DEVICE
ORDER BY `time` DESC, id DESC LIMIT 60;
SELECT `time`, metric, value FROM ESP32EXTRA
WHERE `time` >= UTC_TIMESTAMP() - INTERVAL 1 DAY
ORDER BY `time`, id;
