"""Source lint: the gateway server selection in startMeshComUDP() must never leave srv_host NULL.

Upstream #1209 added the HC and HB servers; in the Internet branch the HB block ended up as the
last link of the if/else-if chain without a closing bare else, so node_gwsrv == "OE" (factory
default) left srv_host NULL. HB also uses split DNS: one name (meshcom.emcomm.ch) in both
branches. Since upstream #1210-#1212 HB is a literal IP in the Hamnet and meshcom.emcomm.ch on
the Internet; HC (Hamcloud, Hamnet only) falls back to the DL Internet server. nRF52 follows the
same rules (DL/HC share the Hamnet server, HC never gets a 44er address on the Internet). This is a plain text scanner: it brace-matches the Hamnet and Internet branches of the
server selection and walks the memcmp(node_gwsrv, "XX", 2) chain of each.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src" / "udp_functions.cpp"

SRV_VALUES = ("OE", "DL", "IT", "HC", "HB")  # accepted by --gateway srv; OE via the final else
HAMNET_COND = re.compile(r"if\s*\(\s*\(node_ip\[0\]\s*==\s*44[^\n]*node_hamnet_only\s*==\s*1\s*\)")
CHAIN_IF = re.compile(r'if\s*\(\s*memcmp\(\s*meshcom_settings\.node_gwsrv\s*,\s*"(\w\w)"\s*,\s*2\s*\)\s*==\s*0\s*\)\s*\{')
HOST = re.compile(r'srv_host\s*=\s*"([^"]*)"\s*;')


def match_brace(text: str, open_idx: int) -> int:
    """Index of the '}' matching the '{' at open_idx (string literals skipped)."""
    assert text[open_idx] == "{"
    depth = 0
    i = open_idx
    while i < len(text):
        c = text[i]
        if c == '"':
            i += 1
            while text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise AssertionError("unbalanced braces in " + SRC.name)


def load_branches(text: str | None = None) -> tuple[str, str]:
    """(hamnet_body, internet_body) of the server selection in startMeshComUDP()."""
    if text is None:
        text = SRC.read_text(encoding="utf-8", errors="replace")
    fn = re.search(r"\bvoid\s+startMeshComUDP\s*\(\s*\)\s*\{", text)
    assert fn, "startMeshComUDP() not found"
    body = text[fn.end() - 1 : match_brace(text, fn.end() - 1) + 1]
    cond = HAMNET_COND.search(body)
    assert cond, "Hamnet condition of the server selection not found"
    h_open = body.index("{", cond.end())
    h_close = match_brace(body, h_open)
    m = re.compile(r"\s*else\s*\{").match(body, h_close + 1)
    assert m, "Internet else branch not found after the Hamnet branch"
    i_open = m.end() - 1
    i_close = match_brace(body, i_open)
    return body[h_open + 1 : h_close], body[i_open + 1 : i_close]


def parse_chain(branch: str) -> tuple[dict[str, str], str | None]:
    """({gwsrv value: block body}, body of the final bare else or None)."""
    blocks: dict[str, str] = {}
    pos = 0
    while True:
        m = CHAIN_IF.search(branch, pos)
        if not m:
            break
        close = match_brace(branch, m.end() - 1)
        blocks[m.group(1)] = branch[m.end() : close]
        pos = close + 1
        e = re.compile(r"\s*else\s*(\{|if\b)").match(branch, pos)
        if not e:
            return blocks, None  # chain ends without a closing else
        if e.group(1) == "{":
            e_close = match_brace(branch, e.end() - 1)
            return blocks, branch[e.end() : e_close]
    return blocks, None


def host_of(block: str) -> str | None:
    m = HOST.search(block)
    return m.group(1) if m else None


def test_chain_ends_in_else_setting_srv_host() -> None:
    for name, branch in zip(("Hamnet", "Internet"), load_branches()):
        blocks, final = parse_chain(branch)
        assert blocks, f"{name} branch: no node_gwsrv chain found"
        assert final is not None, (
            f"{name} branch: node_gwsrv if/else-if chain has no closing bare else, "
            "a gwsrv value outside the chain (e.g. OE) leaves srv_host NULL"
        )
        assert host_of(final), f"{name} branch: final else does not set srv_host"
        for key, block in blocks.items():
            assert host_of(block), f"{name} branch: block {key} does not set srv_host"


def test_internet_branch_resolves_every_srv_value() -> None:
    _, internet = load_branches()
    blocks, final = parse_chain(internet)
    for value in SRV_VALUES:
        block = blocks.get(value, final)
        host = host_of(block) if block is not None else None
        assert host, f"Internet branch: --gateway srv {value} resolves to no srv_host"


def test_hb_hamnet_literal_internet_emcomm() -> None:
    hamnet, internet = load_branches()
    assert host_of(parse_chain(hamnet)[0].get("HB", "")) == "44.142.42.59"
    assert host_of(parse_chain(internet)[0].get("HB", "")) == "meshcom.emcomm.ch"


def test_hc_internet_falls_back_to_dl_internet_server() -> None:
    hamnet, internet = load_branches()
    blocks = parse_chain(internet)[0]
    assert host_of(blocks.get("HC", "")) == host_of(blocks.get("DL", "")) == "meshcom.hamnet.network"
    assert host_of(parse_chain(hamnet)[0].get("HC", "")) == "44.148.230.197"


def _nrf_branches() -> tuple[str, str]:
    text = (ROOT / "src" / "nrf52" / "nrf_eth.cpp").read_text(encoding="utf-8", errors="replace")
    start = text.index("const char *srv_path = NULL;")
    cond = re.compile(r"if\s*\(\s*\(local_addr\[0\]\s*==\s*44[^\n]*\)\s*\n\s*\{").search(text, start)
    assert cond, "nRF52 Hamnet condition not found"
    h_open = cond.end() - 1
    h_close = match_brace(text, h_open)
    m = re.compile(r"\s*else\s*\{").match(text, h_close + 1)
    assert m, "nRF52 Internet else branch not found"
    i_open = m.end() - 1
    return text[h_open : h_close + 1], text[i_open : match_brace(text, i_open) + 1]


def test_nrf52_dl_hc_share_hamnet_server_and_inet_never_44er() -> None:
    hamnet, internet = _nrf_branches()
    assert re.search(r'"DL"[^\n]*\|\|[^\n]*"HC"[^\n]*\n\s*\{[^}]*44, 148, 230, 197', hamnet)
    assert re.search(r'"DL"[^\n]*\|\|[^\n]*"HC"[^\n]*\n\s*\{[^}]*192, 68, 17, 26', internet)
    assert "IPAddress(44," not in internet.replace("IPAddress(44, 148, 224", "")


def test_internet_oe_default_is_oevsv() -> None:
    _, internet = load_branches()
    _, final = parse_chain(internet)
    assert final is not None, "Internet branch: no OE default else block"
    assert host_of(final) == "meshcom.oevsv.at", (
        f"Internet OE default must be meshcom.oevsv.at, got {host_of(final)!r}"
    )
