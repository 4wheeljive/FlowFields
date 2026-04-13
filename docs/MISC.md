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