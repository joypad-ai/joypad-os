#!/bin/sh
# Astro mode placeholder sounds: generic synthesized robot chirps/beeps.
# Replace with your own sound pack files (same names) and re-run encode.py.
# NOTE: assets/astro_* and ds5_astro_assets.h are gitignored on purpose —
# pack audio stays local and is never committed.
set -e
cd "$(dirname "$0")"
mkdir -p assets

chirp() {  # chirp <name> <expr> <dur>
    ffmpeg -y -loglevel error -f lavfi \
        -i "aevalsrc=$2:d=$3:s=48000" -af "volume=0.85" "assets/$1.wav"
}

# Button chirps: quick rising/falling two-tone beeps
chirp astro_btn_1 "0.55*sin(2*PI*(900+700*t)*t)*exp(-6*t)" 0.28
chirp astro_btn_2 "0.55*sin(2*PI*(1400-600*t)*t)*exp(-6*t)" 0.28
chirp astro_btn_3 "0.5*(sin(2*PI*880*t)+0.6*sin(2*PI*1320*t))*exp(-8*t)" 0.30
chirp astro_btn_4 "0.55*sin(2*PI*(700+1400*t*t)*t)*exp(-5*t)" 0.34

# Fall: descending worried warble (plays during free-fall)
chirp astro_fall "0.6*sin(2*PI*(1500-500*t+90*sin(2*PI*9*t))*t)*(1-0.4*t)" 1.6

# Impact: sad low double-beep; Catch: happy rising triple
chirp astro_impact_1 "0.6*sin(2*PI*300*t)*exp(-4*t)+0.5*sin(2*PI*240*(t-0.3))*exp(-4*(t-0.3))*gte(t\,0.3)" 0.9
chirp astro_catch_1 "0.55*sin(2*PI*(600+1200*t)*t)*exp(-2.5*t)" 0.7

# Idle: soft curious blip
chirp astro_idle_1 "0.35*sin(2*PI*(1000+300*sin(2*PI*3*t))*t)*exp(-3*t)" 0.5
chirp astro_idle_2 "0.35*sin(2*PI*(800+500*t)*t)*exp(-5*t)" 0.35

echo "astro placeholder assets written"
