// Shadow of test/support/Arduino.h for the U3 serial-command twin.
//
// Not an edit of the shared shim (out of file set) -- a full replacement that
// the -I search order picks up first (test/test_serial_command_twin/stubs
// comes before test/support in both native_serial_esp32 and
// native_serial_nrf52). Content is copied from test/support/Arduino.h as of
// 2026-09-12 and extended with exactly what checkSerialCommand() needs and
// the shared shim does not offer: SerialStub::available()/read() plus an
// input-feed queue, since test/support/Arduino.h's SerialStub only ever
// modeled the *output* side (capture what firmware prints), never the input
// side (feed what a human types).
#pragma once

#ifndef NATIVE_BUILD
#error "test/test_serial_command_twin/stubs/Arduino.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// ---------------------------------------------------------------- Basistypen

typedef uint8_t byte;
typedef bool boolean;

#ifndef HIGH
#define HIGH 1
#define LOW 0
#endif

#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef F
#define F(x) (x)
#endif

// ------------------------------------------------------------------ Zeitgeber
inline uint32_t mc_test_clock_ms = 0;

inline unsigned long mc_test_millis(void) { return mc_test_clock_ms; }
inline void mc_test_set_millis(unsigned long ms) { mc_test_clock_ms = ms; }
inline void mc_test_advance_millis(unsigned long d) { mc_test_clock_ms += d; }

inline unsigned long millis(void) { return mc_test_millis(); }
inline unsigned long micros(void) { return mc_test_millis() * 1000UL; }
inline void delay(unsigned long) {}

// ------------------------------------------------------------------- Zufall

inline void randomSeed(unsigned long seed) { srand((unsigned int)seed); }

inline long random(long howbig)
{
    if (howbig <= 0)
        return 0;
    return rand() % howbig;
}

inline long random(long howsmall, long howbig)
{
    if (howsmall >= howbig)
        return howsmall;
    return random(howbig - howsmall) + howsmall;
}

// -------------------------------------------------------------------- String

class String
{
public:
    String() {}
    String(const char *s) : s_(s ? s : "") {}
    String(const std::string &s) : s_(s) {}
    explicit String(int v) : s_(std::to_string(v)) {}
    explicit String(unsigned int v) : s_(std::to_string(v)) {}
    explicit String(long v) : s_(std::to_string(v)) {}
    explicit String(char c) : s_(1, c) {}
    String(double v, unsigned char decimals)
    {
        char buf[33];
        snprintf(buf, sizeof(buf), "%.*f", (int)decimals, v);
        s_ = buf;
    }

    unsigned int length() const { return (unsigned int)s_.size(); }
    const char *c_str() const { return s_.c_str(); }
    bool isEmpty() const { return s_.empty(); }

    char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : '\0'; }
    char operator[](unsigned int i) const { return charAt(i); }

    int compareTo(const String &o) const { return s_.compare(o.s_); }
    bool equals(const String &o) const { return s_ == o.s_; }
    bool operator==(const String &o) const { return s_ == o.s_; }
    bool operator!=(const String &o) const { return s_ != o.s_; }
    bool operator<(const String &o) const { return s_ < o.s_; }

    String &operator+=(const String &o)
    {
        s_ += o.s_;
        return *this;
    }
    String &operator+=(const char *o)
    {
        s_ += (o ? o : "");
        return *this;
    }
    String &operator+=(char c)
    {
        s_ += c;
        return *this;
    }
    friend String operator+(String a, const String &b)
    {
        a += b;
        return a;
    }

    int indexOf(char c) const
    {
        auto p = s_.find(c);
        return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const String &n) const
    {
        auto p = s_.find(n.s_);
        return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const String &n, unsigned int from) const
    {
        auto p = s_.find(n.s_, from);
        return p == std::string::npos ? -1 : (int)p;
    }
    int lastIndexOf(char c) const
    {
        auto p = s_.rfind(c);
        return p == std::string::npos ? -1 : (int)p;
    }

    String substring(unsigned int from) const
    {
        return from >= s_.size() ? String() : String(s_.substr(from));
    }
    String substring(unsigned int from, unsigned int to) const
    {
        if (from > to)
        {
            unsigned int tmp = from;
            from = to;
            to = tmp;
        }
        if (from >= s_.size() || to <= from)
            return String();
        if (to > s_.size())
            to = (unsigned int)s_.size();
        return String(s_.substr(from, to - from));
    }

    void concat(const String &o) { s_ += o.s_; }
    void concat(const char *o) { s_ += (o ? o : ""); }
    void concat(char c) { s_ += c; }

    void replace(const String &from, const String &to)
    {
        if (from.s_.empty())
            return;
        std::string out;
        size_t pos = 0, hit;
        while ((hit = s_.find(from.s_, pos)) != std::string::npos)
        {
            out.append(s_, pos, hit - pos);
            out += to.s_;
            pos = hit + from.s_.size();
        }
        out.append(s_, pos, std::string::npos);
        s_.swap(out);
    }

    bool startsWith(const String &p) const { return s_.rfind(p.s_, 0) == 0; }
    bool endsWith(const String &p) const
    {
        return s_.size() >= p.s_.size() &&
               s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
    }

    void trim()
    {
        const char *ws = " \t\r\n";
        auto b = s_.find_first_not_of(ws);
        if (b == std::string::npos) { s_.clear(); return; }
        auto e = s_.find_last_not_of(ws);
        s_ = s_.substr(b, e - b + 1);
    }
    void toUpperCase()
    {
        for (auto &c : s_) c = (char)toupper((unsigned char)c);
    }
    void toLowerCase()
    {
        for (auto &c : s_) c = (char)tolower((unsigned char)c);
    }

    int toInt() const { return atoi(s_.c_str()); }
    double toDouble() const { return atof(s_.c_str()); }
    float toFloat() const { return (float)atof(s_.c_str()); }

    const std::string &std_str() const { return s_; }

private:
    std::string s_;
};

inline String operator+(const char *a, const String &b) { return String(a) + b; }

// -------------------------------------------------------------------- Serial
//
// Extended over test/support/Arduino.h: checkSerialCommand() reads bytes
// (available()/read()), which the shared shim never modeled. in_ is a plain
// FIFO the test feeds explicitly -- no line buffering, no auto-echo, so a
// test controls exactly what checkSerialCommand() sees on each call, byte by
// byte, the way Serial.available()/read() actually behaves.

class SerialStub
{
public:
    void begin(unsigned long) {}
    void print(const char *s) { out_ += (s ? s : ""); }
    void print(const String &s) { out_ += s.c_str(); }
    void println(const char *s = "")
    {
        out_ += (s ? s : "");
        out_ += "\n";
    }
    void println(const String &s)
    {
        out_ += s.c_str();
        out_ += "\n";
    }
    int printf(const char *fmt, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n > 0)
            out_ += buf;
        return n;
    }
    void flush() {}
    operator bool() const { return true; }

    // Testzugriff -- Ausgabe
    const std::string &captured() const { return out_; }
    void clear() { out_.clear(); }

    // Testzugriff -- Eingabe (neu gegenueber test/support/Arduino.h)
    int available() const { return (int)in_.size(); }
    int read()
    {
        if (in_.empty())
            return -1;
        int c = (unsigned char)in_.front();
        in_.erase(in_.begin());
        return c;
    }
    void feed(const char *s) { if (s) in_.append(s); }
    void feed(const void *data, size_t len)
    {
        in_.append(reinterpret_cast<const char *>(data), len);
    }
    void feedByte(char c) { in_.push_back(c); }
    void clearIn() { in_.clear(); }

private:
    std::string out_;
    std::string in_;
};

inline SerialStub Serial;

#include <IPAddress.h>
