// Stand-in for NimBLELog.h at CONFIG_NIMBLE_CPP_LOG_LEVEL=0 (the library's own macros are (void)tag there). Found first
// because the library source is compiled from a gen/ copy, so its quoted include does not see the library directory.
#pragma once
#define NIMBLE_LOGD(tag, format, ...) (void)tag
#define NIMBLE_LOGI(tag, format, ...) (void)tag
#define NIMBLE_LOGW(tag, format, ...) (void)tag
#define NIMBLE_LOGE(tag, format, ...) (void)tag
