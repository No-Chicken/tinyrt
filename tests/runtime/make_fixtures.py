"""Independent hand-coded Wasm for runtime boundary regression; no compiler needed."""
from pathlib import Path
import struct

def u(n):
    b=bytearray()
    while n>=128: b.append((n&127)|128); n>>=7
    b.append(n); return bytes(b)
def si(n):
    b=bytearray()
    while True:
        a=n&127; n>>=7
        end=(n==0 and not a&64) or (n==-1 and a&64)
        b.append(a if end else a|128)
        if end: return bytes(b)
def s(x):
    b=x.encode(); return u(len(b))+b
def sec(i,b):return bytes([i])+u(len(b))+b
def c(x):return b'\x41'+si(x)
def call(x):return b'\x10'+u(x)
def module(mode='valid'):
    # import function indices: 0 clear, 1 rect, 2 text, 3 get, 4 set, 5 now
    counts=[1,5,5,2,2,0,2,4,0]
    types=u(9)+b''.join(b'\x60'+u(n)+b'\x7f'*n+b'\x01\x7f' for n in counts)
    names=['draw_clear','draw_rect','draw_text','kv_get','kv_set','now_ms']
    if mode=='unknown': names[0]='evil'
    imp=u(6)+b''.join(s('wasi_snapshot_preview1' if mode=='wasi' else 'tinyrt')+s(n)+b'\x00'+u(i) for i,n in enumerate(names))
    mem=b'\x01\x01\x01\x02'
    if mode=='nomax': mem=b'\x01\x00\x01'
    if mode=='shared': mem=b'\x01\x03\x01\x02'
    if mode=='huge': mem=b'\x01\x01\x01\x11'
    ex=[s('tinyrt_init')+b'\x00\x06',s('tinyrt_event')+b'\x00\x07',s('tinyrt_render')+b'\x00\x08']
    if mode=='ctor': ex.append(s('__wasm_call_ctors')+b'\x00\x08')
    if mode=='post': ex.append(s('__post_instantiate')+b'\x00\x08')
    init=c(0)+c(0)+call(3)+b'\x24\x00'+c(0)
    event=b'\x23\x00'+c(1)+b'\x6a\x24\x00'+c(0)+b'\x23\x00'+call(4)+b'\x1a'+c(0)
    if mode=='spin':event=b'\x03\x40\x0c\x00\x0b'+c(0)
    if mode=='badkey':event=c(16)+c(1)+call(4)+b'\x1a'+c(0)
    if mode=='clock':event=call(5)+b'\x1a'+c(0)
    if mode=='bulkfill':event=c(0)+c(7)+c(65536)+b'\xfc\x0b\x00'+c(0)
    if mode=='bulkcopy':event=c(0)+c(1)+c(65535)+b'\xfc\x0a\x00\x00'+c(0)
    if mode=='bulkinit':event=c(0)+c(0)+c(5)+b'\xfc\x08\x00\x00'+c(0)
    if mode=='datadrop':event=b'\xfc\x09\x00'+c(0)
    if mode=='tablecopy':event=c(0)+c(0)+c(1)+b'\xfc\x0e\x00\x00'+c(0)
    if mode=='tablefill':event=c(0)+b'\xd0\x70'+c(1)+b'\xfc\x11\x00'+c(0)
    render=c(0x102030)+call(0)+b'\x1a'
    render+=c(0)+c(0)+b'\x23\x00'+c(20)+c(0xff0000)+call(1)+b'\x1a'
    render+=c(10)+c(20)+c(65535 if mode=='oob' else 0)+c(5)+c(0xffffff)+call(2)+b'\x1a'
    # Mutate guest storage after draw_text: output must already own its bytes.
    render+=c(0)+c(88)+b'\x3a\x00\x00'+c(0)
    if mode=='overflow':render=(c(0)+call(0)+b'\x1a')*129+c(0)
    if mode=='noclear':render=c(0)
    if mode=='badutf8':payload=b'\xc0\xafabc'
    else:payload=b'COUNT'
    if mode=='badsig':types=types.replace(b'\x60\x01\x7f\x01\x7f',b'\x60\x01\x7e\x01\x7f',1)
    body=lambda b:u(len(b)+2)+b'\x00'+b+b'\x0b'
    result=b'\0asm\x01\0\0\0'+sec(1,types)+sec(2,imp)+sec(3,b'\x03\x06\x07\x08')+(sec(4,b'\x01\x70\x01\x01\x01') if mode in ('tablecopy','tablefill') else b'')+sec(5,mem)+sec(6,b'\x01\x7f\x01'+c(0)+b'\x0b')+sec(7,u(len(ex))+b''.join(ex))
    if mode=='start':result+=sec(8,b'\x08')
    if mode in ('bulkinit','datadrop'):result+=sec(12,b'\x01')
    result+=sec(10,b'\x03'+body(init)+body(event)+body(render))+sec(11,b'\x01\x00'+c(0)+b'\x0b'+u(len(payload))+payload)
    if mode=='malformed':result=result[:-1]
    return result
if __name__=='__main__':
    import sys
    out=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).parent/'fixtures'; out.mkdir(parents=True,exist_ok=True)
    for mode in ['valid','unknown','wasi','nomax','shared','huge','ctor','post','start','spin','badkey','clock','oob','overflow','noclear','badutf8','badsig','malformed','bulkfill','bulkcopy','bulkinit','datadrop','tablecopy','tablefill']:
        (out/(mode+'.wasm')).write_bytes(module(mode))
