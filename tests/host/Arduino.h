#pragma once
// Minimal host adapter for exercising the real driver's parser/state machine.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

inline uint32_t &test_clock() { static uint32_t value = 0; return value; }
inline unsigned long millis() { return test_clock(); }
constexpr int SERIAL_8N1 = 0;

class String {
    std::string data_;
public:
    String() = default;
    String(const char *s) : data_(s) {}
    String(std::string s) : data_(std::move(s)) {}
    String(unsigned char value) : data_(std::to_string(value)) {}
    size_t length() const { return data_.size(); }
    const char *c_str() const { return data_.c_str(); }
    char charAt(size_t i) const { return i < data_.size() ? data_[i] : '\0'; }
    void remove(size_t at) { data_.erase(at); }
    void trim() {
        const auto first = data_.find_first_not_of(" \r\n\t");
        data_ = first == std::string::npos ? "" : data_.substr(first, data_.find_last_not_of(" \r\n\t") - first + 1);
    }
    int indexOf(const char *s) const {
        const auto p = data_.find(s); return p == std::string::npos ? -1 : static_cast<int>(p);
    }
    int lastIndexOf(char c) const {
        const auto p = data_.rfind(c); return p == std::string::npos ? -1 : static_cast<int>(p);
    }
    String substring(size_t first, size_t end = std::string::npos) const {
        return data_.substr(first, end == std::string::npos ? end : end - first);
    }
    bool startsWith(const char *s) const { return data_.rfind(s, 0) == 0; }
    bool equalsIgnoreCase(const char *s) const {
        std::string a = data_, b(s);
        for (auto &c : a) c = std::tolower(static_cast<unsigned char>(c));
        for (auto &c : b) c = std::tolower(static_cast<unsigned char>(c));
        return a == b;
    }
    bool operator!=(const char *s) const { return data_ != s; }
    String &operator+=(const String &s) { data_ += s.data_; return *this; }
    String &operator+=(char c) { data_ += c; return *this; }
    friend String operator+(const String &a, const String &b) { return a.data_ + b.data_; }
};

class HardwareSerial {
public:
    std::vector<uint8_t> bytes;
    explicit HardwareSerial(int = 0) {}
    void begin(int, int = 0, int = 0, int = 0) {}
    void setTimeout(int) {}
    int available() { return 0; }
    int read() { return -1; }
    template<class T> void print(const T &) {}
    template<class T> void println(const T &) {}
    template<class... T> void printf(const char *, T...) {}
    void write(uint8_t value) { bytes.push_back(value); }
    void write(const uint8_t *data, size_t size) { bytes.insert(bytes.end(), data, data + size); }
    void flush() {}
};
extern HardwareSerial Serial;
