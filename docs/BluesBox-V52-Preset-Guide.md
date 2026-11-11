# BluesBox V52 preset guide

V52 is a local audition build for the M-VAVE BlackBox. The goal is to put a focused set of blues sounds under the guitar volume and two footswitches, compare amp and cabinet captures separately, and measure how much DSP is left for better drive and compressor models.

The bank covers Stevie Ray Vaughan, John Mayer, Freddie King, B.B. King, Joe Bonamassa, Gary Moore and Eric Clapton. Song titles are listening references. The notes below identify where we chose substitute gear or effects.

## Using the bank

- Hold **HOME**, open **Footswitches** and choose **Stomp**. Switch A toggles Drive and switch B toggles Delay. The footer shows which switch mode is active.
- The strip above the footer shows the raw guitar input peak. Seven green segments cover most of the range, yellow starts at about −20 dBFS, orange at −12 dBFS and red at −3 dBFS. An actual clip holds the red segment briefly.
- Compare **P46 Opto T4 AB** and **P47 LA2A Plus AB** using the same pick attack and sustained notes. The compressor options also include the previous LA-2A-style model.
- **P48–P51** are USER1–USER4. **P52** is reserved for the saved DSP STRESS test on the audition pedal.

V52 contains locally sourced amp and cabinet captures. Install the local firmware and its matching manifest using the [BluesBox updater](https://testing7b.github.io/bluesbox/#local-build). The hosted installer offers V51. Export your presets with the [web editor](https://testing7b.github.io/bluesbox/editor.html) before changing firmware.

## Preset list

### Stevie Ray Vaughan

Vaughan names Super Reverb, Vibroverb and Dumble amps, Tube Screamer and Vibrotone. CTS/JBL and Two-Rock are voicing choices. Dimension, Plate and small delays are our mix choices.

[Primary reference](https://www.guitarworld.com/features/stevie-ray-vaughan-classic-1988-interview) · [Additional reference](https://www.guitarplayer.com/guitarists/stevie-ray-vaughan-talks-his-number-one-strat-dumble-amps-and-craziest-gigs)

| Slot | Preset | Tone and song reference | Amp and cabinet | Drive A and delay B | Other effects |
| --- | --- | --- | --- | --- | --- |
| P01 | PRIDE & JOY | Saved V51 shuffle reference. *Pride and Joy* | Super Edge V51 fitted / V51 CTS-derived cabinet model | TS9 · on / Off | Dimension mode 3 · mix 25 · Plate · mix 20 |
| P02 | RIVIERA | Saved V51 wide clean reference. *Riviera Paradise* | Super Clean V51 fitted / V51 CTS-derived cabinet model | TS9 · off / Analog · 535 ms · mix 12 | Opto T4 · Dimension mode 3 · mix 25 · Plate · mix 24 |
| P03 | SRV SNAP | Bright 4×10 clean with strong pick definition. *Love Struck Baby* | Super Clean A2 / CTS 4×10 IR | TS9 · off / Analog · 265 ms · mix 12 · off | Spring · mix 14 |
| P04 | SRV BREAKUP | Guitar volume controls a lightly breaking Super. *Pride and Joy / Rude Mood* | Super Edge A2 / CTS 4×10 IR | TS9 · off / Analog · 265 ms · mix 12 · off | Spring · mix 14 |
| P05 | SRV BIG LEAD | Firm low end and compressed Tube Screamer bite. *Texas Flood / Testify* | Super Crunch A2 / JBL E130 IR | 808 Circuit · on / Analog · 265 ms · mix 12 · on | Dimension 3 / mix 25 · Plate · mix 16 |
| P06 | SRV ROTARY | A slowly moving speaker with room around it. *Cold Shot* | Super Clean A2 / JBL E130 IR | TS9 · off / Analog · 265 ms · mix 12 · off | Slow Rotary · Spring · mix 14 |
| P07 | SRV STEEL | Clean high-headroom lead with a boost on A. *Lenny / Little Wing* | Two-Rock TS1 A2 / JBL-derived fitted model | Clean Boost · off / Analog · 265 ms · mix 12 · off | LA-2A Plus · Dimension 3 / mix 25 · Plate · mix 16 |

### John Mayer

Rene Martinez documents Two-Rock, Dumble, Bassman, Celestion 2×12, Bluesbreaker, TS10, Klon, Katana, TimeFactor and Aqua Puss. TS1/Fuchs cab approximate the amp and speaker family.

[Primary reference](https://www.premierguitar.com/gear/rig-rundown-john-mayer) · [Additional reference](https://www.musicradar.com/news/john-mayer-interview-251957)

| Slot | Preset | Tone and song reference | Amp and cabinet | Drive A and delay B | Other effects |
| --- | --- | --- | --- | --- | --- |
| P08 | JM ROUND | Rounded clean, strong bass definition. *Gravity* | Two-Rock TS1 A2 / Fuchs 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | LA-2A Plus · Spring · mix 14 |
| P09 | SLOW DANCE | Saved V51 mid-cut lead reference. *Slow Dancing in a Burning Room* | Two-Rock TS1 A2 / V51 JBL-derived cabinet model | TS808 Japan · on / Analog · 473 ms · mix 12 | Opto T4 · Plate · mix 16 |
| P10 | JM TRIO | Glassy rhythm with a little Bluesbreaker grit. *Who Did You Think I Was* | Two-Rock TS1 A2 / Fuchs 2×12 IR | Bluesbreaker-style · on / Analog · 265 ms · mix 12 · off | Room · mix 10 |
| P11 | JM KLON | Open upper mids and a restrained lead echo. *Vultures / Belief* | Twin Vibrato A2 / Oxford 2×12 IR | Klon-style · on / Analog · 265 ms · mix 12 · on | Plate · mix 16 |
| P12 | JM WIDE | Soft clean chords with dimension widening. *Wild Blue / Last Train Home* | Two-Rock TS1 A2 / Fuchs 2×12 IR | Bluesbreaker-style · off / Tape · 302 ms · mix 12 · off | Dimension 3 / mix 25 · Plate · mix 16 |
| P13 | JM BLOOM | Tweed attack with controlled sustain. *I Don’t Need No Doctor* | Bassman Clean A2 (simulated head capture) / Jensen C12NA 2×12 IR | TS9 · on / Analog · 265 ms · mix 12 · off | LA-2A Plus · Spring · mix 14 |
| P14 | JM FILTER | Touch-sensitive clean funk. *I Don’t Trust Myself* | Twin Clean A2 / Oxford 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Plate · mix 16 · Touch filter |

### Freddie King

Rock Hall documents Goldtop, ES-345 and pick attack. Fender high-headroom and tweed sounds here are listening-based proxies; these sources do not establish exact amps or effects for the songs.

[Primary reference](https://rockhall.com/wp-content/uploads/2024/03/Freddie_King_2012.pdf) · [Additional reference](https://www.premierguitar.com/gear/joe-bonamassa-and-the-three-kings)

| Slot | Preset | Tone and song reference | Amp and cabinet | Drive A and delay B | Other effects |
| --- | --- | --- | --- | --- | --- |
| P15 | FK HIDEAWAY | Dry, cutting instrumental attack with a short room. *Hide Away* | Bassman Clean A2 (simulated head capture) / Jensen P12Q 1×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Room · mix 10 |
| P16 | FK STUMBLE | Firmer treble and exposed single-note snap. *The Stumble* | Twin Clean A2 / Oxford 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Room · mix 10 |
| P17 | FK SAN HO Z | Bouncy rhythm and quick speaker breakup. *San-Ho-Zay* | Bassman Driven A2 (simulated head capture) / Jensen C12NA 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Spring · mix 14 |
| P18 | FK BIG 410 | Big live 4×10 response, bite without extra gain. *Have You Ever Loved a Woman* | Super Clean A2 / Jensen P10R 4×10 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Spring · mix 14 |
| P19 | FK CANNON | Hard attack and fat low-mid crunch. *Going Down* | Super Crunch A2 / CTS 4×10 IR | Bluesbreaker-style · off / Analog slap · 94 ms · mix 12 · off | Room · mix 10 |
| P20 | FK PALACE | Rounded semi-hollow lead with just enough sustain. *Palace of the King* | Blonde Bassman DI A2 / Jensen C12NA 2×12 IR | Clean Boost · off / Analog slap · 94 ms · mix 12 · off | LA-2A Plus · Plate · mix 16 |
| P21 | FK LIVE PUSH | Bright Texas drive; use A for extra push. *I’m Tore Down* | Twin Vibrato A2 / Jensen P12Q 1×12 IR | 808 Circuit · off / Analog · 265 ms · mix 12 · off | Spring · mix 14 |

### B.B. King

King interview documents the Lucille/Lab Series setup. The L5 amp head is paired with a separately captured Oxford 2×12 or Fuchs 2×12. The cabinet pairings are voicing choices.

[Primary reference](https://guitar.com/features/king-of-the-blues-a-conversation-with-b-b-king/) · [Additional reference](https://www.tone3000.com/tones/lab-series-l5-100w-212-70545)

| Slot | Preset | Tone and song reference | Amp and cabinet | Drive A and delay B | Other effects |
| --- | --- | --- | --- | --- | --- |
| P22 | BB LUCILLE | Round neck-pickup clean, restrained room. *The Thrill Is Gone* | Lab Series L5 A2 / Oxford 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | LA-2A Plus · Room · mix 10 |
| P23 | BB REGAL | Dryer upper-mid cut and warm lower strings. *Every Day I Have the Blues* | Twin Vibrato A2 / Oxford 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Room · mix 10 |
| P24 | BB GLASS | Firm clean response with a short spring. *Sweet Little Angel* | Lab Series L5 A2 / Fuchs 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Spring · mix 14 |
| P25 | BB BLOOM | Gentle post-cab levelling for long bends. *How Blue Can You Get* | Lab Series L5 A2 / Oxford 2×12 IR | Clean Boost · off / Analog slap · 94 ms · mix 12 · off | LA-2A Plus · Plate · mix 16 |
| P26 | BB TWEED | Softer, smaller early-electric-style clean. *Three O’Clock Blues* | Bassman Clean A2 (simulated head capture) / Oxford 1×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Room · mix 10 |
| P27 | BB LATE CLUB | Open clean with less top end and a little space. *When Love Comes to Town* | Two-Rock TS1 A2 / Fuchs 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Spring · mix 14 |

### Joe Bonamassa

Bonamassa describes loud tweed Twins and Bassmans. British and boutique voices here cover lead and mixed-rig roles, with echoes set by ear for this bank.

[Primary reference](https://reverb.com/news/reverb-interview-joe-bonamassa-discusses-his-new-record-blues-of-desperation) · [Additional reference](https://www.jbonamassa.com/features/2013/bonamassa_premier_guitar_rig_rundown/)

| Slot | Preset | Tone and song reference | Amp and cabinet | Drive A and delay B | Other effects |
| --- | --- | --- | --- | --- | --- |
| P28 | JB TWEED | Big Bassman attack with controllable clean boost. *Blues of Desperation* | Bassman Clean A2 (simulated head capture) / Jensen C12NA 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Spring · mix 14 |
| P29 | JB CRANKED | Compressed tweed edge under a humbucker. *Mountain Climbing* | Bassman Driven A2 (simulated head capture) / Jensen C12NA 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Room · mix 10 |
| P30 | JB BRIT LEAD | British midrange plus a low analog repeat. *Sloe Gin* | JTM45 Cleanish A2 / Greenback 4×12 IR | Klon-style · on / Analog · 265 ms · mix 12 · on | LA-2A Plus · Plate · mix 16 |
| P31 | JB SMOOTH | Boutique sustain and softer upper mids. *A Place in My Heart* | Two-Rock TS1 A2 / Fuchs 2×12 IR | Klon-style · on / Analog · 265 ms · mix 12 · on | LA-2A Plus · Plate · mix 16 |
| P32 | JB CLUB | Tight bright rhythm with a footswitch boost. *Driving Towards the Daylight* | Blonde Bassman DI A2 / Jensen C12NA 2×12 IR | Clean Boost · off / Analog slap · 94 ms · mix 12 · off | Room · mix 10 |
| P33 | JB DOUBLE | Warm, slightly wider clean under an optional boost. *Dust Bowl* | Twin Vibrato A2 / Oxford 2×12 IR | Clean Boost · off / Tape · 302 ms · mix 12 · off | Dimension 3 / mix 25 · Plate · mix 16 |

### Gary Moore

Moore recalls a clean JTM45 reissue into a Guv’nor for the lead sound and identifies Vox and Fender on later recordings. The JTM45 and Vox are amp head captures. M77 OD substitutes for the Guv’nor.

[Primary reference](https://www.guitarworld.com/features/gary-moore-blues-rock-guitar) · [Additional reference](https://www.vintageguitar.com/3481/gary-moore-3/)

| Slot | Preset | Tone and song reference | Amp and cabinet | Drive A and delay B | Other effects |
| --- | --- | --- | --- | --- | --- |
| P34 | GM SUSTAIN | Dense singing lead with a short plate and echo. *Still Got the Blues* | JTM45 Cleanish A2 / Greenback 4×12 IR | M77 OD (Guv’nor role proxy) · on / Analog · 265 ms · mix 12 · on | LA-2A Plus · Plate · mix 16 |
| P35 | GM CRUNCH | Gritty bridge pickup with a clipped attack. *Walking by Myself* | JTM45 Beano A2 / Greenback 4×12 IR | Bluesbreaker-style · on / Analog · 265 ms · mix 12 · off | Room · mix 10 |
| P36 | GM GREENY | Softer centre and bright edge for Greeny-style attack. *Parisienne Walkways* | JTM45 Beano A2 / Greenback 4×12 IR | Klon-style · on / Tape · 302 ms · mix 12 · on | LA-2A Plus · Plate · mix 16 |
| P37 | GM VOX EDGE | Open chime breaking up on harder chords. *Thirty Days* | Vox AC30 CH A2 / Alnico Blue 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Room · mix 10 |
| P38 | GM FENDER | Bright Fender counterpoint to the British lead. *King of the Blues* | Twin Clean A2 / Oxford 2×12 IR | 808 Circuit · off / Analog slap · 94 ms · mix 12 · off | Spring · mix 14 |
| P39 | GM BALLAD | Darkened sustain with a single repeat available. *One Day* | Two-Rock TS1 A2 / Fuchs 2×12 IR | Bluesbreaker-style · on / Analog · 265 ms · mix 12 · off | LA-2A Plus · Plate · mix 16 |

### Eric Clapton

Yardbirds Tele/AC30, Bluesbreakers Marshall 1962 combo, Cream Marshall stacks and later tweed Twins are distinct eras. The AC30 and JTM45 are separate amp head captures. The Greenback is a 4×12 pairing, and song examples indicate voicing.

[Primary reference](https://www.guitarworld.com/features/eric-clapton-blues-power) · [Additional reference](https://whereseric.com/faq/eric-claptons-guitar-amp-and-equipment-set-historical/)

| Slot | Preset | Tone and song reference | Amp and cabinet | Drive A and delay B | Other effects |
| --- | --- | --- | --- | --- | --- |
| P40 | EC YARDBIRDS | Bright Vox-style chime and clipped rhythm. *I Ain’t Got You* | Vox AC30 CH A2 / Alnico Blue 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Room · mix 10 |
| P41 | EC BEANO | Edgy British breakup with dry attack. *All Your Love / Steppin’ Out* | JTM45 Beano A2 / Greenback 4×12 IR | Bluesbreaker-style · off / Analog · 265 ms · mix 12 · off | Room · mix 10 |
| P42 | EC WOMAN | Rolled-back top, rich mids, rounded sustain. *Sunshine of Your Love / SWLABR* | JTM45 Beano A2 / Greenback 4×12 IR | M77 OD (Guv’nor role proxy) · on / Analog · 265 ms · mix 12 · off | Room · mix 10 |
| P43 | EC TWEED | Lively late tweed rhythm with optional boost. *Before You Accuse Me* | Bassman Clean A2 (simulated head capture) / Jensen C12NA 2×12 IR | Clean Boost · off / Analog · 265 ms · mix 12 · off | Spring · mix 14 |
| P44 | EC STRAT | Relaxed clean rhythm, short spring and optional slap. *After Midnight* | Twin Vibrato A2 / Oxford 2×12 IR | Clean Boost · off / Analog slap · 94 ms · mix 12 · off | Spring · mix 14 |
| P45 | EC JOURNEY | Widened clean with a low tape echo on B. *Old Love / Pretending* | Twin Clean A2 / Oxford 2×12 IR | Klon-style · off / Tape · 302 ms · mix 12 · off | LA-2A Plus · Dimension 3 / mix 25 · Plate · mix 16 |
