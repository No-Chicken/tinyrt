"""Compile independent C apps to Wasm with LLVM/Clang or Zig's Clang frontend."""
import argparse, os, shutil, subprocess
from pathlib import Path

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--cc',default=os.environ.get('TINYRT_CC'),help='Path to standard clang or zig; no global install required')
    parser.add_argument('--out',type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    cc=args.cc or shutil.which('clang') or shutil.which('zig')
    if not cc:parser.error('Pass --cc pointing to clang with wasm32 backend, or Zig 0.13.0')
    output=args.out or root/'build/fixture-guests';output.mkdir(parents=True,exist_ok=True)
    zig=Path(cc).stem.lower()=='zig'
    common=([cc,'cc','-target','wasm32-freestanding'] if zig else [cc,'--target=wasm32-unknown-unknown'])
    common+=['-std=c11','-O2','-nostdlib','-fno-builtin','-I',str(root/'contracts'),'-Wl,--no-entry','-Wl,--export=tinyrt_init','-Wl,--export=tinyrt_event','-Wl,--export=tinyrt_render','-Wl,-z,stack-size=16384','-Wl,--initial-memory=0x10000','-Wl,--max-memory=0x20000','-Wl,--strip-all']
    env=os.environ.copy(); env.setdefault('ZIG_GLOBAL_CACHE_DIR',str(root/'build/zig-cache'))
    for name,source,defines in [('counter-v1','counter.c',['-DCOUNTER_VERSION=1']),('counter-v2','counter.c',['-DCOUNTER_VERSION=2']),('color','color.c',[])]:
        target=output/(name+'.wasm');subprocess.run(common+defines+[str(root/'tests/fixtures/guests'/source),'-o',str(target)],env=env,check=True)
        print(f'{target}: {target.stat().st_size} bytes')
if __name__=='__main__':main()
