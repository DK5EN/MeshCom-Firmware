import sys, time
sys.path.insert(0, "/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench")
import tdeck_harness as h
s = h.TDeckSession("/dev/cu.usbmodem1101", 115200, 40.0)
s.open()
s.send("--tab 0"); time.sleep(1)
s.send("--mute on"); time.sleep(1.5)
for i in range(3):
    s.send(f"--injectmsg 9999 FRAME test {i}"); time.sleep(3)
    s.send("--framedump"); time.sleep(3)
    idx = s.send("--uistat"); m = s.wait_for(r"\[UISTAT\]", 2.0, since=idx)
    r = h.parse_line(m.string) if m else None
    print(f"after msg {i}: msg_list={r and r.get('msg_list')} scroll_y={r and r.get('scroll_y')} scroll_bottom={r and r.get('scroll_bottom')} last_y1={r and r.get('last_y1')} last_y2={r and r.get('last_y2')}", flush=True)
s.close(); print("=== DONE", flush=True)
