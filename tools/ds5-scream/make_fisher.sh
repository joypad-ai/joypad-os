#!/bin/sh
# Fisher-Price mode placeholder voices: numbers (dpad) + letters (face buttons)
# in a bright toy voice (clear TTS pitched up 25%). Replace any file in
# assets/ with real recordings and re-run encode.py.
set -e
cd "$(dirname "$0")"
mkdir -p assets

gen() {  # gen <outname> <text>
    say -v Samantha -o "/tmp/fp_$1.aiff" "$2"
    ffmpeg -y -loglevel error -i "/tmp/fp_$1.aiff" \
        -af "aresample=48000,asetrate=48000*1.25,aresample=48000,volume=0.95" \
        "assets/$1.wav"
    rm -f "/tmp/fp_$1.aiff"
}

gen fisher_num_1 "One!"
gen fisher_num_2 "Two!"
gen fisher_num_3 "Three!"
gen fisher_num_4 "Four!"
gen fisher_abc_1 "A!"
gen fisher_abc_2 "B!"
gen fisher_abc_3 "C!"
gen fisher_abc_4 "D!"
echo "fisher assets written"
