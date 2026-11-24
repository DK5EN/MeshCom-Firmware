// AU-28 host harness: compiles HTTPClient::handleHeaderResponse() (stock text, or stock text run
// through tools/arduino_esp32_patch.py, selected with -DVARIANT=\"file\") against a std::string
// String and a byte-stream client, then feeds it raw HTTP response heads.
// usage: harness [-t MS_PER_BYTE] head-file...   (MS_PER_BYTE simulates a trickling server)
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <sstream>
#define log_v(...) do{}while(0)
#define log_d(...) do{}while(0)
static unsigned long g_ms = 0;
static unsigned long g_calls=0;
static unsigned long millis(){ if(++g_calls % 1000 == 0) g_ms++; return g_ms; }
static void delay(unsigned long d){ g_ms += d; }
class String {
public:
  std::string s;
  String(){} String(const char*c):s(c?c:""){} String(const std::string&x):s(x){}
  String& operator+=(char c){ s.push_back(c); return *this; }
  String& operator+=(const char*c){ s+=c; return *this; }
  String& operator+=(const String&o){ s+=o.s; return *this; }
  size_t length() const { return s.size(); }
  const char* c_str() const { return s.c_str(); }
  void trim(){ size_t a=0; while(a<s.size()&&isspace((unsigned char)s[a]))a++; size_t b=s.size(); while(b>a&&isspace((unsigned char)s[b-1]))b--; s=s.substr(a,b-a); }
  bool startsWith(const char*p) const { return s.compare(0,strlen(p),p)==0; }
  char operator[](size_t i) const { return i<s.size()?s[i]:0; }
  int indexOf(const char*p) const { size_t r=s.find(p); return r==std::string::npos?-1:(int)r; }
  int indexOf(char c, int from=0) const { if(from<0)from=0; size_t r=s.find(c,(size_t)from); return r==std::string::npos?-1:(int)r; }
  String substring(int a) const { if(a<0)a=0; if((size_t)a>s.size()) return String(); return String(s.substr(a)); }
  String substring(int a,int b) const { if(b<0) b=(int)s.size(); if(a>b) std::swap(a,b); if(a<0)a=0; if((size_t)b>s.size())b=(int)s.size(); if((size_t)a>s.size()) return String(); return String(s.substr(a,b-a)); }
  long toInt() const { return atol(s.c_str()); }
  bool equalsIgnoreCase(const String&o) const { if(o.s.size()!=s.size())return false; for(size_t i=0;i<s.size();i++) if(tolower((unsigned char)s[i])!=tolower((unsigned char)o.s[i]))return false; return true; }
  bool operator==(const char*c) const { return s==c; }
  bool isEmpty() const { return s.empty(); }
  void clear(){ s.clear(); }
};
enum { HTTPC_ERROR_CONNECTION_REFUSED=-1, HTTPC_ERROR_NOT_CONNECTED=-4, HTTPC_ERROR_CONNECTION_LOST=-5,
  HTTPC_ERROR_NO_HTTP_SERVER=-7, HTTPC_ERROR_TOO_LESS_RAM=-8, HTTPC_ERROR_ENCODING=-9, HTTPC_ERROR_READ_TIMEOUT=-11 };
struct FakeClient; 
enum transferEncoding_t { HTTPC_TE_IDENTITY, HTTPC_TE_CHUNKED };

static unsigned long g_ms_per_byte = 0;
struct FakeClient { std::string d; size_t p=0; int available(){ return (int)(d.size()-p); } int read(){ if(p<d.size()){ g_ms += g_ms_per_byte; return (unsigned char)d[p++]; } return -1; }
  String readStringUntil(char t){ String r; int c=read(); while(c>=0 && c!=t){ r+=(char)c; c=read(); } return r; } };
struct RequestArgument { String key; String value; };
class HTTPClient {
public:
  FakeClient* _client; int _returnCode=0; int _size=-1; bool _canReuse=false, _reuse=false;
  transferEncoding_t _transferEncoding; unsigned long _tcpTimeout=15000; String _location;
  RequestArgument _currentHeaders[2]; size_t _headerKeysCount=2;
  bool connected(){ return _client->available()>0; }
  void setCookie(String, String){}
  int handleHeaderResponse();
};
#include VARIANT
int main(int argc,char**argv){
  for(int i=1;i<argc;i++){
    if(!strcmp(argv[i],"-t") && i+1<argc){ g_ms_per_byte=strtoul(argv[++i],0,10); continue; }
    std::ifstream f(argv[i], std::ios::binary); std::stringstream ss; ss<<f.rdbuf();
    FakeClient fc; fc.d=ss.str(); fc.d += "BODY";
    HTTPClient h; h._client=&fc; h._currentHeaders[0].key="Transfer-Encoding"; h._currentHeaders[1].key="Content-Encoding";
    int code=h.handleHeaderResponse();
    printf("%s code=%d size=%d loclen=%zu consumed=%zu/%zu\n", strrchr(argv[i],'/')+1, code, h._size, h._location.length(), fc.p, fc.d.size());
  }
}
