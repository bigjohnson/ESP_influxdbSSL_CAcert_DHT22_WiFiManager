# ESP_influxdbSSL_CAcert_DHT22
An arduino sketch to upload DHT22 humidity and temperature on an influxdb database with SSL and Root CA cert

On a fresh esp8266 you must upload the config.json on the SPIFF before the sketch, or the sketch will not work.
An alternative method is comment the read of the file on the first run, then reenable it.
