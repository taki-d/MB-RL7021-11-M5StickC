#include "mb_rl7023_11.h"

#include <stdio.h>

namespace {
constexpr uint32_t kCommandTimeoutMs = 3000;
constexpr uint32_t kScanExtraTimeoutMs = 5000;
constexpr uint32_t kJoinTimeoutMs = 30000;
constexpr uint32_t kPowerTimeoutMs = 3000;
constexpr uint32_t kRetryDelayMs = 5000;
constexpr size_t kMaxLineLength = 256;
}

mb_rl2023_11::mb_rl2023_11(
    uint8_t channel, uint8_t tx_pin, uint8_t rx_pin, String bid, String bpass
)
    : serial(channel), bid_(bid), bpass_(bpass) {
    serial.begin(115200, SERIAL_8N1, rx_pin, tx_pin);
    serial.setTimeout(0);
}

bool mb_rl2023_11::reached(uint32_t now, uint32_t target) {
    return static_cast<int32_t>(now - target) >= 0;
}

void mb_rl2023_11::clear_input() {
    while (serial.available() > 0) {
        serial.read();
    }
    rx_line_.remove(0);
}

void mb_rl2023_11::begin() {
    clear_input();
    channel_.remove(0);
    pan_id_.remove(0);
    addr_.remove(0);
    ipv6_addr_.remove(0);
    new_power_ = false;
    scan_duration_ = 4;

    send_command("SKRESET", State::WaitReset, kCommandTimeoutMs);
}

void mb_rl2023_11::send_command(
    const String &command, State wait_state, uint32_t timeout_ms
) {
    serial.print(command);
    serial.print("\r\n");
    serial.flush();

    state_ = wait_state;
    deadline_ = millis() + timeout_ms;
}

void mb_rl2023_11::start_scan() {
    channel_.remove(0);
    pan_id_.remove(0);
    addr_.remove(0);

    String command = "SKSCAN 2 FFFFFFFF ";
    command += String(scan_duration_);
    command += " 0";
    send_command(command, State::WaitScan, (scan_duration_ * 1000UL) + kScanExtraTimeoutMs);
    status_ = "scanning";
}

void mb_rl2023_11::start_setup_retry(const char *reason) {
    DEBUG_PRINT("RL7023 setup: ");
    DEBUG_PRINTLN(reason);

    state_ = State::RetryWait;
    retry_at_ = millis() + kRetryDelayMs;
    deadline_ = 0;
    status_ = reason;
}

void mb_rl2023_11::handle_command_ok(State wait_state) {
    switch (wait_state) {
    case State::WaitReset:
        status_ = "reading module info";
        send_command("SKINFO", State::WaitInfo, kCommandTimeoutMs);
        break;

    case State::WaitInfo:
        status_ = "setting password";
        send_command("SKSETPWD C " + bpass_, State::WaitPassword, kCommandTimeoutMs);
        break;

    case State::WaitPassword:
        status_ = "setting route id";
        send_command("SKSETRBID " + bid_, State::WaitBid, kCommandTimeoutMs);
        break;

    case State::WaitBid:
        scan_duration_ = 4;
        start_scan();
        break;

    case State::WaitChannel:
        status_ = "setting pan id";
        send_command("SKSREG S3 " + pan_id_, State::WaitPanId, kCommandTimeoutMs);
        break;

    case State::WaitPanId:
        status_ = "converting address";
        send_command("SKLL64 " + addr_, State::WaitIpv6, kCommandTimeoutMs);
        break;

    default:
        break;
    }
}

void mb_rl2023_11::handle_scan_line(const String &line) {
    if (line.indexOf("Channel:") >= 0) {
        channel_ = line.substring(line.indexOf("Channel:") + 8);
        channel_.trim();
    } else if (line.indexOf("Pan ID:") >= 0) {
        pan_id_ = line.substring(line.indexOf("Pan ID:") + 7);
        pan_id_.trim();
    } else if (line.indexOf("Addr:") >= 0) {
        addr_ = line.substring(line.indexOf("Addr:") + 5);
        addr_.trim();
    }

    if (line.indexOf("FAIL") >= 0 || line.indexOf("EVENT 22") >= 0) {
        if (line.indexOf("EVENT 22") >= 0 && addr_.length() > 0 && addr_ != "NG") {
            status_ = "setting channel";
            send_command("SKSREG S2 " + channel_, State::WaitChannel, kCommandTimeoutMs);
            return;
        }

        if (scan_duration_ < 9) {
            ++scan_duration_;
            start_scan();
        } else {
            start_setup_retry("scan failed");
        }
    }
}

void mb_rl2023_11::handle_power_line(const String &line) {
    if (line.indexOf("ERXUDP") < 0) {
        return;
    }

    // The last field is the received ECHONET Lite frame. Using the last
    // eight hex digits preserves the behavior of the original implementation
    // while allowing variable-length SK protocol headers.
    int last_space = line.lastIndexOf(' ');
    if (last_space < 0) {
        return;
    }

    String frame = line.substring(last_space + 1);
    frame.trim();
    if (frame.length() < 8) {
        return;
    }

    String power_hex = frame.substring(frame.length() - 8);
    char *end = nullptr;
    unsigned long value = strtoul(power_hex.c_str(), &end, 16);
    if (end == power_hex.c_str() || *end != '\0' || value > 2147483647UL) {
        return;
    }

    instantaneous_power_ = static_cast<int>(value);
    new_power_ = true;
    state_ = State::Ready;
    deadline_ = 0;
    status_ = "ready";
    DEBUG_PRINT("instantaneous power: ");
    DEBUG_PRINTLN(instantaneous_power_);
}

void mb_rl2023_11::handle_line(String line) {
    line.trim();
    if (line.length() == 0) {
        return;
    }

    DEBUG_PRINTLN(line);

    if (state_ == State::WaitScan) {
        handle_scan_line(line);
        return;
    }

    if (state_ == State::WaitIpv6) {
        if (line.indexOf("FE80") >= 0 || line.indexOf("fe80") >= 0) {
            ipv6_addr_ = line;
            ipv6_addr_.trim();
            status_ = "joining network";
            send_command("SKJOIN " + ipv6_addr_, State::WaitJoin, kJoinTimeoutMs);
        } else if (line.indexOf("FAIL") >= 0) {
            start_setup_retry("address conversion failed");
        }
        return;
    }

    if (state_ == State::WaitJoin) {
        if (line.indexOf("EVENT 25") >= 0) {
            state_ = State::Ready;
            deadline_ = 0;
            status_ = "ready";
        } else if (line.indexOf("FAIL") >= 0) {
            start_setup_retry("join failed");
        }
        return;
    }

    if (state_ == State::WaitPower) {
        handle_power_line(line);
        return;
    }

    switch (state_) {
    case State::WaitReset:
    case State::WaitInfo:
    case State::WaitPassword:
    case State::WaitBid:
    case State::WaitChannel:
    case State::WaitPanId:
        if (line.indexOf("FAIL") >= 0) {
            start_setup_retry("module command failed");
        } else if (line.indexOf("OK") >= 0) {
            State completed_state = state_;
            handle_command_ok(completed_state);
        }
        break;

    default:
        break;
    }
}

void mb_rl2023_11::loop() {
    uint8_t lines_processed = 0;
    while (serial.available() > 0 && lines_processed < 32) {
        char c = static_cast<char>(serial.read());
        if (c == '\n') {
            handle_line(rx_line_);
            rx_line_.remove(0);
            ++lines_processed;
        } else if (c != '\r') {
            if (rx_line_.length() < kMaxLineLength) {
                rx_line_ += c;
            } else {
                rx_line_.remove(0);
            }
        }
    }

    uint32_t now = millis();
    if (state_ == State::RetryWait && reached(now, retry_at_)) {
        begin();
        return;
    }

    if (deadline_ != 0 && reached(now, deadline_)) {
        switch (state_) {
        case State::WaitScan:
            if (scan_duration_ < 9) {
                ++scan_duration_;
                start_scan();
            } else {
                start_setup_retry("scan timeout");
            }
            break;

        case State::WaitPower:
            state_ = State::Ready;
            deadline_ = 0;
            status_ = "power timeout";
            break;

        case State::WaitJoin:
            start_setup_retry("join timeout");
            break;

        case State::WaitReset:
        case State::WaitInfo:
        case State::WaitPassword:
        case State::WaitBid:
        case State::WaitChannel:
        case State::WaitPanId:
        case State::WaitIpv6:
            start_setup_retry("module response timeout");
            break;

        default:
            deadline_ = 0;
            break;
        }
    }
}

void mb_rl2023_11::send_power_request() {
    const uint8_t echonet_lite_frame[] = {
        0x10, 0x81, // EHD1, EHD2
        0x00, 0x01, // TID
        0x05, 0xFF, 0x01, // SEOJ
        0x02, 0x88, 0x01, // DEOJ
        0x62, // ESV: Get
        0x01, // OPC
        0xE7, // EPC: instantaneous power
        0x00, // PDC
    };

    serial.print("SKSENDTO 1 ");
    serial.print(ipv6_addr_);
    serial.print(" 0E1A 1 0 ");
    char length[5];
    snprintf(length, sizeof(length), "%04X", static_cast<unsigned int>(sizeof(echonet_lite_frame)));
    serial.print(length);
    serial.print(' ');
    serial.write(echonet_lite_frame, sizeof(echonet_lite_frame));
    serial.print("\r\n");
    serial.flush();
}

bool mb_rl2023_11::request_instantaneous_power() {
    if (state_ != State::Ready || ipv6_addr_.length() == 0) {
        return false;
    }

    new_power_ = false;
    send_power_request();
    state_ = State::WaitPower;
    deadline_ = millis() + kPowerTimeoutMs;
    status_ = "reading power";
    return true;
}

bool mb_rl2023_11::take_instantaneous_power(int &value) {
    if (!new_power_) {
        return false;
    }

    value = instantaneous_power_;
    new_power_ = false;
    return true;
}

bool mb_rl2023_11::is_ready() const {
    return state_ == State::Ready;
}

bool mb_rl2023_11::is_busy() const {
    return state_ != State::Ready && state_ != State::Idle && state_ != State::RetryWait;
}

const char *mb_rl2023_11::status() const {
    return status_;
}
