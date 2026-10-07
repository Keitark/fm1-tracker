# FM1 Buddha Machine: first melody and phrase prototype

This delivers one editable chant study and a pitch-playable phrase for the
existing FM1 MOD tracker. Melody is the priority. It does not change the
firmware, boot song, LCD, or flash. Work is tracked in
[issue 7](https://github.com/Keitark/fm1-tracker/issues/7).

## Repertoire research, 2026-10-08 JST

Recurring titles in the inspected catalogs include:

| Title | Starting point for an FM1 tune |
| --- | --- |
| 南無阿彌陀佛 / 南无阿弥陀佛 | Short repeating six-syllable melodic phrase; first prototype |
| 南無觀世音菩薩 | Another short-name recitation for a later tune |
| 大悲咒 | Longer chant; would need selected short phrases |
| 六字大明咒 | Compact mantra, with multiple different musical settings |
| 往生咒 | Another longer chant found in the catalogs |

This is evidence of recurring catalog repertoire, not a sales or popularity
ranking. A title or chant text does not specify one unique melody.

Sources:

- [Fuji Music FD-221 manufacturer catalog](https://www.ez-songs.com/product-info.asp?id=664):
  several Amitabha settings, Guanyin recitation, Great Compassion mantra,
  six-syllable mantra, and rebirth mantra.
- [First Bird's chant-box app listing](https://play.google.com/store/apps/details?id=com.phoenix.buddhasongs):
  repeats the same five repertoire families.
- [Pure Land Buddhism audio collection](https://www.pure-land-buddhism.com/audio):
  distinguishes several temple chant settings of Amitabha recitation.
- [Basic Buddhist chant teaching book](https://compassion.familyds.org/library/Ebooks/Cabinet_05/52013%E5%9F%BA%E7%A4%8E%E6%A2%B5%E5%94%84%E6%95%99%E5%AD%B8%E8%AA%B2%E6%9C%AC.pdf):
  indexed notation excerpt headed 六字二音 第二種唱韻, around printed page 68.
  The demo uses its short upper/lower melodic contour as a starting point.
  The full PDF server timed out: the source used was the indexed notation
  excerpt, not a visually verified complete score. Durations, phrase ending,
  accompaniment, C tonic, and 100 BPM are this prototype's arrangement.
- [User's 佛如意 video reference](https://www.youtube.com/watch?v=WyK3OKTjydg):
  visual/product reference only. Its specific melody has not been transcribed
  or confirmed, and its recording is not included.

## What was made

`build/buddha/NAMO.MOD` is a self-contained four-channel, four-order tune,
nominally 38.4 seconds before looping. The generated manifest records sample
hashes, exact pattern events, sources, and the generated MOD hash.

The upper phrase begins with scale degrees `5 5 | 3 3 | 2 1`, then a turning
cadence. The answering phrase starts on the tonic and dips to the lower sixth.
The arrangement repeats those two phrases, adding light flute-like doubling
on the second pass. Melismatic Tuo/Fo changes in the tracker use portamento,
so the vowel continues rather than retriggering a consonant for every note.

| Tracker sample (hex) | Contents |
| --- | --- |
| 01-06 | Na / Mo / A / Mi / Tuo / Fo, each a pitched, looped synthetic vowel |
| 07 | Complete Na-mo A-mi-tuo-fo phrase, one shot |
| 08 | Original flute-like tone |
| 09 | Original wooden percussion |
| 0A | Original bell |
| 0B | Original plucked-string-like tone |

The voice is a **synthetic prototype**, derived from generic Yunxi Mandarin TTS
syllables with a sampled consonant onset and harmonic vowel resynthesis. It is
not a recording of a monk, not an imitation of a named performer, and not an
authentic human chant performance. Input characters 拿/摩 are pronunciation
proxies for the intended Na/Mo syllables; 阿/弥/陀/佛 supply the remainder.
Pronunciation, musical phrasing, and timbre still need listening acceptance.
No commercial recording was copied. Generated voice files are build outputs,
not part of the existing CC0 breakbeat bank.

## Play and edit on the existing tracker

After loading the MOD through the existing USB song workflow:

1. Use PRESETS to choose **07**, then press piano note buttons. Each button
   starts the entire phrase at the corresponding sample playback pitch.
   Root is physical MIDI 48/C3 (shown as `C-2` by the MOD note naming).
   The phrase contains its own melody; another key transposes that entire melody.
2. Choose 01-06 to play a single syllable. These have sustained vowel loops;
   use EDIT to stop an audition. Key release does not gate it on this firmware.
3. PLAY runs the tune. Piano audition remains available during playback;
   it is one additional preview voice, with the next key replacing it.
4. Stop playback to edit the pattern. REC enables note entry. Change the
   existing lead notes, syllable slots, effects, tempo, or order arrangement.
5. Save the edited MOD back to the PC before powering off.

Pitch currently changes playback speed: the complete phrase lasts approximately
9.60 seconds at MIDI 48, 7.60 seconds at MIDI 52, and 6.39 seconds at MIDI 55.
Independent pitch/time control, a phrase-per-key pad mode, and persistent
on-device song saving require additional firmware work.

## Regenerate and validate

The generator requires numpy/scipy and ffmpeg. Fetching new synthetic voice
inputs additionally requires `edge-tts` and network access. Cached inputs avoid
network calls. TTS may change upstream; the manifest hashes distinguish assets.
With the same cached voice inputs the generator is deterministic.

```powershell
python firmware/protracker/samples/buddha/make_demo.py --fetch-voice
cmake --build firmware/protracker/build --config Release --target mod_tests mod_demo_preview
& firmware/protracker/build/Release/mod_tests.exe --probe firmware/protracker/build/buddha/NAMO.MOD
& firmware/protracker/build/Release/mod_demo_preview.exe firmware/protracker/build/buddha/NAMO.MOD firmware/protracker/build/buddha/NAMO-preview.wav
cmake -S firmware/protracker/samples/buddha -B firmware/protracker/build/buddha/host -G "Visual Studio 17 2022" -A x64
cmake --build firmware/protracker/build/buddha/host --config Release
& firmware/protracker/build/buddha/host/Release/phrase_preview.exe firmware/protracker/build/buddha/NAMO.MOD firmware/protracker/build/buddha/phrase-keys-preview.wav
```

The phrase preview renders the actual `fm1_mod_preview_start`/`fm1_mod_render`
path at MIDI 48, 52 and 55 in the mono speaker mix. It asserts non-silent,
unclipped output; exact sample-rate duration bounds; shortening as pitch rises;
unchanged song bytes/dirty state; and audition while the song continues playing.
The main preview uses the existing stereo host renderer.

Evidence level: **host validation**. No candidate firmware packaging, device
upload, flashing, physical key test, or audible hardware acceptance was done.
