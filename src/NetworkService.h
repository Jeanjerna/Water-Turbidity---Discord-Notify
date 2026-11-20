#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "MonitorTypes.h"
namespace NetworkService {
bool begin();
void publish(const Monitor::Reading &reading);
bool command(Monitor::Command command);
bool status(Monitor::NetworkStatus &result);
} // namespace NetworkService
