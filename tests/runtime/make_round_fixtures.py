"""Small hand-encoded guests for optional graphics and stop boundaries."""
from pathlib import Path
from make_fixtures import u, s, sec, c, call


def guest(imports, render, stop=None, *, init=None, event=None, payload=b"TEXT", stop_type=0, stop_kind=0):
    counts = [0, 1, 2, 4, 5, 6, 7, 9]
    types = u(len(counts)) + b"".join(b"\x60" + u(n) + b"\x7f" * n + b"\x01\x7f" for n in counts)
    imp = u(len(imports)) + b"".join(s("tinyrt") + s(name) + b"\0" + u(counts.index(n)) for name, n in imports)
    bodies = [c(0) if init is None else init, c(-1) if event is None else event, render]
    indices = [2, 3, 0]
    names = ["tinyrt_init", "tinyrt_event", "tinyrt_render"]
    if stop is not None:
        bodies.append(stop)
        indices.append(stop_type)
        names.append("tinyrt_stop")
    ex = u(len(names)) + b"".join(s(name) + bytes([stop_kind if name == "tinyrt_stop" else 0]) + u(0 if name == "tinyrt_stop" and stop_kind else len(imports) + i) for i, name in enumerate(names))
    body = lambda b: u(len(b) + 2) + b"\0" + b + b"\x0b"
    return (b"\0asm\1\0\0\0" + sec(1, types) + sec(2, imp)
            + sec(3, u(len(indices)) + b"".join(u(i) for i in indices))
            + sec(5, b"\1\1\1\2") + sec(7, ex)
            + sec(10, u(len(bodies)) + b"".join(body(b) for b in bodies))
            + sec(11, b"\1\0" + c(0) + b"\x0b" + u(len(payload)) + payload))


def fixtures():
    clear = c(0x123456) + call(0) + b"\x1a"
    imports = [("draw_clear", 1), ("draw_round_rect", 6), ("draw_arc", 7), ("draw_text_box", 9)]
    rect = [10, 20, 80, 40, 12, 0xabcdef]
    arc = [233, 233, 200, 12, 0, 360, 0x123456]
    text = [40, 30, 300, 56, 0, 4, 0xffffff, 48, 1]
    code = lambda args, index: b"".join(c(n) for n in args) + call(index) + b"\x1a"
    # The guest mutates its string after drawing; host must already own a copy.
    render = clear + code(rect, 1) + code(arc, 2) + code(text, 3) + c(0) + c(88) + b"\x3a\0\0" + c(0)
    result = {"round_valid": guest(imports, render)}
    result["round_all_imports"] = guest(imports + [("draw_rect", 5), ("draw_text", 5), ("kv_get", 2), ("kv_set", 2), ("now_ms", 0)], render)
    result["round_cap128"] = guest(imports, clear + code(rect, 1) * 127 + c(0))
    result["round_cap129"] = guest(imports, clear + code(rect, 1) * 128 + c(0))
    cases = {
        "rr_xnegative": (1, rect, 0, -1), "rr_overflow": (1, rect, 2, 2147483647),
        "rr_zero": (1, rect, 2, 0), "rr_negative_radius": (1, rect, 4, -1),
        "rr_large_radius": (1, rect, 4, 21),
        "arc_zero": (2, arc, 2, 0), "arc_negative": (2, arc, 2, -1),
        "arc_overflow": (2, arc, 0, 2147483647), "arc_xedge": (2, arc, 0, 266),
        "arc_leftedge": (2, arc, 0, 199), "arc_thinzero": (2, arc, 3, 0),
        "arc_thick": (2, arc, 3, 201), "arc_negative_angle": (2, arc, 4, -1),
        "arc_angle361": (2, arc, 5, 361),
        "text_negative": (3, text, 0, -1), "text_zero_width": (3, text, 2, 0),
        "text_overflow": (3, text, 2, 2147483647), "text_ptr": (3, text, 4, 65535),
        "text_ptr_wrap": (3, text, 4, -1), "text_empty": (3, text, 5, 0),
        "text_long": (3, text, 5, 64), "text_font": (3, text, 7, 23),
        "text_align_negative": (3, text, 8, -1), "text_align": (3, text, 8, 3),
    }
    for name, (index, source, pos, value) in cases.items():
        args = source.copy(); args[pos] = value
        result[name] = guest(imports, clear + code(args, index) + c(0))
    reverse = arc.copy(); reverse[4:6] = [270, 90]
    result["arc_reversed"] = guest(imports, clear + code(reverse, 2) + c(0))
    for name, payload in (("text_badutf8", b"\xc0\xafAB"), ("text_control", b"A\nBC"), ("text_surrogate", b"\xed\xa0\x80X")):
        result[name] = guest(imports, clear + code(text, 3) + c(0), payload=payload)
    maximum = text.copy(); maximum[5] = 63
    result["text_63"] = guest(imports, clear + code(maximum, 3) + c(0), payload=b"A"*63)
    styles = clear
    for font, align in ((18, 0), (24, 1), (36, 2), (48, 1)):
        args = text.copy(); args[7:] = [font, align]; styles += code(args, 3)
    zero_rect = rect.copy(); zero_rect[4] = 0
    empty_arc = arc.copy(); empty_arc[4:6] = [90, 90]
    styles += code(zero_rect, 1) + code(empty_arc, 2) + c(0)
    result["round_styles"] = guest(imports, styles)
    for name, args, index in (("rr", rect, 1), ("arc", arc, 2), ("textbox", text, 3)):
        result[name+"_init"] = guest(imports, clear+c(0), init=code(args,index)+c(0))
        result[name+"_event"] = guest(imports, clear+c(0), event=code(args,index)+c(0))
        result[name+"_noclear"] = guest(imports, code(args,index)+c(0))
    stop_imports = [("draw_clear", 1), ("kv_set", 2)]
    write = c(0) + c(77) + call(1) + b"\x1a"
    failed_write = c(0) + c(999) + call(1) + b"\x1a"
    result["stop_valid"] = guest(stop_imports, clear + c(0), write + c(0))
    result["stop_bad_signature"] = guest(stop_imports, clear + c(0), c(0), stop_type=1)
    result["stop_bad_kind"] = guest(stop_imports, clear + c(0), c(0), stop_kind=2)
    result["stop_legacy"] = guest(stop_imports, clear + c(0))
    result["stop_failure"] = guest(stop_imports, clear + c(0), failed_write + c(-1))
    result["stop_trap"] = guest(stop_imports, clear + c(0), failed_write + b"\0" + c(0))
    result["stop_spin"] = guest(stop_imports, clear + c(0), failed_write + b"\x03\x40\x0c\0\x0b" + c(0))
    result["stop_draw"] = guest(stop_imports, clear + c(0), clear + c(0))
    result["stop_init_failure"] = guest(stop_imports, clear + c(0), write + c(0), init=c(-1))
    result["stop_render_failure"] = guest(stop_imports, clear + c(-1), write + c(0))
    return result


if __name__ == "__main__":
    import sys
    folder = Path(sys.argv[1]); folder.mkdir(parents=True, exist_ok=True)
    for name, data in fixtures().items():
        (folder / (name + ".wasm")).write_bytes(data)
