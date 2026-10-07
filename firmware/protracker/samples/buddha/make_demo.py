"""Build a melody-first, self-contained FM1 chant study (no device I/O).

The melodic starting point is the indexed 六字二音 第二種唱韻 excerpt in
基礎梵唄教學課本. Rhythm, accompaniment and ending are this demo's arrangement.
Voice: generic Microsoft Yunxi TTS syllables, resynthesized as pitched vowels.
No commercial chant recording or recording from the user's video is included.
Requires numpy/scipy; --fetch-voice additionally requires edge-tts and ffmpeg.
"""
from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import numpy as np
from scipy import ndimage, signal
from scipy.io import wavfile

RATE = 3546895 / 428  # MOD playback rate at MIDI 48, period 428.
PERIODS = [856,808,762,720,678,640,604,570,538,508,480,453,
           428,404,381,360,339,320,302,285,269,254,240,226,
           214,202,190,180,170,160,151,143,135,127,120,113]
VOICE = "zh-CN-YunxiNeural"
SYLLABLES = [("na", "拿"), ("mo", "摩"), ("a", "阿"),
             ("mi", "弥"), ("tuo", "陀"), ("fo", "佛")]
# 5 5 | 3 3 | 2 1 | 2 3 2 1. Fixed 4/4 timing is our arrangement.
HIGH = [(0,1,55),(4,2,55),(16,3,52),(20,4,52),
        (32,5,50),(36,5,48),(48,6,50),(52,6,52),(54,6,50),(56,6,48)]
# Lower answering line: 1 1 | lower 6 1 | 2 1 | 2 3.
LOW = [(0,1,48),(4,2,48),(16,3,45),(20,4,48),
       (32,5,50),(36,5,48),(48,6,50),(52,6,52)]


async def fetch_voice(folder: Path) -> None:
    import edge_tts
    for label, text in SYLLABLES:
        path = folder / (label + ".mp3")
        if not path.exists():
            await edge_tts.Communicate(text, VOICE, rate="-30%").save(str(path))
        print("voice", label, flush=True)


def quantize(x: np.ndarray, amplitude: float = .78) -> bytes:
    x = np.asarray(x, dtype=np.float64)
    x = x - np.mean(x)
    peak = float(np.max(np.abs(x)))
    if not np.isfinite(peak) or peak <= 0:
        raise ValueError("silent/nonfinite generated sample")
    y = np.clip(np.rint(x / peak * 127 * amplitude), -127, 127).astype(np.int8)
    if len(y) & 1:
        y = np.append(y, np.int8(0))
    return y.tobytes()


def fade(x: np.ndarray, seconds: float = .012) -> np.ndarray:
    x = x.copy()
    n = min(len(x)//2, round(RATE * seconds))
    if n:
        x[:n] *= np.linspace(0,1,n)
        x[-n:] *= np.linspace(1,0,n)
    return x


def make_syllable(path: Path) -> tuple[bytes,int,int,dict]:
    """Retain a speech onset, then join a periodic vowel at a known pitch.

    Harmonic amplitudes come from the TTS vowel; the fixed oscillator pitch
    makes tracker notes meaningful. This is a synthetic voice prototype.
    """
    wav = path.with_suffix(".wav")
    subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-y",
                    "-i",str(path),"-ar","22050","-ac","1",str(wav)],check=True)
    sr, raw = wavfile.read(wav)
    x = raw.astype(np.float64) / 32768
    # Trim only quiet ends, retaining initial consonants.
    energy = ndimage.uniform_filter1d(x*x, size=round(sr*.015))
    active = np.flatnonzero(energy > np.max(energy)*.014)
    if not len(active):
        raise ValueError("empty TTS syllable")
    x = x[max(0,active[0]-round(sr*.018)):min(len(x),active[-1]+round(sr*.018))]
    center = int(np.argmax(ndimage.uniform_filter1d(x*x,size=round(sr*.075))))
    center = min(max(center,round(sr*.07)),len(x)-round(sr*.07))
    vowel = x[center-round(sr*.07):center+round(sr*.07)]
    vowel -= np.mean(vowel)
    ac = signal.correlate(vowel,vowel,mode="full",method="fft")[len(vowel)-1:]
    lo, hi = int(sr/350), int(sr/80)
    peaks,_ = signal.find_peaks(ac[lo:hi])
    if not len(peaks):
        raise ValueError("cannot find voiced pitch")
    lag = lo + int(peaks[np.argmax(ac[lo+peaks])])
    f0 = sr/lag
    nfft = 8192
    spectrum = np.abs(np.fft.rfft(vowel*np.hanning(len(vowel)),n=nfft))
    freq = np.fft.rfftfreq(nfft,1/sr)
    harmonics = np.arange(1,27)
    weights = np.interp(harmonics*f0,freq,spectrum)
    weights /= max(np.max(weights),1e-9)
    # 10 periods in 634 even samples: 130.72 Hz at the MOD root rate.
    loop_length = 634
    phase = np.arange(loop_length)*2*np.pi*10/loop_length
    loop = sum(w*np.sin(h*phase + .12*h) for h,w in zip(harmonics,weights))
    loop /= np.max(np.abs(loop))
    # Brief consonant onset, followed by a crossfade into the pitched vowel.
    onset = signal.resample_poly(x[:round(sr*.18)], 8287, sr)
    onset /= max(np.max(np.abs(onset)),1e-9)
    prefix_length = ((len(onset)+loop_length-1)//loop_length)*loop_length
    body = np.tile(loop,prefix_length//loop_length+1)
    blend = np.zeros(prefix_length+loop_length)
    blend[:len(onset)] = onset*.65
    ramp = np.clip((np.arange(len(blend))-len(onset)*.42)/(len(onset)*.58),0,1)
    blend = blend*(1-ramp)+body[:len(blend)]*ramp
    blend[:64] *= np.linspace(0,1,64)
    pcm = quantize(blend,.72)
    return pcm,prefix_length,loop_length,{"source_pitch_hz":round(f0,2),
                                         "root_pitch_hz":round(RATE*10/634,3)}


def instrument(kind: str) -> bytes:
    rng = np.random.default_rng(821)
    if kind == "flute":
        t = np.arange(round(RATE*2.2))/RATE
        x = (np.sin(2*np.pi*130.81*t)+.18*np.sin(2*np.pi*261.62*t)
             +.05*np.sin(2*np.pi*392.43*t))
        x *= np.minimum(1,t/.055)*np.exp(-t*1.6)
        x += rng.normal(0,.015,len(t))*np.exp(-t*3)
    elif kind == "wood":
        t = np.arange(round(RATE*.15))/RATE
        x = (np.sin(2*np.pi*820*t)*np.exp(-t*40)
             +.45*np.sin(2*np.pi*1440*t)*np.exp(-t*70)
             +rng.normal(0,.1,len(t))*np.exp(-t*120))
    elif kind == "bell":
        t = np.arange(round(RATE*2.4))/RATE
        x = sum(a*np.sin(2*np.pi*196*ratio*t)*np.exp(-t*decay)
                for ratio,a,decay in [(1,1,1.6),(2.71,.4,2.1),(4.08,.18,3.8),(5.43,.12,4.8)])
    elif kind == "pluck":
        t = np.arange(round(RATE*1.5))/RATE
        x = sum(np.sin(2*np.pi*130.81*h*t)/h**1.5*np.exp(-t*(2+h*.5))
                for h in range(1,10))
    else:
        raise ValueError(kind)
    return quantize(fade(x))


def sample_at(pcm: bytes, start: int, repeat: int, midi: int, duration: float,
              phase_start: float = 0) -> np.ndarray:
    x = np.frombuffer(pcm,dtype=np.int8).astype(np.float64)/127
    phase = phase_start + np.arange(round(RATE*duration)) * (428/PERIODS[midi-36])
    phase = np.where(phase >= start+repeat, start+(phase-start)%repeat,phase)
    return np.interp(phase,np.arange(len(x)),x)


def build(folder: Path) -> dict:
    samples = []
    voice_metrics = {}
    for label,_ in SYLLABLES:
        pcm,start,repeat,metrics = make_syllable(folder/"voice"/(label+".mp3"))
        samples.append((label.upper()+" SYNTH C3",pcm,start,repeat,46))
        voice_metrics[label] = metrics
    # Whole phrase is an additional one-shot instrument, not the editable lead.
    phrase = np.zeros(round(RATE*9.6))
    phase_start = 0.0
    for index,(row,slot,midi) in enumerate(HIGH):
        endrow = HIGH[index+1][0] if index+1<len(HIGH) else 64
        # Keep the complete phrase recognizable through the final melisma.
        pcm,start,repeat,_ = samples[slot-1][1:]
        same_word = index>0 and slot==HIGH[index-1][1]
        if not same_word:
            phase_start = 0.0
        segment = sample_at(pcm,start,repeat,midi,(endrow-row)*.15,phase_start)
        phase_start += len(segment)*(428/PERIODS[midi-36])
        # Preserve a single consonant through each word's changing vowel notes.
        edge = min(round(RATE*.008),len(segment)//2)
        if not same_word:
            segment[:edge] *= np.linspace(0,1,edge)
        if index+1==len(HIGH) or HIGH[index+1][1]!=slot:
            segment[-edge:] *= np.linspace(1,0,edge)
        at = round(row*.15*RATE)
        phrase[at:at+len(segment)] += segment[:len(phrase)-at]
    samples.append(("NAMO PHRASE ROOT C3",quantize(fade(phrase)),0,2,42))
    for kind,volume in [("flute",26),("wood",23),("bell",23),("pluck",24)]:
        samples.append((kind.upper()+" ORIGINAL C3",instrument(kind),0,2,volume))
    header = bytearray(1084)
    header[:20] = b"NAMO CHANT STUDY"
    header[950] = 4
    header[952:956] = bytes(range(4))
    header[1080:1084] = b"M.K."
    data = bytearray(4*1024)
    events = []
    def cell(pattern,row,ch,slot=0,midi=0,fx=0,param=0):
        period = PERIODS[midi-36] if midi else 0
        at = pattern*1024+row*16+ch*4
        data[at:at+4] = bytes([(slot&0xf0)|(period>>8),period&255,
                               ((slot&15)<<4)|fx,param])
        events.append({"pattern":pattern,"row":row,"channel":ch+1,
                       "sample":slot,"midi":midi,"effect":fx,"parameter":param})
    for pattern in range(4):
        melody = HIGH if pattern%2==0 else LOW
        for index,(row,slot,midi) in enumerate(melody):
            # Tuo/Fo ornaments change the sounding vowel without repeating its consonant.
            same_word = index>0 and slot==melody[index-1][1]
            cell(pattern,row,0,0 if same_word else slot,midi,3 if same_word else 0,
                 0x30 if same_word else 0)
        for row in range(0,64,8):
            cell(pattern,row,2,9,48,0xc,20 if row%16==0 else 14)
        for row in [0,32]:
            cell(pattern,row,3,10,48,0xc,22)
        # Light instrumental responses and doubling grow through the arrangement.
        if pattern < 2:
            for row,midi in [(8,48),(24,55),(40,48),(60,52)]:
                cell(pattern,row,1,11,midi,0xc,16)
        else:
            for row,slot,midi in melody:
                cell(pattern,row,1,8,midi,0xc,19)
        if pattern==0:
            cell(pattern,0,1,11,48,0xf,100)
            cell(pattern,0,2,9,48,0xf,6)
    pcm_data = bytearray()
    manifest_samples = []
    for slot,(name,pcm,start,repeat,volume) in enumerate(samples,1):
        if not (len(pcm)%2==start%2==repeat%2==0 and len(pcm)<=131070
                and (repeat<=2 or start+repeat<=len(pcm))):
            raise ValueError("invalid MOD sample layout")
        h=20+(slot-1)*30
        header[h:h+22]=name.encode("ascii")[:22].ljust(22,b"\0")
        struct.pack_into(">HBBHH",header,h+22,len(pcm)//2,0,volume,start//2,repeat//2)
        pcm_data.extend(pcm)
        manifest_samples.append({"slot":slot,"hex":f"{slot:02X}","name":name,
                                 "bytes":len(pcm),"loop_start":start,"loop_bytes":repeat,
                                 "sha256":hashlib.sha256(pcm).hexdigest()})
    mod=bytes(header+data+pcm_data)
    if len(mod)>256*1024:
        raise ValueError("FM1 song buffer overflow")
    (folder/"NAMO.MOD").write_bytes(mod)
    for slot,(name,pcm,start,repeat,_) in enumerate(samples,1):
        wavfile.write(folder/f"{slot:02X}-{name.split()[0].lower()}.wav",round(RATE),
                      np.frombuffer(pcm,dtype=np.int8).astype(np.int16)*256)
    manifest={"title":"Namo chant study","phrase":"南無阿彌陀佛",
              "arrangement":"Six-character chant study; original fixed timing/accompaniment",
              "bpm":100,"speed":6,"orders":4,"patterns":4,"nominal_seconds":38.4,
              "root_note":"MIDI 48 (physical C3; MOD display C-2)",
              "voice":"Synthetic TTS-derived vowels; not a human chant recording",
              "tts_voice":VOICE,"tts_texts":dict(SYLLABLES),"voice_metrics":voice_metrics,
              "source_scope":"Indexed score excerpt only; not a verified transcription of the supplied video",
              "mod_bytes":len(mod),"sha256":hashlib.sha256(mod).hexdigest(),
              "samples":manifest_samples,"events":events,
              "source_urls":["https://www.ez-songs.com/product-info.asp?id=664",
                             "https://play.google.com/store/apps/details?id=com.phoenix.buddhasongs",
                             "https://compassion.familyds.org/library/Ebooks/Cabinet_05/52013%E5%9F%BA%E7%A4%8E%E6%A2%B5%E5%94%84%E6%95%99%E5%AD%B8%E8%AA%B2%E6%9C%AC.pdf"]}
    (folder/"manifest.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding="utf-8")
    return manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,default=Path("firmware/protracker/build/buddha"))
    parser.add_argument("--fetch-voice",action="store_true")
    args=parser.parse_args()
    (args.out/"voice").mkdir(parents=True,exist_ok=True)
    if args.fetch_voice:
        asyncio.run(fetch_voice(args.out/"voice"))
    manifest=build(args.out)
    print(f"NAMO.MOD: {manifest['mod_bytes']} bytes, four orders, 38.4 seconds")


if __name__=="__main__":
    main()
