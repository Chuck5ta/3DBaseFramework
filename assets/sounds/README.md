# Sounds

| File | Description |
|---|---|
| `hum_loop.wav` | Machine hum, 2 s, loops seamlessly (used as a 3D sound on the spinning crate) |
| `ping.wav` | Small bell chime (played from the spheres, positioned in 3D) |
| `drone_loop.wav` | Buzzing propeller motor, 1 s, loops seamlessly (the flying drone; you hear the Doppler effect as it passes) |

These sounds were synthesised for this project (sine tones, filtered noise and simple
envelopes; mono, 48 kHz, 16-bit WAV). They are released into the public domain (CC0):
use, change and ship them freely.

Play them with `app.audio().play(core::assetPath("sounds/<file>"))`. Supported formats are
WAV, FLAC, MP3 and OGG Vorbis. For music, put tracks in `assets/music/` and use
`app.audio().playMusic(...)`; music is streamed from disk rather than loaded into memory.
