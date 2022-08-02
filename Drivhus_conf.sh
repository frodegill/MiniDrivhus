#!/bin/sh
SERVER=<server>
PORT=<port>
USERNAME=<username>
PASSWORD=<password>

mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/sec_between_reading"      -m "30" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/growlight_minutes_pr_day" -m "0" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/fan_activate_temp"        -m "40.0" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/fan_activate_humid"       -m "98.0" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant_count"              -m "3" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant0/enabled"                   -m "1" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant0/dry_value"                 -m "32.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant0/wet_value"                 -m "30.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant0/watering_duration_ms"      -m "30000" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant0/watering_grace_period_sec" -m "3600" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant1/enabled"                   -m "1" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant1/dry_value"                 -m "32.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant1/wet_value"                 -m "30.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant1/watering_duration_ms"      -m "30000" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant1/watering_grace_period_sec" -m "3600" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant2/enabled"                   -m "1" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant2/dry_value"                 -m "32.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant2/wet_value"                 -m "30.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant2/watering_duration_ms"      -m "30000" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor0/config/plant2/watering_grace_period_sec" -m "3600" &&

mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/sec_between_reading"      -m "30" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/growlight_minutes_pr_day" -m "0" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/fan_activate_temp"        -m "99.0" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/fan_activate_humid"       -m "99.0" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant_count"              -m "1" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant0/enabled"                   -m "1" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant0/dry_value"                 -m "32.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant0/wet_value"                 -m "30.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant0/watering_duration_ms"      -m "30000" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant0/watering_grace_period_sec" -m "3600" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant1/enabled"                   -m "0" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant1/dry_value"                 -m "32.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant1/wet_value"                 -m "30.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant1/watering_duration_ms"      -m "30000" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant1/watering_grace_period_sec" -m "3600" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant2/enabled"                   -m "0" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant2/dry_value"                 -m "32.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant2/wet_value"                 -m "30.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant2/watering_duration_ms"      -m "30000" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor1/config/plant2/watering_grace_period_sec" -m "3600" &&

mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/sec_between_reading"      -m "30" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/growlight_minutes_pr_day" -m "0" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/fan_activate_temp"        -m "99.0" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/fan_activate_humid"       -m "99.0" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant_count"              -m "3" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant0/enabled"                   -m "1" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant0/dry_value"                 -m "32.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant0/wet_value"                 -m "30.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant0/watering_duration_ms"      -m "30000" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant0/watering_grace_period_sec" -m "3600" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant1/enabled"                   -m "1" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant1/dry_value"                 -m "32.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant1/wet_value"                 -m "30.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant1/watering_duration_ms"      -m "30000" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant1/watering_grace_period_sec" -m "3600" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant2/enabled"                   -m "1" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant2/dry_value"                 -m "32.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant2/wet_value"                 -m "30.5" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant2/watering_duration_ms"      -m "30000" &&
mosquitto_pub -h $SERVER -p $PORT -u $USERNAME -P $PASSWORD -t "/Drivhus/Sensor2/config/plant2/watering_grace_period_sec" -m "3600"
