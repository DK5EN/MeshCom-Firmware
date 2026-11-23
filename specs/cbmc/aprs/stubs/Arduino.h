// FORMAL-01 harness-local Arduino.h: replaces test/support/Arduino.h for ESBMC (which cannot parse the std::string
// based String there). String is a bounded char buffer with exactly the operations src/aprs_functions.cpp uses.
// The aprs/ dir stubs/ is put FIRST on the include path; everything else resolves to src/ and test/support/.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>


typedef uint8_t byte;
typedef bool boolean;
#define PROGMEM
#define F(x) (x)
#define HIGH 1
#define LOW 0

extern "C" int nondet_int();

#ifndef STR_CAP
#define STR_CAP 8    // 7 chars max: CheckGroup only branches on length > 6 and on 0..9 chars, so truncation keeps its control flow; pos_atxt/pos_name are never read back
#endif

class String
{
public:
    String() { b_[0] = 0; n_ = 0; }
    String(const char *s) { set(s); }
    String(const String &o) { set(o.b_); }
    String(char c) { b_[0] = c; b_[1] = 0; n_ = c ? 1 : 0; }
    String(int v) { snprintf(b_, STR_CAP, "%d", v); n_ = strlen(b_); }
    String(unsigned int v) { snprintf(b_, STR_CAP, "%u", v); n_ = strlen(b_); }
    String(long v) { snprintf(b_, STR_CAP, "%ld", v); n_ = strlen(b_); }
    String(unsigned long v) { snprintf(b_, STR_CAP, "%lu", v); n_ = strlen(b_); }
    String(float v, int d = 2) { snprintf(b_, STR_CAP, "%.*f", d, (double)v); n_ = strlen(b_); }
    String(double v, int d = 2) { snprintf(b_, STR_CAP, "%.*f", d, v); n_ = strlen(b_); }
    String &operator=(const String &o) { set(o.b_); return *this; }
    String &operator=(const char *s) { set(s); return *this; }
    const char *c_str() const { return b_; }
    unsigned int length() const { return n_; }
    char charAt(unsigned int i) const { return i < n_ ? b_[i] : 0; }
    char operator[](unsigned int i) const { return charAt(i); }
    // value is only ever compared / stored, never used as an index: a nondet value over-approximates (atol unrolls per byte)
    long toInt() const { return nondet_int(); }
    float toFloat() const { return (float)nondet_int(); }
    void trim()
    {
        unsigned int s = 0, e = n_;
        while(s < e && (b_[s] == ' ' || b_[s] == '\t' || b_[s] == '\r' || b_[s] == '\n')) s++;
        while(e > s && (b_[e-1] == ' ' || b_[e-1] == '\t' || b_[e-1] == '\r' || b_[e-1] == '\n')) e--;
        memmove(b_, b_ + s, e - s); n_ = e - s; b_[n_] = 0;
    }
    bool concat(const char *s)
    {
        size_t l = strlen(s);
        if(n_ + l >= STR_CAP) return false;
        memcpy(b_ + n_, s, l + 1); n_ += l; return true;
    }
    bool concat(const String &s) { return concat(s.b_); }
    bool concat(char c) { char t[2] = {c, 0}; return concat(t); }
    String &operator+=(const char *s) { concat(s); return *this; }
    String &operator+=(const String &s) { concat(s.b_); return *this; }
    String &operator+=(char c) { concat(c); return *this; }
    int indexOf(char c, unsigned int from = 0) const
    {
        for(unsigned int i = from; i < n_; i++) if(b_[i] == c) return (int)i;
        return -1;
    }
    int indexOf(const String &s, unsigned int from = 0) const
    {
        if(from > n_) return -1;
        const char *p = strstr(b_ + from, s.b_);
        return p ? (int)(p - b_) : -1;
    }
    int lastIndexOf(char c) const
    {
        for(int i = (int)n_ - 1; i >= 0; i--) if(b_[i] == c) return i;
        return -1;
    }
    String substring(unsigned int from, unsigned int to) const
    {
        String r;
        if(to > n_) to = n_;
        if(from >= to) return r;
        memcpy(r.b_, b_ + from, to - from); r.n_ = to - from; r.b_[r.n_] = 0;
        return r;
    }
    String substring(unsigned int from) const { return substring(from, n_); }
    bool startsWith(const String &p) const { return strncmp(b_, p.b_, p.n_) == 0; }
    bool endsWith(const String &p) const { return p.n_ <= n_ && strcmp(b_ + n_ - p.n_, p.b_) == 0; }
    bool equals(const String &o) const { return strcmp(b_, o.b_) == 0; }
    int compareTo(const String &o) const { return strcmp(b_, o.b_); }
    bool operator==(const String &o) const { return strcmp(b_, o.b_) == 0; }
    bool operator==(const char *o) const { return strcmp(b_, o) == 0; }
    bool operator!=(const String &o) const { return strcmp(b_, o.b_) != 0; }
    bool operator!=(const char *o) const { return strcmp(b_, o) != 0; }
    void remove(unsigned int idx) { if(idx < n_) { n_ = idx; b_[n_] = 0; } }
    void toUpperCase() { for(unsigned int i = 0; i < n_; i++) if(b_[i] >= 'a' && b_[i] <= 'z') b_[i] -= 32; }
    void toLowerCase() { for(unsigned int i = 0; i < n_; i++) if(b_[i] >= 'A' && b_[i] <= 'Z') b_[i] += 32; }
    void replace(const String &, const String &) {}
    operator const char *() const { return b_; }
private:
    void set(const char *s)
    {
        unsigned i = 0;
        if(s) for(; i < STR_CAP - 1 && s[i]; i++) b_[i] = s[i];
        b_[i] = 0; n_ = i;
    }
    char b_[STR_CAP];
    unsigned int n_;
};

inline String operator+(const String &a, const String &b) { String r(a); r.concat(b); return r; }
inline String operator+(const String &a, const char *b) { String r(a); r.concat(b); return r; }
inline String operator+(const char *a, const String &b) { String r(a); r.concat(b); return r; }

struct SerialStub
{
    int printf(const char *, ...) { return 0; }
    void print(const char *) {}
    void print(const String &) {}
    void println(const char * = "") {}
    void println(const String &) {}
    void flush() {}
};
static SerialStub Serial;

inline unsigned long millis(void) { return 0; }
inline void delay(unsigned long) {}
