#!/bin/sh
# Robot mode voice set: every button gets a UNIQUE synthesized sound with its
# own character. All original synthesis — committed to the repo.
# Numeric prefixes define the firmware button order (bit index). Re-run
# encode.py after changing anything here.
set -e
cd "$(dirname "$0")"
mkdir -p assets

g() {  # g <name> <expr> <dur>
    ffmpeg -y -loglevel error -f lavfi -i "aevalsrc=$2:d=$3:s=48000" \
        -af "volume=0.9" "assets/$1.wav"
}

# --- face buttons: melodic two/three-note motifs -------------------------
# cross: happy confirm "bee-BOOP" (up)
g robot_btn_00_cross    "0.5*sin(2*PI*660*t)*exp(-9*t)+0.55*sin(2*PI*880*(t-0.13))*exp(-7*(t-0.13))*gte(t\,0.13)" 0.45
# circle: cheeky decline "boo-beep" (down)
g robot_btn_01_circle   "0.5*sin(2*PI*880*t)*exp(-9*t)+0.55*sin(2*PI*587*(t-0.13))*exp(-7*(t-0.13))*gte(t\,0.13)" 0.45
# square: fast trill (alternating tones)
g robot_btn_02_square   "0.5*sin(2*PI*(800+150*sgn(sin(2*PI*18*t)))*t)*exp(-4*t)" 0.4
# triangle: curious rising "hmm?"
g robot_btn_03_triangle "0.5*sin(2*PI*(480+380*t*t)*t)*(1-0.5*t)" 0.55

# --- dpad: intuitive directional pitch logic ------------------------------
g robot_btn_04_up       "0.55*sin(2*PI*(420+900*t)*t)*exp(-3*t)" 0.45
g robot_btn_05_right    "0.5*sin(2*PI*1500*t)*exp(-14*t)+0.5*sin(2*PI*1500*(t-0.11))*exp(-14*(t-0.11))*gte(t\,0.11)" 0.32
g robot_btn_06_down     "0.55*sin(2*PI*(1300-900*t)*t)*exp(-3*t)" 0.45
g robot_btn_07_left     "0.5*sin(2*PI*310*t)*exp(-12*t)+0.5*sin(2*PI*310*(t-0.13))*exp(-12*(t-0.13))*gte(t\,0.13)" 0.36

# --- shoulders/triggers: mechanical whips + charge buzzes ------------------
g robot_btn_08_l1       "0.55*sin(2*PI*(950-500*t/0.22)*t)*exp(-8*t)" 0.28
g robot_btn_09_r1       "0.55*sin(2*PI*(480+520*t/0.22)*t)*exp(-8*t)" 0.28
g robot_btn_10_l2       "0.45*(2*mod(190*(1-0.55*t)*t\,1)-1)*(0.8+0.2*sin(2*PI*22*t))*(1-0.55*t)" 0.6
g robot_btn_11_r2       "0.45*(2*mod((160+240*t)*t\,1)-1)*(0.8+0.2*sin(2*PI*22*t))*(1-0.4*t)" 0.6

# --- stick clicks: squish and pop ------------------------------------------
g robot_btn_12_l3       "0.65*sin(2*PI*(250-70*t/0.2)*t)*exp(-10*t)" 0.3
g robot_btn_13_r3       "0.6*sin(2*PI*1250*t)*exp(-22*t)" 0.22

# --- system buttons ---------------------------------------------------------
# create: shutter trill (three fast ticks)
g robot_btn_14_create   "0.5*sin(2*PI*1100*t)*exp(-30*t)+0.5*sin(2*PI*1100*(t-0.07))*exp(-30*(t-0.07))*gte(t\,0.07)+0.5*sin(2*PI*1400*(t-0.14))*exp(-25*(t-0.14))*gte(t\,0.14)" 0.35
# options: bell-ish confirmation ding (two partials, long decay)
g robot_btn_15_options  "(0.4*sin(2*PI*1000*t)+0.3*sin(2*PI*1520*t))*exp(-4*t)" 0.7
# touchpad: big round satisfied "boop"
g robot_btn_16_tpad     "0.6*sin(2*PI*440*t)*(1+0.15*sin(2*PI*7*t))*exp(-4*t)" 0.6
# ps: little fanfare arpeggio C-E-G-C
g robot_btn_17_ps       "0.5*sin(2*PI*523*t)*exp(-8*t)+0.5*sin(2*PI*659*(t-0.12))*exp(-8*(t-0.12))*gte(t\,0.12)+0.5*sin(2*PI*784*(t-0.24))*exp(-8*(t-0.24))*gte(t\,0.24)+0.55*sin(2*PI*1046*(t-0.36))*exp(-5*(t-0.36))*gte(t\,0.36)" 0.85

# --- personality: falls, reactions, idle -----------------------------------
g robot_fall     "0.6*sin(2*PI*(1500-500*t+90*sin(2*PI*9*t))*t)*(1-0.4*t)" 1.6
g robot_impact_1 "0.6*sin(2*PI*300*t)*exp(-4*t)+0.5*sin(2*PI*240*(t-0.3))*exp(-4*(t-0.3))*gte(t\,0.3)" 0.9
g robot_catch_1  "0.55*sin(2*PI*(600+1200*t)*t)*exp(-2.5*t)" 0.7
g robot_idle_1   "0.35*sin(2*PI*(1000+300*sin(2*PI*3*t))*t)*exp(-3*t)" 0.5
g robot_idle_2   "0.35*sin(2*PI*(800+500*t)*t)*exp(-5*t)" 0.35

echo "robot voice set written ($(ls assets/robot_* | wc -l | tr -d ' ') files)"
