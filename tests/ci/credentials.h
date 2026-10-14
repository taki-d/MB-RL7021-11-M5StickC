#pragma once

#include <Arduino.h>

// Public, deliberately invalid credentials for compile-only CI builds.
// Never use this header to provision a device or replace local credentials.
String bid = "00000000000000000000000000000000";
String bpass = "000000000000";
const char *ssid = "ci-build-only";
const char *password = "not-a-real-password";
