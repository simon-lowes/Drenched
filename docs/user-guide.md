# Drenched user guide (friends beta)

Thank you for testing Drenched. It is a convolution and hybrid reverb: it plays real recorded rooms, lets you reshape them, and can hand the tail over to a "living" reverb that moves and never repeats. This beta is the full product. There is no trial and no licence check, so nothing needs activating.

This guide covers every control and window so you can find everything and, ideally, break it. Please tell us when you do (see [Reporting bugs](#reporting-bugs)).

## Installing

Drenched comes as VST3, Audio Unit (macOS only), CLAP and a standalone app. There is no AAX (Pro Tools) version in this beta, and no VST2.

You need macOS 11 or later (Apple Silicon or Intel), or Windows 10 or 11 (32 or 64-bit).

### macOS

1. Open the .pkg installer. It is unsigned, so right-click it and choose **Open**, then **Open** again. If macOS still refuses, go to System Settings > Privacy & Security and click **Open Anyway**.
2. Choose **Customise** if you want to leave out any of the Audio Unit, VST3, CLAP or the app.

The plug-ins go in `/Library/Audio/Plug-Ins` (Components, VST3 and CLAP), the app in `/Applications`, and this guide in `/Library/Application Support/Drenched`.

The first time the app uses your audio input, macOS asks whether Drenched may use the microphone. Choose **Allow**, or the app hears silence. You can change it later in System Settings > Privacy & Security > Microphone.

If you were sent a plug-in on its own rather than the installer, clear the quarantine flag before loading it, for example:

```sh
xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/Drenched.vst3
```

### Windows

Run the setup program. It is unsigned, so when Windows warns you, choose **More info > Run anyway**. You can install everything or pick the VST3, CLAP and app.

Drenched's window runs in Microsoft's WebView2 runtime, which most Windows 10 and 11 PCs already have. If yours doesn't, the last page of the installer offers Microsoft's download, and the Drenched window says so too, with a button to get it. Install it, then close the Drenched window and open it again.

On 64-bit Windows it installs both the 64-bit and 32-bit plug-ins, each where its kind of host looks (`C:\Program Files\Common Files` and `C:\Program Files (x86)\Common Files`). The app and this guide go in `C:\Program Files\Drenched`, and the Start menu has a shortcut to the guide.

### Finding it in your host

If Drenched doesn't appear, rescan plug-ins in your host. Logic Pro lists it under Audio Units > Drenched.

## First look

A new instance opens on the first recorded space, St Paul's Hall, with the decay graph showing that room's measured decay. The default look is **Aperture**; there are eleven others (see [Looks](#looks)), and every one has the same controls.

The screen has four parts:

- **The space**: its name and a picture or drawing. Click its description to open the Spaces browser; the arrows beside it step through spaces.
- **The decay graph**: decay time in eight octave bands, 63 Hz to 8 kHz.
- **The main controls**: Mix, Decay, Pre-delay, Size, Brightness, Early/Late and Distance, plus **Freeze**, **Match a room**, **All parameters**, **Wet EQ** and **Waterfall** (some looks say More, Match or EQ).
- **The strip along the bottom**: a hint line, the **In** and **Out** meters and a **Beta** label.

The hint line explains whatever you last touched, with its value and the captured space's. A small dot beside a control means it differs from the captured space.

### Working the controls

- Drag any control up or down. Hold Shift for fine steps.
- Click a value to type one. You can type "off" where a control has an Off, "centre" or "20% left" for the positions, and "12k" for frequencies. A decimal comma ("1,5") works as well as a point, and you can type in another unit: "0.1 s" for Pre-delay, "800 ms" for Decay.
- With a control focused, the arrow keys nudge it, Page Up and Page Down take bigger steps, and Home and End go to the ends.
- Escape closes any window.
- Drag the corner to resize the window. It keeps its shape, and the size is saved with the session.

### Studio and Stage

The **Studio** and **Stage** buttons at the top switch views (the Catalogue look calls them Day and Night). Studio leads with the space and its decay. Stage is for post: large readouts for RT60, Pre-delay, Early, Tail and Mix, output meters, and the main controls in a list. Stage has no Spaces browser button; step through spaces with the arrows, or switch back to Studio.

## Choosing a space

### The Spaces browser

Click the space's name. In Aperture, click the line under the name (its place and volume); some looks have a button such as **All spaces** or **Browse the collection**. There are 46 spaces: 42 recorded and 4 modelled.

| Kind | Count | For example |
| --- | --- | --- |
| Halls | 16 | St Paul's Hall |
| Churches | 9 | York Minster |
| Rooms | 5 | Genesis 6 Studio, Live Room |
| Industrial | 4 | Spring Lane Stairwell |
| Outdoors | 3 | Troller's Gill |
| Underground | 5 | Gill Heads Mine |
| Modelled | 4 | Grand Hall, Stone Chapel, Car Park Level 2, Live Room B |

Modelled spaces are synthesised from a room's size and decay rather than recorded.

- **Search** by name, place, kind ("church", "tunnel") or microphone ("ORTF").
- **Kind buttons** filter the list and show their counts. **Your IR** appears once you have loaded a file.
- Each row shows the RT60, the volume and eight small bars of decay by band.
- **Similar**: the button at the end of a row lists the spaces that sound most like it, closest first, saying how each differs (longer, darker, bigger). **Similar to this space** does it for the current space; **Sounding like ...** returns to the whole list.
- Keys: arrows move, Enter picks, Shift+Enter lists the spaces like the highlighted one.

Along the bottom of the browser are the tools: **Load your own IR...**, **Capture a space...**, **Reverb speakers...**, **Wet EQ...**, **Waterfall...**, **Scenes...** and **Recording credits...**.

### What changes when you change space

The new room brings its own decay curve and pre-delay, and Size goes back to 100%. Everything else stays. To keep yours, use the lock buttons beside the controls (Mix has one in every look; Stage view and the Section, Contact and Folio looks show more). A locked Decay keeps the overall length by scaling the new room's curve to match, and locked Pre-delay and Size keep your values.

### Captured positions

Three rooms were recorded from several places or with several microphone setups: St Paul's Hall (five recordings), Detmold Konzerthaus (two) and WDR Großer Sendesaal (two). Their positions are listed with the space, with the distance from the stage where known and the microphones used. Click one to move there; the old position rings out as the new one takes over.

### Your own impulse responses

Choose **Load your own IR...** in the browser, or drop a file anywhere on the window. Drenched reads WAV, AIFF, FLAC, SOFA and .amb files: mono, stereo, true stereo (four channels), surround up to 9.1.6, or B-format.

- The file is resampled to the session rate and its decay is measured into the graph, so you can reshape it, and use Size, like any other space. Anything past 30 seconds is cut.
- **B-format** (first-order ambisonics) is recognised by its metadata, the .amb type, or "AmbiX", "B-format" or "FuMa" in the name. Higher-order AmbiX plays at first order. Any other four-channel file is true stereo.
- **Surround files** play their own channels on a surround track and fold down on a stereo one.
- **SOFA** files play a pair of sources about 30 degrees either side of the listener as true stereo, or else the source closest to straight ahead. Ambisonic SOFA plays as B-format. The limit is 512 MB.

The session remembers where the file is, not the audio. A dropped file is copied to **Documents/Drenched/Imported** first, since the window can't tell where it came from, and the session names the copy; files up to 512 MB can be dropped. If you move a file, Drenched looks for it where it most likely went; if it can't find it, the space says "File not found": choose **Find** *name*... in the browser, or drop the file on the window, and your settings stay as they were.

## Shaping the sound

### Main controls

| Control | What it does | Range | Default |
| --- | --- | --- | --- |
| Mix | Balance of dry and wet | 0 to 100% | 30% |
| Decay | Scales the whole decay curve; reads the average of 500 Hz and 1 kHz. Not a host parameter: hosts see the eight bands | 0.2 to 12 s | The space's |
| Pre-delay | Gap before the first reflection | 0 to 250 ms | The space's |
| Size | Makes the room bigger or smaller by moving its reflections later or sooner, keeping its decay and tone | 50 to 150% | 100% |
| Brightness | Tilts the wet tone darker or brighter | -12 to +12 dB | 0 dB |
| Early/Late | Balances first reflections against the tail; shown as early / late | 0 to 100 | 55 (45 / 55) |
| Distance | Moves the listener away from the source, shown in metres. Where a recording says where its microphones stood, the default lands there | 0 to 100% | 40% |
| Freeze | Holds the tail until you release it | On, Off | Off |

### All parameters

Click **All parameters** (or **More**) to open every control in place, with the space and graph still in view. The panel also has a **Look** button for the skin picker. Its last row, Early pattern, only reports that the early reflections are as captured.

**Room and position**

| Control | What it does | Range | Default |
| --- | --- | --- | --- |
| Listener position | Moves you across the room, left wall to right | 50% left to 50% right | Centre |
| Source position | Moves the sound across the stage | 50% left to 50% right | Centre |

In the **Section** look, the plan is a room map: drag R1 (you) up and down for Distance and sideways for Listener position, and drag S1 (the sound) sideways for Source position. The **PLAN** and **SECTION** buttons switch drawings.

**Tail and living tail**

| Control | What it does | Range | Default |
| --- | --- | --- | --- |
| Living tail | Hands the late reverb to a living one fitted to the same room, at the same level, keeping the early reflections | Off to 100% | Off |
| Movement | Lets the living tail drift slowly, like air moving. Turn up Living tail to hear it | Still to 100% | 20% |
| Movement rate | How fast it drifts | 0.05 to 5 Hz | 0.70 Hz |
| Crossover | Where Freeze stops holding: below it a frozen room keeps fading, so the bass never builds up | 100 to 1000 Hz | 320 Hz |

**Envelope**

| Control | What it does | Range | Default |
| --- | --- | --- | --- |
| Reverse | Plays the room backwards, so it swells into each sound | On, Off | Off |
| Gate | Cuts the room off after this long, like a gated drum room. The living tail rests while it's on | Off to 2000 ms | Off |
| Attack | Fades the room in over this long, so it blooms behind the sound | Off to 500 ms | Off |
| Pre-delay sync | Rounds Pre-delay to the nearest whole number of 64th notes at the host tempo | On, Off | Off |

**Direct sound** (the first sound to reach you, before any reflection)

| Control | What it does | Range | Default |
| --- | --- | --- | --- |
| Direct level | Its level; Off leaves only the room | Off (-60 dB) to +6 dB | 0 dB |
| Direct delay | Moves it later than the room | 0 to 50 ms | 0 ms |
| Direct colour | Tilts it darker or brighter, leaving the room alone | -12 to +12 dB | 0 dB |

**Stereo and surround**

| Control | What it does | Range | Default |
| --- | --- | --- | --- |
| Width | Narrows or widens the wet signal | 0 to 200% | 100% |
| Cloud size | Lets the reverb follow where the sound is panned. 100% fills the room; lower gathers it around the sound | 0 to 100% | 100% |
| Cross-feed | How much each input side reaches the opposite side. At 0 the left and right get separate reverbs | 0 to 100% | 100% |
| Mono below | Sums the wet signal to mono under this frequency | Off to 500 Hz | Off |

**Output**

| Control | What it does | Range | Default |
| --- | --- | --- | --- |
| Wet low cut | Removes low end from the wet signal | 20 to 1000 Hz | 80 Hz |
| Wet high cut | Removes top end from the wet signal | 1 to 20 kHz | 14 kHz |
| Ducking | Turns the tail down while the dry signal plays | Off to 100% | Off |
| Output | Trims the plug-in's output | -24 to +12 dB | 0 dB |
| Low CPU | See [Low CPU and latency](#low-cpu-and-latency) | On, Off | Off |

## The decay graph

Each point is one octave band's decay time, from 0.1 to 20 seconds.

- Drag a point up or down, or type a value in the box for its band.
- With a point focused, Left and Right move between bands, and Up and Down change it by 0.1 s (0.01 s with Shift).
- The Decay control scales all eight at once.

### Left and right unlinked

Click **Link** to unlink the two sides. Each side then keeps its own curve: choose **Left** or **Right** to pick the one you edit, and the other is drawn faintly behind it. Unlinking starts the right side on the curve it was already playing, so nothing changes until you drag. On surround, side and rear speakers follow their side and centre speakers play the average. Click **Link** again and both sides go back to the left curve.

## Windows

### Wet EQ

Six bands on the reverb alone, after the wet low and high cuts: a low shelf, four bells and a high shelf. The dry sound is not touched, and a band at 0 dB does nothing, so it starts flat. When any band is in use, the Wet EQ button shows a small marker.

- Drag a point sideways for frequency and up or down for gain. Scroll over a bell, or drag it with Alt held, to change its Q. Shift gives fine steps.
- Double-click a point to return it to 0 dB.
- The table below the curve lets you type Frequency, Gain and Q for each band (the shelves have a fixed slope).
- **Reset to flat** zeroes every band.
- The live spectrum of the reverb is drawn faintly behind the curve.

### Waterfall

The response you are hearing, measured: **Energy over time** (a ridge per moment in third-octave bands, 0 to -72 dB), the broadband **Decay**, **RT60 by octave** (a dash means a band couldn't be measured) and the **Live spectrum** of the reverb alone, whatever the mix. Drenched only analyses while this window or the Wet EQ is open. After a change it shows "Measuring the new response" until the new one is ready.

### Scenes

A scene stores every setting and the space under a name, up to 32 per session: one per shot or location.

- **Store as scene N** saves the current settings. Type in a name field to rename.
- **Recall** loads it, **Update** overwrites it with the current settings, and **Delete** removes it (the others keep their numbers, so their automation still works).
- Automate the **Scene** parameter to change rooms on the cut. **Off** leaves the settings alone.
- Freeze and Low CPU are not stored; they stay as they are.
- Scenes follow automation within about a twentieth of a second of real time, so bounce in real time when a scene changes the space.

### Reverb speakers

Sets the reverb's level (Off to +12 dB) and delay (up to 100 ms) in each output speaker, with a meter for each: push the rears later and quieter for depth, or line up speakers at different distances. The dry sound is unchanged, and the LFE gets no reverb. **Reset all** clears it. Saved with the session, but not automatable.

### Capture a space

Records a real room as an impulse response. Put Drenched on a track whose input is the room microphone (in the standalone app, choose your interface's input). One or two microphones are used.

| Option | Choices | Notes |
| --- | --- | --- |
| Method | Sine sweep, Hand clap | A sweep is cleanest; a clap needs no speakers |
| Listen for (clap) | 5, 10, 20 s | Clap again if one goes wrong; the best clap is kept |
| Speakers (sweep) | Left only, Left, then right | Left, then right gives true stereo with two microphones |
| Sweep length | 5, 10, 20 s | Longer rises further above background noise |
| Ring-out | 3, 6, 12 s | Make it longer than the room takes to fall silent |
| Sweep level | -24, -18, -12, -6 dB | Start low; aim for a mic peak around -12 dB |

For a sweep, turn input monitoring on and set the speakers to a comfortable level; while it runs, the outputs play only the sweep. For a clap, clap once sharply where the sound should come from, then keep still while the room rings out.

Click **Start capture**; **Stop capture** cancels. The result is saved as a dated WAV in **Documents/Drenched/Captures** and loads straight away as your IR. Drenched tells you if the mic heard almost nothing or a sweep clipped. Changing audio settings mid-capture drops the take.

### Match a room

Hear a room's decay in an ordinary recording. Click **Match a room** and choose a recording made in the room: a voice, a band or a drum kit, with pauses where the sound stops and the room rings on. Use WAV, AIFF or FLAC, at least five seconds long (only the first ten minutes are used).

Drenched sets the decay graph to what it heard, switches to the closest space, and tells you which bands it had to estimate. Then drag the graph to adjust.

### Recording credits

Lists who made each recorded space, its licence, and any changes made to it (such as resampling or trimming).

## Looks

Click the **Drenched** name in any look to open **Choose a look**. Every value stays where it is. The look is saved with the instance and becomes the default for new ones.

| Look | Tag |
| --- | --- |
| Anodised | The slab |
| Section | The drawing sheet (plan and section drawings, with the room map) |
| Catalogue | The plate |
| Field | The plot is the screen |
| Vitrine | The photograph |
| Signal | The poster grid (a pixel room and bar graph) |
| Aperture | The light (the default) |
| Contact | The print |
| Strata | The landscape (decay drawn as ridges) |
| Rotunda | The round (decay drawn as a circle) |
| Material | The surface (colours follow the room's surface) |
| Folio | The spread |

## A/B, locks and Freeze

- **A** and **B** are two snapshots of every setting and the space. B starts as a copy of A; changes go to whichever is selected, so switch back and forth to compare. Freeze is left out. The snapshots are not saved with the session.
- **Locks** hold a value when you change space (see [What changes when you change space](#what-changes-when-you-change-space)). They are saved with the session.
- **Freeze** holds the tail indefinitely while it's on; the hint line says "Frozen". Crossover sets the frequency below which a frozen room keeps fading. Freeze is not stored in scenes.

## Low CPU and latency

Drenched normally runs with no latency. **Low CPU** runs the reverb on about a third of the processing by playing everything about 21 ms late. Your host makes up the delay, so tracks stay in time, but leave it off when you play live through the reverb. It can't be automated, and scenes leave it alone. Bypassing Drenched in your host keeps the same delay, so the dry sound stays in time, and the reverb fades out rather than cutting off.

## Sessions and automation

- Every setting, the space, locks, the view, the look, the speaker settings, the scenes and the window size are saved with the session.
- Sessions from earlier betas load and sound the same. New controls start at the setting that matches how older versions played.
- Bouncing offline, changes to the room (decay, size and the other settings that reshape it) take effect at the moment they are automated, so a bounce sounds the same every time. Rebuilding the room makes a bounce with a lot of room automation take a little longer.
- Every parameter can be automated except Low CPU. Decay is shaped through its eight bands, as hosts see them; the positions, the wet EQ and Scene are all automatable.
- Your own IR files and captures are files on disk, and a session remembers where. If a file has moved, or the session came from another computer, Drenched looks for it in the same place under your home folder, in the folder you last loaded an IR from, and in your captures. If it still can't find it, open **Spaces** and choose **Find** with the file's name: your decay and other settings stay as the session saved them.

## 32-bit notes

Every feature is in both the 32-bit and 64-bit versions, and sessions move freely between them. The strip says **Beta · 32-bit** when the 32-bit version is loaded.

- There is no 32-bit Mac version, and no AAX.
- A 32-bit host has little memory, so long surround reverbs at high sample rates are shortened (stereo and mono never are), and Drenched tells you how far. For example, 7.1.4 plays 27.8 s at 48 kHz and 6.9 s at 192 kHz.
- Use the version that matches your host. Bridge only if you must, for example to keep an old 32-bit session working in a 64-bit host; REAPER bridges by itself. Many other hosts' bridges only handle VST2, which Drenched doesn't offer.

## Reporting bugs

Send reports to whoever sent you the beta. Click **Report a bug** (on the
Beta chip at the top, or in the Spaces window's tools) and Drenched copies
your setup to the clipboard: its version, format, host, system, sample rate,
buffer size, channels, space and look. Paste that into your message, then
add:

- What you did, step by step, what you expected, and what happened.
- If you can, the session, any IR file you loaded, and a screenshot or recording.

## Parameter reference

Names as your host shows them. 63 parameters.

| Parameter | Range | Default |
| --- | --- | --- |
| Mix | 0 to 100% | 30% |
| Output | -24 to +12 dB | 0 dB |
| Pre-delay | 0 to 250 ms | 18 ms (each space sets its own) |
| Size | 50 to 150% | 100% |
| Brightness | -12 to +12 dB | 0 dB |
| Early/Late | 0 to 100% | 55% |
| Distance | 0 to 100% | 40% |
| Freeze | Off, On | Off |
| Pre-delay sync | Off, On | Off |
| Crossover | 100 to 1000 Hz | 320 Hz |
| Wet low cut | 20 to 1000 Hz | 80 Hz |
| Wet high cut | 1000 to 20000 Hz | 14000 Hz |
| Width | 0 to 200% | 100% |
| Ducking | 0 to 100% | 0% |
| Mono below | 0 to 500 Hz | 0 Hz (off) |
| Decay 63 Hz, Decay 125 Hz, Decay 250 Hz, Decay 500 Hz, Decay 1 kHz, Decay 2 kHz, Decay 4 kHz, Decay 8 kHz | 0.1 to 20 s | The space's measured decay |
| Living tail | 0 to 100% | 0% |
| Movement | 0 to 100% | 20% |
| Movement rate | 0.05 to 5 Hz | 0.7 Hz |
| Reverse | Off, On | Off |
| Gate | 0 to 2000 ms (0 is off) | 0 ms |
| Attack | 0 to 500 ms (0 is off) | 0 ms |
| Direct level | -60 to +6 dB (-60 is off) | 0 dB |
| Direct delay | 0 to 50 ms | 0 ms |
| Direct colour | -12 to +12 dB | 0 dB |
| Cross-feed | 0 to 100% | 100% |
| Low CPU | Off, On (not automatable) | Off |
| Cloud size | 0 to 100% | 100% |
| Listener position | 50% left to 50% right | Centre |
| Source position | 50% left to 50% right | Centre |
| Scene | Off, Scene 1 to Scene 32 | Off |
| EQ low frequency | 20 to 20000 Hz | 100 Hz |
| EQ low gain | -18 to +18 dB | 0 dB |
| EQ 2 frequency, EQ 2 gain, EQ 2 Q | 20 to 20000 Hz; -18 to +18 dB; 0.1 to 10 | 250 Hz; 0 dB; 0.9 |
| EQ 3 frequency, EQ 3 gain, EQ 3 Q | as EQ 2 | 700 Hz; 0 dB; 0.9 |
| EQ 4 frequency, EQ 4 gain, EQ 4 Q | as EQ 2 | 2000 Hz; 0 dB; 0.9 |
| EQ 5 frequency, EQ 5 gain, EQ 5 Q | as EQ 2 | 5000 Hz; 0 dB; 0.9 |
| EQ high frequency | 20 to 20000 Hz | 10000 Hz |
| EQ high gain | -18 to +18 dB | 0 dB |
| Decay linked | Off, On | On |
| Right decay 63 Hz, Right decay 125 Hz, Right decay 250 Hz, Right decay 500 Hz, Right decay 1 kHz, Right decay 2 kHz, Right decay 4 kHz, Right decay 8 kHz | 0.1 to 20 s | The space's measured decay (heard only while Decay linked is off) |

In the Wet EQ window, EQ low is the Low shelf, EQ 2 to EQ 5 are Band 2 to Band 5, and EQ high is the High shelf.
