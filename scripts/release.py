#! /usr/bin/env python3 -B
import os

def release(clear=False):
    os.makedirs('build/terminal/Release', exist_ok=True)
    os.chdir('build/terminal/Release')
    os.system('cmake ../../.. -GNinja -DCMAKE_BUILD_TYPE=Release -DYOCTO_EMBREE=OFF')
    os.system('cmake --build . --parallel 8' + (' --clean-first' if clear else ''))

if __name__ == '__main__':
    release()