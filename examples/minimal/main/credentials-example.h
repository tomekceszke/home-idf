#pragma once

#define WIFI_SSID                   ""
#define WIFI_PASS                   ""
/* Authorization header for the /admin endpoints (scripts). Empty locks them. */
#define HEADER_AUTHORIZATION_VALUE  ""
/* tools/hash_password.py output. Empty disables web login. */
#define AUTH_PASSWORD_ITERATIONS    20000
#define AUTH_PASSWORD_SALT_HEX      ""
#define AUTH_PASSWORD_HASH_HEX      ""
/* ntfy.sh topics, empty disables */
#define NTFY_TOPIC                  ""
#define NTFY_ERROR_TOPIC            ""
