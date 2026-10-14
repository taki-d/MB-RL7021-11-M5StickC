#include <Arduino.h>
#include <cassert>
#include <cstdio>
#define private public
#include "mb_rl7023_11.h"
#undef private

HardwareSerial Serial;

static String response(unsigned tid, const char *payload, const char *esv = "72") {
    char frame[256];
    snprintf(frame, sizeof(frame), "ERXUDP peer local 0E1A 0E1A mac 1 0 0018 1081%04X02880105FF01%s%s", tid, esv, payload);
    return frame;
}

int main() {
    mb_rl2023_11 meter(1, 0, 26, "test", "test");
    assert(!meter.is_connected());
    meter.state_ = mb_rl2023_11::State::Ready;
    meter.ipv6_addr_ = "FE80::1";
    assert(meter.request_instantaneous_power(true));
    assert(meter.is_connected() && !meter.is_ready());
    const auto tid = meter.transaction_id_;
    assert(meter.serial.bytes.size() == 16 && meter.serial.bytes[11] == 2);
    assert(meter.serial.bytes[12] == 0xE7 && meter.serial.bytes[14] == 0xE8);
    meter.handle_line(response(tid + 1, "02E70400000100E804001E0000"));
    assert(!meter.is_ready() && !meter.new_power_);
    meter.handle_line(response(tid, "02E70400000100E804001E"));
    assert(!meter.is_ready() && !meter.new_power_); // truncated second property
    meter.handle_line(response(tid, "02E70400000100E804001Z0000"));
    assert(!meter.is_ready() && !meter.new_power_); // malformed data is atomic
    meter.handle_line(response(tid, "02E70400000100E804001E0000"));
    int watts, r, t; bool rv, tv;
    assert(meter.is_ready() && meter.take_instantaneous_power(watts) && watts == 256);
    assert(meter.take_instantaneous_current(r, t, rv, tv) && r == 30 && t == 0 && rv && tv);
    assert(!meter.take_instantaneous_power(watts));
    assert(meter.request_instantaneous_power());
    assert(meter.transaction_id_ != tid && !meter.request_instantaneous_current());
    meter.handle_line(response(tid, "01E70400000001")); // stale response
    assert(!meter.is_ready() && !meter.new_power_);
    meter.handle_line(response(meter.transaction_id_, "01E70400000101"));
    assert(meter.take_instantaneous_power(watts) && watts == 257);
    assert(meter.request_instantaneous_power(true));
    meter.handle_line(response(meter.transaction_id_, "02E70400000102E800", "52"));
    assert(meter.is_ready() && !meter.can_combine_current());
    assert(!meter.request_instantaneous_power(true));
    assert(meter.take_instantaneous_power(watts) && watts == 258);
    assert(meter.request_instantaneous_current()); // separate fallback remains usable
    meter.handle_line(response(meter.transaction_id_, "01E804001F0000"));
    assert(meter.take_instantaneous_current(r, t, rv, tv) && r == 31);
    meter.combine_current_supported_ = true;
    assert(meter.request_instantaneous_power(true));
    test_clock() += 60001;
    meter.loop();
    assert(meter.is_ready() && !meter.can_combine_current());
    puts("meter host tests passed");
}
