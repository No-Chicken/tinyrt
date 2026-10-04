"""Real Wasm fixtures for atomic raster submissions and owned framebuffer snapshots."""
from pathlib import Path
import struct,sys
from make_round_fixtures import guest as raw_guest
from make_pixel_fixtures import code
from make_fixtures import c

def guest(*args,**kwargs):
    kwargs.setdefault("event",c(0))
    return raw_guest(*args,**kwargs)

def fixtures():
    imp=[('gfx_begin',1),('gfx_submit',2),('gfx_end',0),('gfx_pal_upload',4),('gfx_tex_upload',7),('gfx_tex_free',1),('fb_config',3),('fb_present',6)]
    rect=struct.pack('<HHiiiiII',2,28,0,0,2,2,0xf800,255)
    palette=bytes.fromhex('000000f8e0071f00')
    grid=struct.pack('<HHiiIIIIIII',9,44,0,0,2,2,1,1,0,0,0)+bytes([1,2,3,0])
    begin=code([0],0);end=code([],2)
    out={}
    out['gfx_rect']=guest(imp,begin+code([0,28],1)+end+c(0),payload=rect)
    # The first legal record must not be rendered when a later record is invalid.
    bad=rect+struct.pack('<HHI',2,8,0)
    out['gfx_invalid']=guest(imp,begin+code([0,len(bad)],1)+end+c(0),payload=bad)
    out['gfx_grid']=guest(imp,begin+code([8,44],1)+end+c(0),init=code([0,0,4,0],3)+c(0),payload=palette+grid)
    sprite=struct.pack('<HHiiiiIIIIIIII',5,52,0,0,2,2,0,0,0,2,2,1,0,0)
    texture=bytes([1,2,3,0]);payload=palette+texture+sprite
    out['gfx_resident']=guest(imp,begin+code([12,52],1)+end+c(0),init=code([0,0,4,0],3)+code([0,1,2,2,8,4,0],4)+c(0),event=code([0],5)+c(0),payload=payload)
    out['gfx_noclose']=guest(imp,begin+code([0,28],1)+c(0),payload=rect)
    out['gfx_badflags']=guest(imp,code([2],0)+c(0))
    out['gfx_fb']=guest(imp,code([0,8,0,0,2,2],7)+c(0),init=code([2,2,2],6)+c(0),payload=bytes.fromhex('00f8e0071f00ffff'))
    return out
if __name__=='__main__':
    root=Path(sys.argv[1]);root.mkdir(parents=True,exist_ok=True)
    for name,data in fixtures().items():(root/(name+'.wasm')).write_bytes(data)
