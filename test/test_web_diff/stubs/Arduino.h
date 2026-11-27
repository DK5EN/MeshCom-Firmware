// Arduino shim of the web differential rig (FORMAL-02, web server wave A step 0).
//
// Replaces test/support/Arduino.h for this suite only (-I order, like test/test_serial_command_twin/stubs).
// Why not the shared shim: its String is a std::string wrapper that differs from the Arduino String the
// request path runs on in exactly the places the web handlers depend on:
//   - indexOf / lastIndexOf / startsWith work on the C string (stop at the first NUL byte), as WString.cpp does
//   - toInt() is atol(), the shared shim uses atoi()
//   - lastIndexOf(const String &) exists
//   - indexOf(x, from) returns -1 for from >= length, substring() swaps and clamps like WString.cpp
// The clock is the rig's fake clock (web_rig.h), yield() and delay() do not advance it.
#pragma once

#ifndef NATIVE_BUILD
#error "test/test_web_diff/stubs/Arduino.h is for the native test build only"
#endif

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

typedef uint8_t byte;
typedef bool boolean;

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef F
#define F(x) (x)
#endif

// ---- fake clock (state lives in web_rig.cpp)
unsigned long webRigMillis();
inline unsigned long millis(void) { return webRigMillis(); }
inline unsigned long micros(void) { return webRigMillis() * 1000UL; }
inline void yield(void) {}
inline void delay(unsigned long) {}

// ---- String: the subset of Arduino's WString the request path uses, with WString semantics
//
// Leak probe (rig only): a real Arduino String keeps its heap buffer when it is shortened or set to "" (copy() into the
// existing buffer, only the terminating NUL is written), so the old bytes stay in the buffer until something overwrites
// them; only a byte-wise overwrite (rm_wipe_string) removes them. resid_ models those bytes past the end, and
// bufferImage() returns buffer content + resid_ (what a heap dump of the buffer would show). stringDtorProbe is called with
// that image when a String dies (the buffer is freed unwiped on the device), but only for a String cut out of another one
// (substring(), and what is copied or moved from it): a temporary built from a literal that happens to equal the request
// line (wg_path == "/?logout") is not a copy of the request. A move steals the buffer and leaves an empty String behind, as
// WString's move does, so a temporary does not count as a second copy.
inline void (*stringDtorProbe)(const std::string &image) = nullptr;

class String
{
public:
    String() {}
    String(const char *s) : s_(s ? s : "") {}
    String(const std::string &s) : s_(s) {}
    String(const String &o) : s_(o.s_), cut_(o.cut_) {}
    String(String &&o) noexcept : s_(std::move(o.s_)), resid_(std::move(o.resid_)), cut_(o.cut_)
    {
        o.s_.clear();
        o.resid_.clear();
    }
    ~String()
    {
        if (cut_ && stringDtorProbe)
            stringDtorProbe(s_ + resid_);
    }
    String &operator=(const String &o)
    {
        if (this != &o)
        {
            assign_(o.s_);
            cut_ = cut_ || o.cut_;
        }
        return *this;
    }
    String &operator=(String &&o) noexcept
    {
        if (this != &o)
        {
            s_ = std::move(o.s_);
            resid_ = std::move(o.resid_);
            cut_ = o.cut_;
            o.s_.clear();
            o.resid_.clear();
        }
        return *this;
    }
    String &operator=(const char *s)
    {
        assign_(s ? s : "");
        return *this;
    }
    explicit String(char c) : s_(1, c) {}
    explicit String(int v) : s_(std::to_string(v)) {}
    explicit String(unsigned int v) : s_(std::to_string(v)) {}
    explicit String(long v) : s_(std::to_string(v)) {}
    explicit String(unsigned long v) : s_(std::to_string(v)) {}

    unsigned int length() const { return (unsigned int)s_.size(); }
    const char *c_str() const { return s_.c_str(); }
    void reserve(unsigned int n) { s_.reserve(n); }
    char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : '\0'; }
    char operator[](unsigned int i) const { return charAt(i); }
    void setCharAt(unsigned int i, char c)
    {
        if (i < s_.size())
            s_[i] = c;
    }

    bool equals(const String &o) const { return s_ == o.s_; }
    bool equals(const char *o) const { return o ? s_ == o : s_.empty(); }
    bool operator==(const String &o) const { return s_ == o.s_; }
    bool operator==(const char *o) const { return equals(o); }
    bool operator!=(const String &o) const { return s_ != o.s_; }
    bool operator!=(const char *o) const { return !equals(o); }
    int compareTo(const String &o) const { return strcmp(c_str(), o.c_str()); }
    bool equalsIgnoreCase(const String &o) const
    {
        if (s_.size() != o.s_.size())
            return false;
        for (size_t i = 0; i < s_.size(); i++)
            if (tolower((unsigned char)s_[i]) != tolower((unsigned char)o.s_[i]))
                return false;
        return true;
    }

    String &operator+=(const String &o)
    {
        const std::string add = o.s_; // o may be *this
        append_(add);
        return *this;
    }
    String &operator+=(const char *o)
    {
        append_(o ? o : "");
        return *this;
    }
    String &operator+=(char c)
    {
        append_(std::string(1, c));
        return *this;
    }
    friend String operator+(const String &a, const String &b)
    {
        String r(a);
        r.s_ += b.s_;
        return r;
    }

    // C-string semantics: a NUL byte inside the String ends the search, as in WString.cpp (strchr/strstr)
    int indexOf(char c) const { return indexOf(c, 0); }
    int indexOf(char c, unsigned int from) const
    {
        if (from >= s_.size())
            return -1;
        const char *p = strchr(c_str() + from, c);
        return p ? (int)(p - c_str()) : -1;
    }
    int indexOf(const String &n) const { return indexOf(n, 0); }
    int indexOf(const String &n, unsigned int from) const
    {
        if (from >= s_.size())
            return -1;
        const char *p = strstr(c_str() + from, n.c_str());
        return p ? (int)(p - c_str()) : -1;
    }
    int lastIndexOf(char c) const
    {
        const char *p = strrchr(c_str(), c);
        return p ? (int)(p - c_str()) : -1;
    }
    int lastIndexOf(const String &n) const
    {
        if (n.s_.size() > s_.size())
            return -1;
        return lastIndexOf(n, (unsigned int)(s_.size() - n.s_.size()));
    }
    int lastIndexOf(const String &n, unsigned int from) const
    {
        if (n.s_.empty() || s_.empty() || n.s_.size() > s_.size())
            return -1;
        if (from >= s_.size())
            from = (unsigned int)s_.size() - 1;
        int found = -1;
        for (const char *p = c_str(); p <= c_str() + from; p++)
        {
            p = strstr(p, n.c_str());
            if (!p)
                break;
            if ((unsigned int)(p - c_str()) <= from)
                found = (int)(p - c_str());
        }
        return found;
    }

    String substring(unsigned int left) const { return substring(left, (unsigned int)s_.size()); }
    String substring(unsigned int left, unsigned int right) const
    {
        if (left > right)
        {
            unsigned int t = right;
            right = left;
            left = t;
        }
        if (left >= s_.size())
            return String();
        if (right > s_.size())
            right = (unsigned int)s_.size();
        String out;
        out.s_.assign(s_, left, right - left);
        out.cut_ = true;
        return out;
    }

    bool startsWith(const String &p) const { return startsWith(p, 0); }
    bool startsWith(const String &p, unsigned int off) const
    {
        if (off > s_.size() || p.s_.size() > s_.size() - off)
            return false;
        return strncmp(c_str() + off, p.c_str(), p.s_.size()) == 0;
    }
    bool endsWith(const String &p) const
    {
        return s_.size() >= p.s_.size() && strcmp(c_str() + s_.size() - p.s_.size(), p.c_str()) == 0;
    }

    void trim()
    {
        if (s_.empty())
            return;
        size_t b = 0, e = s_.size();
        while (b < e && isspace((unsigned char)s_[b]))
            b++;
        while (e > b && isspace((unsigned char)s_[e - 1]))
            e--;
        assign_(s_.substr(b, e - b));
    }
    void toLowerCase()
    {
        for (auto &c : s_)
            c = (char)tolower((unsigned char)c);
    }
    void toUpperCase()
    {
        for (auto &c : s_)
            c = (char)toupper((unsigned char)c);
    }
    long toInt() const { return atol(c_str()); } // WString.cpp: atol, not atoi
    const std::string &std_str() const { return s_; }
    // the whole buffer as a heap dump would show it: the content, then the bytes of earlier, longer content (rig probe)
    std::string bufferImage() const { return s_ + resid_; }

private:
    void assign_(const std::string &next)
    {
        const std::string img = s_ + resid_;
        s_ = next;
        resid_ = img.size() > s_.size() ? img.substr(s_.size()) : std::string();
    }
    void append_(const std::string &add)
    {
        s_ += add;
        resid_ = resid_.size() > add.size() ? resid_.substr(add.size()) : std::string();
    }
    std::string s_;
    std::string resid_; // bytes past the end that a real buffer still holds
    bool cut_ = false;  // made by substring() (or copied / moved from such a String): a piece of some request
};

inline String operator+(const char *a, const String &b) { return String(a) + b; }

// ---- Print / Stream: what ArduinoJson and the web client derive from
class Print
{
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t b) = 0;
    virtual size_t write(const uint8_t *buf, size_t n)
    {
        size_t k = 0;
        while (k < n && write(buf[k]) == 1)
            k++;
        return k;
    }
    size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
    size_t print(const char *s) { return write(s); }
    size_t print(const String &s) { return write((const uint8_t *)s.c_str(), s.length()); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int v) { return printNum("%d", v); }
    size_t print(unsigned int v) { return printNum("%u", v); }
    size_t print(long v) { return printNum("%ld", v); }
    size_t print(unsigned long v) { return printNum("%lu", v); }
    size_t println() { return write((const uint8_t *)"\r\n", 2); }
    size_t println(const char *s) { return print(s) + println(); }
    size_t println(const String &s) { return print(s) + println(); }
    size_t println(char c) { return print(c) + println(); }
    size_t println(int v) { return print(v) + println(); }
    size_t println(unsigned int v) { return print(v) + println(); }
    size_t println(long v) { return print(v) + println(); }
    size_t println(unsigned long v) { return print(v) + println(); }
    size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n <= 0)
            return 0;
        if ((size_t)n >= sizeof(buf))
            n = (int)sizeof(buf) - 1;
        return write((const uint8_t *)buf, (size_t)n);
    }
    virtual void flush() {}

private:
    template <typename T> size_t printNum(const char *f, T v)
    {
        char b[32];
        int n = snprintf(b, sizeof(b), f, v);
        return write((const uint8_t *)b, (size_t)n);
    }
};

class Stream : public Print
{
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() { return -1; }
};

// ---- Serial: collects the output (the rig records it per request)
class SerialStub : public Print
{
public:
    void begin(unsigned long) {}
    size_t write(uint8_t b) override
    {
        out_ += (char)b;
        return 1;
    }
    using Print::write;
    const std::string &captured() const { return out_; }
    void clear() { out_.clear(); }

private:
    std::string out_;
};
extern SerialStub Serial;
