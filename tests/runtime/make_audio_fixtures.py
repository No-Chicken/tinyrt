"""Hand-encoded guests exercise actual WAMR audio import boundaries."""
from pathlib import Path
from make_fixtures import c, call
from make_round_fixtures import guest
import sys

out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
imports=[('audio_play',3),('draw_clear',1)]
render=c(0)+call(1)+b'\x1a'+c(0)
def audio(offset=0,length=32000,rate=16000):
    return c(offset)+c(length)+c(rate)+call(0)+b'\x1a'+c(0)
for name,offset,length,rate in [('valid',0,32000,16000),('zero',0,0,16000),
    ('odd',0,3,16000),('long',0,32002,16000),('rate',0,2,44100),
    ('wrap',-1,2,16000),('range',31999,2,16000)]:
    (out/f'audio_{name}.wasm').write_bytes(guest(imports,render,init=audio(offset,length,rate)))
(out/'audio_render.wasm').write_bytes(guest(imports,audio()))
(out/'audio_stop.wasm').write_bytes(guest(imports,render,stop=audio()))
(out/'audio_event.wasm').write_bytes(guest(imports,render,event=audio()))

(out/'audio_spam.wasm').write_bytes(guest(imports,render,init=(audio()[:-2]*8)+c(0)))
