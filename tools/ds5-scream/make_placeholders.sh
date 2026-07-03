#!/bin/sh
# Synthesize placeholder voice assets for the DS5 drop-scream feature.
# Quips use macOS `say`; the scream is a vibrato sine sweep (cartoon scream).
# Replace any file in assets/ with a real recording and re-run encode.py.
set -e
cd "$(dirname "$0")"
mkdir -p assets

# Scream: falling-pitch sweep (950->590Hz) + 2nd harmonic + tremolo reads as
# a cartoon "aaaaah" — far more scream-like than a steady vibrato sine.
ffmpeg -y -loglevel error -f lavfi \
    -i "aevalsrc=(0.62*sin(2*PI*(950*t-90*t*t))+0.28*sin(4*PI*(950*t-90*t*t)))*(0.82+0.18*sin(2*PI*11*t)):d=2.0:s=48000" \
    -af "volume=0.9" assets/scream.wav

s() { say -o "assets/$1.aiff" "$2"; }
s impact_oof      "oof! dude, that hurt."
s impact_ow       "ow ow ow ow ow."
s impact_okay     "I'm okay... I'm okay."
s impact_why      "why would you do that?"
s impact_lawyer   "that's it. I'm calling my lawyer."
s catch_nice      "nice catch, dude."
s catch_phew      "phew. that was a close one."
s catch_life      "my life just flashed before my eyes."

echo "placeholders written to $(pwd)/assets/"
