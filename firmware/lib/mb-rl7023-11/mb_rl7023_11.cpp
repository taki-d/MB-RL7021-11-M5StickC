#include "mb_rl7023_11.h"

#include <ctype.h>
#include <stdio.h>

namespace {
constexpr uint32_t kCommandTimeoutMs = 3000;
// RL7023-11 can report EPANDESC promptly but delay EVENT 22 until the
// active-scan procedure has fully completed.  A short timeout races that
// event, clears the collected address, and starts the next scan too early.
// Keep this close to the blocking implementation's generous wait window.
constexpr uint32_t kScanExtraTimeoutMs = 30000;
constexpr uint32_t kJoinTimeoutMs = 30000;
// AIF specifies at least 60 s when requesting multiple properties. A short
// timeout would discard legitimate delayed replies and overlap transactions.
constexpr uint32_t kMeterResponseTimeoutMs = 60000;
constexpr uint32_t kRetryDelayMs = 5000;
constexpr size_t kMaxLineLength = 256;

bool parse_hex_value(
    const String &text, size_t offset, size_t digits, uint32_t &value
) {
    if (digits == 0 || digits > 8 || offset + digits > text.length()) {
        return false;
    }

    char buffer[9] = {};
    for (size_t index = 0; index < digits; ++index) {
        const char character = text.charAt(offset + index);
        if (!isxdigit(static_cast<unsigned char>(character))) {
            return false;
        }
        buffer[index] = character;
    }

    char *end = nullptr;
    value = strtoul(buffer, &end, 16);
    return end != buffer && *end == '\0';
}
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
    new_current_ = false;
    new_cumulative_import_ = false;
    new_cumulative_export_ = false;
    energy_unit_valid_ = false;
    scan_duration_ = 4;
    request_kind_ = RequestKind::None;

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

void mb_rl2023_11::handle_meter_line(const String &line) {
    if (line.indexOf("ERXUDP") < 0) {
        return;
    }

    // The last field is the received ECHONET Lite frame. Parse its property
    // list instead of relying on a fixed position in the SK protocol line.
    int last_space = line.lastIndexOf(' ');
    if (last_space < 0) {
        return;
    }

    String frame = line.substring(last_space + 1);
    frame.trim();
    // ECHONET Lite response layout (hex string):
    //   EHD(4) TID(4) SEOJ(6) DEOJ(6) ESV(2) OPC(2)
    //   EPC(2) PDC(2) EDT(...)
    // During PANA join the module may still emit unrelated ERXUDP packets,
    // so only accept a Get_Res response for the request currently pending.
    uint32_t value = 0;
    uint32_t transaction_id = 0;
    uint32_t operation = 0;
    uint32_t property_count = 0;
    if (frame.length() < 24 ||
        !frame.substring(0, 4).equalsIgnoreCase("1081") ||
        !frame.substring(8, 20).equalsIgnoreCase("02880105FF01") ||
        !parse_hex_value(frame, 4, 4, transaction_id) ||
        transaction_id != transaction_id_ ||
        !parse_hex_value(frame, 20, 2, operation) ||
        (operation != 0x72 && operation != 0x52) ||
        !parse_hex_value(frame, 22, 2, property_count) ||
        property_count == 0 || property_count > 7) {
        return;
    }

    // Validate every property's boundary before changing cached samples.
    size_t offset = 24;
    for (uint32_t index = 0; index < property_count; ++index) {
        uint32_t property = 0;
        uint32_t size = 0;
        if (!parse_hex_value(frame, offset, 2, property) ||
            !parse_hex_value(frame, offset + 2, 2, size) ||
            offset + 4 + size * 2 > frame.length()) {
            return;
        }
        for (size_t digit = offset + 4; digit < offset + 4 + size * 2; ++digit) {
            if (!isxdigit(static_cast<unsigned char>(frame.charAt(digit)))) {
                return;
            }
        }
        offset += 4 + size * 2;
    }
    if (offset != frame.length()) {
        return;
    }

    const bool combined = request_kind_ == RequestKind::InstantaneousPowerAndCurrent;
    offset = 24;
    bool matched_request = false;
    bool matched_power = false;
    bool matched_current = false;
    for (uint32_t index = 0; index < property_count; ++index) {
        uint32_t property = 0;
        uint32_t data_size = 0;
        if (!parse_hex_value(frame, offset, 2, property) ||
            !parse_hex_value(frame, offset + 2, 2, data_size)) {
            return;
        }
        offset += 4;

        const size_t data_digits = data_size * 2;
        if (data_size > 128 || offset + data_digits > frame.length()) {
            return;
        }

        if (property == 0xE7 && data_size == 4 &&
            (request_kind_ == RequestKind::InstantaneousPower || combined) &&
            parse_hex_value(frame, offset, 8, value) && value <= 2147483647UL) {
            instantaneous_power_ = static_cast<int>(value);
            new_power_ = true;
            matched_power = true;
            matched_request = true;
        } else if (property == 0xE8 && data_size == 4 &&
                   (request_kind_ == RequestKind::InstantaneousCurrent || combined) &&
                   parse_hex_value(frame, offset, 8, value)) {
            const uint16_t r_phase = static_cast<uint16_t>(value >> 16);
            const uint16_t t_phase = static_cast<uint16_t>(value & 0xFFFF);

            // 0x7FFE/0x7FFF and 0x8000 are overflow/underflow or
            // unavailable values in the ECHONET Lite current representation.
            current_r_phase_valid_ = r_phase != 0x7FFE && r_phase != 0x7FFF &&
                                     r_phase != 0x8000;
            current_t_phase_valid_ = t_phase != 0x7FFE && t_phase != 0x7FFF &&
                                     t_phase != 0x8000;
            current_r_phase_deciamps_ = static_cast<int16_t>(r_phase);
            current_t_phase_deciamps_ = static_cast<int16_t>(t_phase);
            new_current_ = true;
            matched_current = true;
            matched_request = true;
        } else if (property == 0xE0 && data_size == 4 &&
                   request_kind_ == RequestKind::CumulativeEnergyImport &&
                   parse_hex_value(frame, offset, 8, value) &&
                   value != 0xFFFFFFFEUL) {
            cumulative_forward_raw_ = value;
            new_cumulative_import_ = true;
            matched_request = true;
        } else if (property == 0xE3 && data_size == 4 &&
                   request_kind_ == RequestKind::CumulativeEnergyExport &&
                   parse_hex_value(frame, offset, 8, value)) {
            if (value != 0xFFFFFFFEUL) {
                cumulative_reverse_raw_ = value;
                new_cumulative_export_ = true;
            }
            // 0xFFFFFFFE explicitly means that reverse measurement is not
            // available; the response still completed the request.
            matched_request = true;
        } else if (property == 0xE1 && data_size == 1 &&
                   request_kind_ == RequestKind::EnergyUnit &&
                   parse_hex_value(frame, offset, 2, value)) {
            energy_unit_code_ = static_cast<uint8_t>(value);
            energy_unit_valid_ = true;
            matched_request = true;
        }

        offset += data_digits;
    }

    if (combined && (operation == 0x52 || !matched_power || !matched_current)) {
        combine_current_supported_ = false;
        DEBUG_PRINTF("METER fallback t=%lu tid=%u epc=E7\n", millis(), transaction_id_);
    }
    if (!matched_request && operation != 0x52) {
        return;
    }

    DEBUG_PRINTF("METER response t=%lu tid=%u epc=%02X elapsed=%lu loop_gap=%lu\n",
                 millis(), transaction_id_, requested_epc_,
                 millis() - request_started_at_, max_loop_gap_ms_);
    state_ = State::Ready;
    request_kind_ = RequestKind::None;
    deadline_ = 0;
    status_ = "ready";
}

void mb_rl2023_11::handle_line(String line) {
    line.trim();
    if (line.length() == 0) {
        return;
    }

    // UART echoes and PANA payloads may contain credentials. Timing traces
    // below deliberately contain only transaction IDs, properties and times.

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
        if (line.startsWith("EVENT 21 ")) {
            const String result = line.substring(line.lastIndexOf(' ') + 1);
            DEBUG_PRINTF("METER tx t=%lu tid=%u epc=%02X elapsed=%lu status=%s\n",
                         millis(), transaction_id_, requested_epc_,
                         millis() - request_started_at_, result.c_str());
        }
        handle_meter_line(line);
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
    const uint32_t loop_now = millis();
    if (last_loop_at_ != 0 && loop_now - last_loop_at_ > max_loop_gap_ms_) {
        max_loop_gap_ms_ = loop_now - last_loop_at_;
    }
    last_loop_at_ = loop_now;
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
            if (request_kind_ == RequestKind::InstantaneousPowerAndCurrent) {
                combine_current_supported_ = false;
            }
            DEBUG_PRINTF("METER timeout t=%lu tid=%u epc=%02X elapsed=%lu\n",
                         now, transaction_id_, requested_epc_,
                         now - request_started_at_);
            state_ = State::Ready;
            request_kind_ = RequestKind::None;
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

void mb_rl2023_11::send_meter_request(
    const uint8_t *properties, size_t property_count,
    RequestKind request_kind
) {
    ++transaction_id_;
    const uint8_t header[] = {
        0x10, 0x81, // EHD1, EHD2
        static_cast<uint8_t>(transaction_id_ >> 8),
        static_cast<uint8_t>(transaction_id_), // unique TID for each request
        0x05, 0xFF, 0x01, // SEOJ: controller
        0x02, 0x88, 0x01, // DEOJ: low-voltage smart meter
        0x62, // ESV: Get
        static_cast<uint8_t>(property_count), // OPC
    };

    const size_t frame_length = sizeof(header) + (property_count * 2);
    serial.print("SKSENDTO 1 ");
    serial.print(ipv6_addr_);
    serial.print(" 0E1A 1 0 ");
    char length[5];
    snprintf(length, sizeof(length), "%04X", static_cast<unsigned int>(frame_length));
    serial.print(length);
    serial.print(' ');
    serial.write(header, sizeof(header));
    for (size_t index = 0; index < property_count; ++index) {
        serial.write(properties[index]);
        serial.write(static_cast<uint8_t>(0x00)); // PDC for a Get request
    }
    serial.print("\r\n");
    serial.flush();

    request_kind_ = request_kind;
    requested_epc_ = properties[0];
    request_started_at_ = millis();
    max_loop_gap_ms_ = 0;
    DEBUG_PRINTF("METER request t=%lu tid=%u epc=%02X opc=%u\n",
                 request_started_at_, transaction_id_, requested_epc_,
                 static_cast<unsigned int>(property_count));
    state_ = State::WaitPower;
    deadline_ = millis() + kMeterResponseTimeoutMs;
}

bool mb_rl2023_11::request_instantaneous_power(bool include_current) {
    if (state_ != State::Ready || ipv6_addr_.length() == 0) {
        return false;
    }

    if (include_current && !combine_current_supported_) {
        return false;
    }
    new_power_ = false;
    const uint8_t properties[] = {0xE7, 0xE8};
    if (include_current) {
        new_current_ = false;
    }
    send_meter_request(properties, include_current ? 2 : 1,
                       include_current ? RequestKind::InstantaneousPowerAndCurrent :
                                         RequestKind::InstantaneousPower);
    status_ = "reading power";
    return true;
}

bool mb_rl2023_11::request_instantaneous_current() {
    if (state_ != State::Ready || ipv6_addr_.length() == 0) {
        return false;
    }

    new_current_ = false;
    const uint8_t properties[] = {0xE8};
    send_meter_request(properties, sizeof(properties), RequestKind::InstantaneousCurrent);
    status_ = "reading current";
    return true;
}

bool mb_rl2023_11::request_cumulative_energy_import() {
    if (state_ != State::Ready || ipv6_addr_.length() == 0) {
        return false;
    }

    new_cumulative_import_ = false;
    const uint8_t properties[] = {0xE0};
    send_meter_request(properties, sizeof(properties), RequestKind::CumulativeEnergyImport);
    status_ = "reading import energy";
    return true;
}

bool mb_rl2023_11::request_cumulative_energy_export() {
    if (state_ != State::Ready || ipv6_addr_.length() == 0) {
        return false;
    }

    new_cumulative_export_ = false;
    const uint8_t properties[] = {0xE3};
    send_meter_request(properties, sizeof(properties), RequestKind::CumulativeEnergyExport);
    status_ = "reading export energy";
    return true;
}

bool mb_rl2023_11::request_energy_unit() {
    if (state_ != State::Ready || ipv6_addr_.length() == 0) {
        return false;
    }

    const uint8_t properties[] = {0xE1};
    send_meter_request(properties, sizeof(properties), RequestKind::EnergyUnit);
    status_ = "reading energy unit";
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

bool mb_rl2023_11::take_instantaneous_current(
    int &r_phase_deciamps, int &t_phase_deciamps,
    bool &r_phase_valid, bool &t_phase_valid
) {
    if (!new_current_) {
        return false;
    }

    r_phase_deciamps = current_r_phase_deciamps_;
    t_phase_deciamps = current_t_phase_deciamps_;
    r_phase_valid = current_r_phase_valid_;
    t_phase_valid = current_t_phase_valid_;
    new_current_ = false;
    return true;
}

bool mb_rl2023_11::take_cumulative_energy_import(uint32_t &value) {
    if (!new_cumulative_import_) {
        return false;
    }

    value = cumulative_forward_raw_;
    new_cumulative_import_ = false;
    return true;
}

bool mb_rl2023_11::take_cumulative_energy_export(uint32_t &value) {
    if (!new_cumulative_export_) {
        return false;
    }

    value = cumulative_reverse_raw_;
    new_cumulative_export_ = false;
    return true;
}

bool mb_rl2023_11::is_ready() const {
    return state_ == State::Ready;
}

bool mb_rl2023_11::is_connected() const {
    return state_ == State::Ready || state_ == State::WaitPower;
}

bool mb_rl2023_11::is_busy() const {
    return state_ != State::Ready && state_ != State::Idle && state_ != State::RetryWait;
}

const char *mb_rl2023_11::status() const {
    return status_;
}

bool mb_rl2023_11::has_energy_unit() const {
    return energy_unit_valid_;
}

uint8_t mb_rl2023_11::energy_unit_code() const {
    return energy_unit_code_;
}

float mb_rl2023_11::energy_unit_kwh() const {
    switch (energy_unit_code_) {
    case 0x00:
        return 1.0f;
    case 0x01:
        return 0.1f;
    case 0x02:
        return 0.01f;
    case 0x03:
        return 0.001f;
    case 0x04:
        return 0.0001f;
    case 0x0A:
        return 10.0f;
    case 0x0B:
        return 100.0f;
    case 0x0C:
        return 1000.0f;
    case 0x0D:
        return 10000.0f;
    default:
        return 0.0f;
    }
}
