int HTTPClient::handleHeaderResponse()
{

    if(!connected()) {
        return HTTPC_ERROR_NOT_CONNECTED;
    }

    _returnCode = 0;
    _size = -1;
    _canReuse = _reuse;

    String transferEncoding;

    _transferEncoding = HTTPC_TE_IDENTITY;
    unsigned long lastDataTime = millis();
    bool firstLine = true;
    String date;

    while(connected()) {
        size_t len = _client->available();
        if(len > 0) {
            String headerLine = _client->readStringUntil('\n');
            headerLine.trim(); // remove \r

            lastDataTime = millis();

            log_v("RX: '%s'", headerLine.c_str());

            if(firstLine) {
		        firstLine = false;
                if(_canReuse && headerLine.startsWith("HTTP/1.")) {
                    _canReuse = (headerLine[sizeof "HTTP/1." - 1] != '0');
                }
                int codePos = headerLine.indexOf(' ') + 1;
                _returnCode = headerLine.substring(codePos, headerLine.indexOf(' ', codePos)).toInt();
            } else if(headerLine.indexOf(':')) {
                String headerName = headerLine.substring(0, headerLine.indexOf(':'));
                String headerValue = headerLine.substring(headerLine.indexOf(':') + 1);
                headerValue.trim();

                if(headerName.equalsIgnoreCase("Date")) {
                    date = headerValue;
                }

                if(headerName.equalsIgnoreCase("Content-Length")) {
                    _size = headerValue.toInt();
                }

                if(_canReuse && headerName.equalsIgnoreCase("Connection")) {
                    if(headerValue.indexOf("close") >= 0 && headerValue.indexOf("keep-alive") < 0) {
                        _canReuse = false;
                    }
                }

                if(headerName.equalsIgnoreCase("Transfer-Encoding")) {
                    transferEncoding = headerValue;
                }

                if (headerName.equalsIgnoreCase("Location")) {
                    _location = headerValue;
                }

                if (headerName.equalsIgnoreCase("Set-Cookie")) {
                    setCookie(date, headerValue);
                }

                for (size_t i = 0; i < _headerKeysCount; i++) {
                    if (_currentHeaders[i].key.equalsIgnoreCase(headerName)) {
                        // Uncomment the following lines if you need to add support for multiple headers with the same key:
                        // if (!_currentHeaders[i].value.isEmpty()) {
                        //     // Existing value, append this one with a comma
                        //     _currentHeaders[i].value += ',';
                        //     _currentHeaders[i].value += headerValue;
                        // } else {
                        _currentHeaders[i].value = headerValue;
                        // }
                        break; // We found a match, stop looking
                    }
                }

            }

            if(headerLine == "") {
                log_d("code: %d", _returnCode);

                if(_size > 0) {
                    log_d("size: %d", _size);
                }

                if(transferEncoding.length() > 0) {
                    log_d("Transfer-Encoding: %s", transferEncoding.c_str());
                    if(transferEncoding.equalsIgnoreCase("chunked")) {
                        _transferEncoding = HTTPC_TE_CHUNKED;
                    } else if(transferEncoding.equalsIgnoreCase("identity")) {
                        _transferEncoding = HTTPC_TE_IDENTITY;
                    } else {
                        return HTTPC_ERROR_ENCODING;
                    }
                } else {
                    _transferEncoding = HTTPC_TE_IDENTITY;
                }

                if(_returnCode) {
                    return _returnCode;
                } else {
                    log_d("Remote host is not an HTTP Server!");
                    return HTTPC_ERROR_NO_HTTP_SERVER;
                }
            }

        } else {
            if((millis() - lastDataTime) > _tcpTimeout) {
                return HTTPC_ERROR_READ_TIMEOUT;
            }
            delay(10);
        }
    }

    return HTTPC_ERROR_CONNECTION_LOST;
}