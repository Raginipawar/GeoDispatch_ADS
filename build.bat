@echo off
echo Building GeoDispatch...
gcc -std=c11 -O2 -Wall -I. -Isrc -o geodispatch.exe src/main.c src/kd.c src/kd_dynamic.c src/voronoi.c src/algo.c -lm
if %errorlevel% == 0 (
    echo Done. Run: python python/server.py
) else (
    echo Build failed.
)
