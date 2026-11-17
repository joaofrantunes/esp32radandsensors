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
