"""Independent Wasm bytecode fixtures for owned RGB565, skip and scheduling."""
from pathlib import Path
from make_fixtures import c, call
from make_round_fixtures import guest


def code(args, index):
    return b"".join(c(n) for n in args) + call(index) + b"\x1a"


def fixtures():
    imports = [("draw_clear", 1), ("draw_rgb565", 6), ("draw_skip", 0)]
    clear = code([0x123456], 0)
    args = [10, 20, 2, 2, 0, 8]
    draw = code(args, 1)
    skip = code([], 2)
    payload = bytes.fromhex("00f8e0071f00ffff")
    mutate = c(0) + c(0x99) + b"\x3a\0\0"
    result = {"pixel_valid": guest(imports, clear + draw + mutate + c(0), payload=payload)}
    result["pixel_maximum"] = guest(imports, clear + code([105,113,256,240,0,122880],1) + c(0),
                                    payload=bytes(range(256))*480, memory_pages=2)
    cases = {
        "negative_x": (0,-1), "negative_y": (1,-1), "overflow_x": (0,2147483647),
        "overflow_y": (1,2147483647), "edge_x": (0,465), "edge_y": (1,465),
        "width_zero": (2,0), "height_zero": (3,0), "width_negative": (2,-1),
        "height_negative": (3,-1), "width_overflow": (2,2147483647),
        "height_overflow": (3,2147483647), "width257": (2,257), "height241": (3,241),
        "length_zero": (5,0), "length_short": (5,7), "length_long": (5,9),
        "length_wrap": (5,-1), "pointer_wrap": (4,-1), "pointer_end": (4,65532),
    }
    for name,(index,value) in cases.items():
        bad=args.copy();bad[index]=value
        result["pixel_"+name] = guest(imports, clear + code(bad,1) + c(0), payload=payload)
    result["pixel_edge_valid"] = guest(imports,clear+code([464,464,2,2,65528,8],1)+c(0))
    result["pixel_twice"] = guest(imports,clear+draw+draw+c(0),payload=payload)
    result["pixel_noclear"] = guest(imports,draw+c(0),payload=payload)
    result["pixel_cap"] = guest(imports,clear*128+draw+c(0),payload=payload)
    result["skip_valid"] = guest(imports,skip+c(0))
    result["skip_clear"] = guest(imports,skip+clear+c(0))
    result["clear_skip"] = guest(imports,clear+skip+c(0))
    result["skip_pixel"] = guest(imports,skip+draw+c(0),payload=payload)
    result["skip_twice"] = guest(imports,skip+skip+c(0))
    # First frame draws; the next event requests a skipped render.
    result["pixel_then_skip"] = guest(imports,
        c(16)+b"\x2d\0\0\x04\x40"+skip+b"\x05"+clear+draw+b"\x0b"+c(0),
        event=c(16)+c(16)+b"\x2d\0\0"+c(1)+b"\x73\x3a\0\0"+c(0),payload=payload)
    observed_imports = imports + [("now_ms", 0)]
    result["frame_observed"] = guest(observed_imports, clear+draw+code([],3)+c(0),
                                      event=code([],3)+c(0),stop=code([],3)+c(0),payload=payload)
    result["frame_partial_failure"] = guest(imports,clear+draw+c(-1),payload=payload)
    result["frame_empty"] = guest(imports,c(0))
    result["guard_pure"] = guest([],b"\x03\x40\x0c\0\x0b"+c(0))
    result["guard_import"] = guest([("now_ms",0)],b"\x03\x40"+code([],0)+b"\x0c\0\x0b"+c(0))
    for stage in ("init","event","render","stop"):
        loop=b"\x03\x40"+code([],1)+b"\x0c\0\x0b"+c(0)
        bodies={"init":c(0),"event":c(0),"render":clear+c(0),"stop":c(0)}
        bodies[stage]=loop
        result["guard_"+stage]=guest([("draw_clear",1),("now_ms",0)],**bodies)
    input_imports=[("draw_clear",1),("input_events",1)]
    input_render=c(0)+b"\x2d\0\0"+call(0)+b"\x1a"+c(0)
    input_event=c(0)+b"\x20\0\x3a\0\0"+c(0)
    result["input_subscribed"]=guest(input_imports,input_render,init=code([0x38],1)+c(0),event=input_event)
    result["input_disabled"]=guest(input_imports,input_render,init=code([0x38],1)+code([0],1)+c(0),event=input_event)
    for mask in (1,8,16,32,55,64,-1):
        result["input_bad_"+str(mask)]=guest(input_imports,clear+c(0),init=code([mask],1)+c(0))
    for stage in ("event","render","stop"):
        bodies={"event":c(0),"render":clear+c(0),"stop":c(0)}
        bodies[stage]=code([0x38],1)+c(0)
        result["input_"+stage]=guest(input_imports,init=code([0x38],1)+c(0),**bodies)
    for name,body in (("pixel",draw),("skip",skip)):
        for stage in ("init","event","stop"):
            result[name+"_"+stage]=guest(imports,clear+c(0),payload=payload,**{stage:body+c(0)})
    clock_imports=[("clock_interval",1),("draw_clear",1)]
    clear_clock=code([0],1)
    result["clock_valid"]=guest(clock_imports,clear_clock+c(0),init=code([1],0)+c(0),
                                event=b"\x20\3"+call(0)+b"\x1a"+c(0))
    for ms in (0,-1,1001,2147483647):
        result["clock_bad_"+str(ms)]=guest(clock_imports,clear_clock+c(0),init=code([ms],0)+c(0))
    result["clock_render"]=guest(clock_imports,clear_clock+code([1],0)+c(0))
    result["clock_stop"]=guest(clock_imports,clear_clock+c(0),stop=code([1],0)+c(0))
    for name,n in (("draw_rgb565",6),("draw_skip",0),("clock_interval",1)):
        result["permission_"+name]=guest([(name,n)],c(0))
        result["signature_"+name]=guest([(name,2)],c(0))
    return result


if __name__ == "__main__":
    import sys
    folder=Path(sys.argv[1]);folder.mkdir(parents=True,exist_ok=True)
    for name,data in fixtures().items():
        (folder/(name+".wasm")).write_bytes(data)
