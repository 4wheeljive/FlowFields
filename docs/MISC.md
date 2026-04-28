## PARTITIONS

ME: Okay. So that seems okay for now. But how about when I enable my LittleFS preset system. In the original AuroraPortal project, I ran into an issue where an attempt to specify a particular csv file cause all saved presets to be lost on every upload. How can I give the P4 the partition settings it needs while retaining the persistent preset function?

CLAUDE:
The key is that your partitions.csv must include a spiffs (or littlefs) partition — LittleFS uses the spiffs partition type under the hood. As long as that partition exists and its offset and size don't change between uploads, the data persists.

The problem you hit in AuroraPortal was likely one of:

The CSV didn't include a data partition at all (so LittleFS had nowhere to live)
Changing partition layouts shifted the data partition's offset, which effectively reformats it
Using huge_app.csv (which has no data partition — all space goes to the app)
Let me check your current P4 partitions.

Read c:\Users\Jeff\Documents\Coding\PlatformIO\Projects\FlowFields\partitions.csv
There's no data/spiffs partition here. When you're ready to enable LittleFS presets, you'll need to add one. For example, replacing or shrinking coredump:


# Name,   Type, SubType, Offset,  Size,    Flags
nvs,      data, nvs,     0x11000, 0x5000,
otadata,  data, ota,     0x16000, 0x2000,
app0,     app,  ota_0,   0x20000, 0x200000,
app1,     app,  ota_1,   0x220000,0x200000,
nvs_key,  data, nvs_keys,0x420000,0x1000,
spiffs,   data, spiffs,  0x421000,0x40000,
coredump, data, coredump,0x461000,0x40000,
That gives 256KB to LittleFS (plenty for JSON presets) and 256KB to coredump (still usable). The rules for persistence:

Never change the offset or size of the spiffs partition after first use — that's what causes data loss
The spiffs SubType is what LittleFS looks for regardless of the partition name
FORMAT_LITTLEFS_IF_FAILED true in your code auto-formats on first boot only
OTA updates (app0/app1) don't touch the data partition — presets survive firmware updates
For the S3, its default partition scheme already includes a spiffs partition, so presets will work there without a custom CSV.


## xAmp/yAmp vs xShift/yShift

ME:
Help me understand exactly what xAmp/yAmp do and how they are distinct from, and interrelate with, xShift/yShift I know that the Amp settings are Perlin noise parameters, and I can observe changes in the display when I change them, but I still don't have a conceptual grasp of their function.

CLAUDE:
Looking at the code, here's how they work together:
xAmp/yAmp control the shape of the noise profile — how extreme the noise values can be. In sampleProfile2D() (line 50-51):
    const float v = n.noise(i * noiseFreq * scale, scrollY);
    out[i] = clampf(v * amp, -1.0f, 1.0f);

The raw Perlin noise value (roughly -0.5 to +0.5) is multiplied by amp. With xAmp = 1.0 (default), the profile values stay in roughly the -0.5 to +0.5 range. With xAmp = 2.0, they'd push toward -1.0 to +1.0 (clamped there). So amp controls the contrast of the noise profile — higher amp means more of the profile hits the extremes, lower amp means the profile stays subtle and gentle.

xShift/yShift control the magnitude of pixel displacement. In noiseFlowAdvect() (line 150):
float sh = yProf[y] * workXShiftCurrent;

The profile value (-1 to +1) is multiplied by the shift value to get actual pixel displacement. With xShift = 1.5, pixels move at most 1.5 pixels per frame. With xShift = 3.0, up to 3 pixels.

The interrelation: The final displacement for a given row/column is profile_value × shift. The profile value's range is shaped by amp. So:

- Low amp, high shift: Most profile values are small (gentle noise shape), but the few that reach the extremes move pixels far. Result: mostly calm with occasional strong movement.
- High amp, low shift: Profile values are pushed to extremes (contrasty shape), but the maximum displacement is small. Result: the flow has strong directional variation across the grid but everything moves slowly.
- Both high: Strong contrast AND big displacement — chaotic, fast motion.
- Both low: Gentle variation AND small displacement — subtle, dreamy drift.

In short: amp shapes the noise curve, shift scales the displacement. Amp is about where on the grid things move differently; shift is about how far they move.


## powf() vs fastpow()

Is there a reason why the dissipation stage in flow_fluid.h uses powf() instead of a fastpow()?
The dissipation powf calls happen once per frame (just two of them — one for velocity, one for dye), with the result used as a scalar multiplier inside the per-cell loop. So the cost is two libm powf calls per frame, totaling roughly 200 ns. Not measurable.

fastpow is designed for hot inner loops — places where it's called per cell (like the Gaussian splat, where I do use it). There it pays off: 432-ish calls per frame × 10-20× speedup = real time saved.

The reason I used fl::powf here:

Consistency with the rest of the project. All the other flows compute their fade factor with fl::powf(0.5f, dt / persistence). Standardizing on the same primitive for once-per-frame fade calculations keeps the dissipation math obviously correct at a glance.

Accuracy matters more than speed at this site. The fade factor is then multiplied into 5 grids (u, v, gR, gG, gB) of every cell. A 5% error in fastpow would systematically bias the decay rate of the entire simulation each frame — slow drift over time vs the user's intended dissipation slider value. Even if visually fine, it'd make the slider less predictable.

fastpow is documented as "for base in [0,1]" with ~5% error. Our dissipation values are in [0,1] so it would work, but the ~5% error compounds frame to frame. fl::powf is exact (modulo FP rounding) and the cost is invisible.

If you wanted to switch it to fastpow, it would still work — the visual effect is unlikely to be noticeable. But there's no FPS to recover by doing so. The cycles are spent inside the Jacobi solver, not in setup math.




## CRGB16

You’re hitting this because CRGB16::r/g/b are fl::u8x8, not float.

CRGB16 is 8.8 fixed-point: each channel stores a 0-255-ish value with 8 fractional bits. Its constructor from float is explicit, so this fails:

leds[idx].r = gR[y][x];   // float -> u8x8 implicit assignment: no
The direct equivalent would be:

leds[idx].r = fl::u8x8(clampf(gR[y][x], 0.0f, 255.0f));
leds[idx].g = fl::u8x8(clampf(gG[y][x], 0.0f, 255.0f));
leds[idx].b = fl::u8x8(clampf(gB[y][x], 0.0f, 255.0f));
or wrapped in a helper, since the clamp still matters. u8x8(float) does not behave like your f2u8; it does not round-to-nearest and does not clamp safely before storing.

The bigger catch: in this FastLED checkout, FastLED.addLeds(...), CLEDController, and the normal WS2812 output path still expect CRGB*, not CRGB16*. So fl::CRGB16 leds[NUM_LEDS] is probably not a drop-in replacement for the actual FastLED output buffer. Once the assignment errors are fixed, you may run into addLeds type errors too.

The practical test shape is probably:

CRGB leds[NUM_LEDS];          // real FastLED output buffer
fl::CRGB16 hdLeds[NUM_LEDS];  // experiment / intermediate buffer
Then fill hdLeds with u8x8(...), do any CRGB16/canvas/HD operations there, and finally demote to CRGB before FastLED.show():

leds[idx].r = hdLeds[idx].r.to_int();
leds[idx].g = hdLeds[idx].g.to_int();
leds[idx].b = hdLeds[idx].b.to_int();
For your current flow-field pipeline, since gR/gG/gB are already float until the final LED copy, switching only the final buffer to CRGB16 probably won’t buy much unless you do additional CRGB16 operations after that copy. WS2812B output is still 8-bit on the wire, so some final quantization step is unavoidable.