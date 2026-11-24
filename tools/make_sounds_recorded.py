"""Builds the mod's sounds from recordings (tools/sounds_src) into Data/Sound/fo4minigolf/<bank>_<n>.wav: mono, 16-bit,
44.1 kHz, trimmed, faded, peaks at -1.5 dB. Banks it has recordings for replace make_sounds.py's synthesised ones;
the rest (splash, holein) stay synthesised. Needs ffmpeg.

    python tools/make_sounds_recorded.py

Sources (all free to ship in the mod):
  putt  freesound 408260 "putt.wav" by 170129 (CC0): eight putts in a row, the six clean strikes cut out;
        freesound 512505 "Minigolf putt, right into the hole!" by pfranzen (CC0): its strike
  cup   freesound 20428 "Drop ball in cup-1.wav" by AGFX (CC BY 4.0: credited in the README);
        512505 (above): its drop; freesound 21878 "golf_putting.wav" by inbeeld (CC0): its drop
  wall  Kenney "Impact Sounds" (CC0): light wood (the rails) and light metal (the set pieces) knocks
  rim   Kenney: light plate knocks, pitched up and quiet (the cup's lip)
  land  Kenney: light generic and soft knocks (a hop landing on the turf)
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "sounds_src")
OUT = os.path.join(ROOT, "dist", "Data", "Sound", "fo4minigolf")
K = os.path.join(SRC, "kenney")

# (bank, source, start s, length s, extra filters)
CLIPS = {
    "putt": [("freesound_408260_putt_170129.mp3", t - 0.012, 0.42, "afftdn=nr=24:nf=-40,agate=threshold=0.02:release=80")
             for t in (1.191, 3.846, 6.108, 9.785, 11.617, 13.805)]
    + [("freesound_512505_minigolf_putt_pfranzen.mp3", 0.080, 0.40, "")],
    "cup": [("agfx_drop_ball_in_cup_1.wav", 0.065, 2.10, "acompressor=threshold=-24dB:ratio=3:attack=1:release=60:makeup=6dB"),
            ("freesound_512505_minigolf_putt_pfranzen.mp3", 0.88, 1.64, ""),
            ("freesound_21878_golf_putting_inbeeld.mp3", 0.38, 1.10, "acompressor=threshold=-30dB:ratio=3:attack=1:release=60:makeup=8dB")],
    "wall": [(os.path.join("kenney", f + ".ogg"), 0.0, 0.40, "") for f in
             ("impact_wood_light_000", "impact_wood_light_001", "impact_wood_light_002", "impact_wood_light_003",
              "impact_metal_light_000", "impact_metal_light_002")],
    "rim": [(os.path.join("kenney", f + ".ogg"), 0.0, 0.35, "asetrate=44100*1.35,aresample=44100") for f in
            ("impact_plate_light_001", "impact_plate_light_003")],
    "land": [(os.path.join("kenney", f + ".ogg"), 0.0, 0.30, "") for f in
             ("impact_generic_light_000", "impact_generic_light_002", "impact_soft_medium_001", "impact_soft_medium_003")],
}
PEAK = {"putt": -1.5, "cup": -1.5, "wall": -1.5, "rim": -6.0, "land": -4.0}  # (the rim and a landing: quieter)


def peak_db(path):
    out = subprocess.run(["ffmpeg", "-hide_banner", "-i", path, "-af", "volumedetect", "-f", "null", "-"],
                         capture_output=True, text=True).stderr
    return float(out.split("max_volume:")[1].split("dB")[0])


def main():
    for bank, clips in CLIPS.items():
        for f in os.listdir(OUT):
            if f.startswith(bank + "_") and f.endswith(".wav"):
                os.remove(os.path.join(OUT, f))
        for n, (src, start, length, extra) in enumerate(clips, 1):
            tmp = os.path.join(OUT, f"_{bank}_{n}.wav")
            fade = min(0.25, length * 0.4)
            chain = [f"atrim=start={max(0.0, start):.3f}:duration={length:.3f}", "asetpts=PTS-STARTPTS", "highpass=f=50"]
            if extra:
                chain.append(extra)
            chain += [f"afade=t=in:d=0.004", f"afade=t=out:st={length - fade:.3f}:d={fade:.3f}"]
            subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", os.path.join(SRC, src), "-af", ",".join(chain),
                            "-ac", "1", "-ar", "44100", "-sample_fmt", "s16", tmp], check=True)
            gain = PEAK[bank] - peak_db(tmp)
            out = os.path.join(OUT, f"{bank}_{n}.wav")
            subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", tmp, "-af", f"volume={gain:.2f}dB",
                            "-ac", "1", "-ar", "44100", "-sample_fmt", "s16", out], check=True)
            os.remove(tmp)
            print(f"{bank}_{n}.wav  <- {src} @{start:.2f}s +{length:.2f}s")


if __name__ == "__main__":
    sys.exit(main())
