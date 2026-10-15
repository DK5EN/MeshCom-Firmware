// Host stubs for tools/gw_legacy_capture.py. The legacy gateway-server selection regions are
// compiled UNCHANGED against these stubs; nothing here may implement selection logic.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

class IPAddress
{
public:
  IPAddress() { _a[0] = _a[1] = _a[2] = _a[3] = 0; }
  IPAddress(int a, int b, int c, int d)
  {
    _a[0] = (uint8_t)a; _a[1] = (uint8_t)b; _a[2] = (uint8_t)c; _a[3] = (uint8_t)d;
  }
  uint8_t operator[](int i) const { return _a[i]; }
  uint8_t &operator[](int i) { return _a[i]; }
  bool operator==(const IPAddress &o) const { return memcmp(_a, o._a, 4) == 0; }
  bool operator!=(const IPAddress &o) const { return !(*this == o); }
private:
  uint8_t _a[4];
};

struct MeshcomSettings
{
  int node_hamnet_only;
  char node_gwsrv[3];
  char node_ip[16];
  char node_ntp[40];
};

static MeshcomSettings meshcom_settings;
static IPAddress udp_dest_addr;
static int bDisplayCont = 0;

struct TimeClientStub
{
  IPAddress last;
  int calls = 0;
  void setPoolServerIP(IPAddress a) { last = a; calls++; }
};
static TimeClientStub timeClient;

#define printlndeb(x) ((void)0)
#define printfdeb(...) ((void)0)
