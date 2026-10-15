"""Guests exercise real WAMR haptic validation and callback boundaries."""
from pathlib import Path
from make_fixtures import c, call
from make_round_fixtures import guest
import sys
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
imports=[('haptic_pulse',2),('haptic_stop',0),('draw_clear',1)]
render=c(0)+call(2)+b'\x1a'+c(0)
def pulse(strength=50,duration=100): return c(strength)+c(duration)+call(0)
for name,strength,duration in [('valid',50,100),('zero',0,100),('strength',101,100),('duration',50,0),('long',50,2001),('negative',-1,100),('maximum',100,2000)]:
    (out/f'haptic_{name}.wasm').write_bytes(guest(imports,render,init=pulse(strength,duration)))
for name,kwargs in [('render',{'render':pulse()}),('stop',{'render':render,'stop':pulse()}),('event',{'render':render,'event':pulse()}),('cancel',{'render':render,'init':call(1)}),('result',{'render':render,'init':pulse()+c(1)+b'\x73'})]:
    (out/f'haptic_{name}.wasm').write_bytes(guest(imports,**kwargs))
