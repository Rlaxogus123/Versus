# Custom sound requests

Format: OGG Vorbis, 44.1 or 48 kHz, no clipping, little or no silence at the
start. Short mono or stereo files are both suitable.

| Filename | Length | Direction |
| --- | ---: | --- |
| `ui-open.ogg` | 0.12-0.25 s | Soft digital whoosh with a light click; clean, no long tail. |
| `ready-lock.ogg` | 0.18-0.30 s | Positive lock-in snap; firm but not metallic or harsh. |
| `winner-reveal.ogg` | 0.8-1.4 s | Short victory sting with a bright GD-style arcade finish. |
| `execution-impact.ogg` | 0.25-0.50 s | Weighty impact with restrained low end; no distortion. |
| `result-reveal.ogg` | 0.30-0.60 s | Clean glassy chime that resolves the result sequence. |

Place files in `resources/sounds/` using the exact names above. A new build is
required after adding or replacing packaged files.
