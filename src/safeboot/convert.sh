# Regenerates ota.h from ota.html. Leading indentation is stripped first: the
# classic-ESP32 safeboot image is within a few hundred bytes of its 704 KB slot
# (tools/safeboot.py), and the page is ~9 KB of pure indentation. The arrays are
# const so the page stays in flash instead of competing with the 44 KB inflate
# block for DRAM. The stripped page is then gzip -9n compressed (36 KB -> 12 KB,
# deterministic); ElegantOTA.cpp serves it with Content-Encoding: gzip. The temp
# file keeps the name ota.html so xxd emits ota_html / ota_html_len.
t=$(mktemp -d) || exit 1
sed 's/^[[:space:]]*//' ota.html | gzip -9n > "$t/ota.html" &&
  (cd "$t" && xxd -i ota.html > ota.c) &&
  sed 's/^unsigned/const unsigned/' "$t/ota.c" > ota.h
rc=$?
rm -rf "$t"
exit $rc
