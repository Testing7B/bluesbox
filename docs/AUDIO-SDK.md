# Audio via le SDK officiel JieLi (AC79 / WL82) — Apache 2.0

Source : SDK officiel JieLi `fw-AC79_AIoT_SDK` V1.2.0 (Gitee : gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK).
Licence Apache 2.0 → réutilisable dans openpocket (GPL-3.0 compatible dans ce sens).

## Ce qu'on a
- `include_lib/driver/cpu/wl82/asm/iis.h` — API I2S (ALNK) : `iis_open`, `iis_set_sample_rate`,
  `iis_set_enc_data_handler` (entrée), `iis_set_dec_data_handler` (sortie), `iis_irq_handler`.
- `include_lib/driver/cpu/wl82/asm/WL82.h` — carte officielle des registres (ALNK0 0x12E00,
  ALNK1 0x15100, IOMAP 0x51000+, CLOCK 0x10000, ANA 0x11900).
- `cpu/wl82/liba/cpu.a` → `iis.c.o` = **bitcode LLVM** produit par clang 4.0.1 (= notre toolchain).
  Compile tel quel : `clang -target pi32v2 -Os -c -x ir iis.bc -o iis.o` (~5 Ko).
  Dépendances à fournir : malloc/free, memset, gpio_direction_input/output, gpio_set_die,
  gpio_set_pull_up/down, log_print/printf (no-op), log_tag_const_{d,e,i}_AUDIO_IIS (u8 = 0),
  os_current_task_rom, cpu_assert_debug, config_asser, P33_SYSTEM_RESET.
  L'IRQ n'est pas enregistrée par le pilote : à nous d'appeler `iis_irq_handler(0)` depuis l'ISR ALNK0.
- Le pilote règle lui-même la famille d'horloge audio (44.1k / 48k) via CLK_CON2 (0x10014) et ANA.

## Broches I2S (datasheets AC791x du SDK)
| Groupe | MCK | SCLK | LRCK | DAT0 | DAT1 | DAT2 | DAT3 |
|---|---|---|---|---|---|---|---|
| IIS_PORTA (B1) | PA4 | PA5 | PA6 | PA7 | PA8 | PA9 | PA10 |
| IIS_PORTC (A)  | PC0 | PC1 | PC2 | PC3 | PC4 | PC5 | PC6 |

Pocket X : PC0/PC1 = encodeur, PC7 = écran, PA0-PA3 = boutons, PA10 = knob MASTER (analogique)
→ **IIS_PORTA, données sur PA7/PA8/PA9** (DAT3/PA10 inutilisable). Lequel = entrée / sortie : à tester.

## Inconnues restantes
1. Le codec externe de la Pocket X (référence de la puce) : configuration I2C nécessaire ou non ?
   Sur la FM-1 (Felucca), le codec ne demande que des GPIO (pas d'I2C).
2. Quelle ligne DAT = sortie, laquelle = entrée guitare.
3. Lignes de contrôle du codec (reset/mute) sur la Pocket X.

## Plan
- Photo de la carte → référence du codec → datasheet.
- Build de test « AUDIO PROBE » : bip successivement sur DAT0/1/2 (l'écran indique lequel),
  puis vumètre d'entrée par ligne pendant que tu joues → on identifie entrée et sortie à l'oreille.
- Ensuite : passthrough guitare → moteur DSP (`engine/px_dsp.c`) dans le callback I2S.

## Résultats sur la pédale
- **v10 (2026-11-01)** : `I2S OK`. **ENTREE guitare = I2S ALNK0, DAT1 = PA8** (IIS_PORTA, maître, MCLK sur PA4,
  44,1 kHz, 16 bits). DAT0 (PA7) et DAT2 (PA9) inactifs. Aucun son en sortie sur DAT0/DAT2.
- Hypothèse sortie : **DAC interne (broche DACL)** -> ampli de ligne **SGM8905** (qui a une broche d'arrêt),
  comme la carte de référence QFN48 de JieLi (`board_7911B.c` : DACL, hw_channel 0x01, mute ampli).
- **v11** : DAC interne via `sdk/dac.bc` (IRQ_AUDIO = 13) + recherche de la broche d'activation de l'ampli
  (pull-up/pull-down faibles sur 23 broches candidates, HAUT/BAS sur une seule à la fois).
- **v11** sur la pédale : `I2S OK`, `DAC OK`, mais aucun son (broches en Z / pull-up / pull-down), et **les LED
  des boutons restent éteintes** avec openpocket -> le **74HC595** n'est piloté par personne. Hypothèse : il commande
  les LED **et** l'activation de l'ampli SGM8905.
- **v12** : recherche automatique des broches du 595 (DS, SHCP+STCP, OE) par essais aléatoires de rôles,
  en pulls faibles uniquement ; témoin = les LED clignotent ; puis affinage broche par broche (oui/non au footswitch).
- **v12** : la Pocket X n'a pas de LED de knob (LED des boutons seulement) ; recherche 595 jugée trop indirecte.
- **v13** : balayage direct, chaque broche candidate pilotée 3,3 V puis 0 V (1,5 s chacun), une à la fois,
  bip en continu sur le DAC interne : on écoute quand le son sort.
- **v13** sur la pédale : **craquement dans la sortie quand PA9 change d'état** (premier signe de vie de la sortie).
- **v14** : PA9 (AMP) et PA7 (MOD) réglables à la main : libre / 3,3 V / 0 V / données I2S ; balayage des
  autres broches en option ; bip simultané sur le DAC interne et l'I2S.

## ✅ CHAINE AUDIO COMPLETE (v14, 2026-11-01)
| Rôle | Broche | Détail |
|---|---|---|
| MCLK | PA4 | sortie (mclk_output = 1) |
| SCLK / LRCK | PA5 / PA6 | JieLi maître |
| **Entrée guitare** | **PA8 = DAT1** | I2S ALNK0, 24 bits stéréo (16 bits jusqu'à la v25), 44,1 kHz (ADC externe) |
| **Sortie** | **PA7 = DAT0** | I2S ALNK0 -> DAC externe « 344C » -> ampli SGM8905 -> OUT + casque |
| **Activation ampli** | **PA9 = 3,3 V** | GPIO sortie haute (craquement = mise en marche de l'ampli) |
Le DAC interne (DACL) n'est PAS utilisé par la Pocket X. Passthrough guitare -> sortie : propre.
Config pilote : `iis_open` IIS_PORTA, channel_in = BIT(1), channel_out = BIT(0), maître, MCLK, sr_points 128,
puis `iis_set_sample_rate(44100)`, `iis_channel_on`, IRQ ALNK0 = 11 -> `iis_irq_handler(0)`.

## v15 : le moteur DSP dans le chemin audio
- `firmware/px_audio.h` : I2S (in PA8 / out PA7), ampli PA9, moteur `engine/px_dsp.c` dans l'IRQ ALNK0
  (blocs de 128 trames), garde-fou : > 85 % de charge 8 blocs d'affilée -> BYPASS. Charge affichée « CPU xx% ».
- Float sur FPU matériel : `-mcpu=r3 -mfprev1` (options du SDK JieLi pour la WL82). Sans elles, clang émet des
  appels logiciels (`__addsf3`...). `firmware/px_math.c` = mini libm (sin/cos/exp/log/pow/sqrt, erreur ~1e-6).
- Moteur (~250 Ko) dans la zone RAM `POOL` 0x01C20000 (0x54000 o), comme CLIPPER. PX_OVERSAMPLE = 2 pour commencer.
- **v15** sur la pédale : **BYPASS** (moteur > 85 % du temps réel).
- **v16** : moteur optimisé sans changer le son (écart -120 dB vérifié sur PC, 8 presets) : baffle en produit
  scalaire linéaire (historique doublé, déroulé x4), plus de modulo dans delay/reverb, anti-dénormaux ±1e-15.
  + écran de mesures au démarrage (vitesse INT/FLOAT, % temps réel par configuration du moteur).
- **v16** sur la pédale : **CPU 52 %**, temps réel OK. Mais son trop aigu, manque de grave.
- **v17** (son) : mesure sur PC (corde pincée, bandes d'octave) -> bande 100-200 Hz à -20 dB sur 5150/Djent.
  Causes : (1) coupe-bas avant disto cumulés (serrage 200 Hz + étages 90/110/130 Hz) ; (2) baffle = réponse de
  6 filtres TRONQUÉE à 256 points (5,8 ms) -> la bosse de grave ~100 Hz disparaissait.
  Corrections : baffle jouée directement en 6 biquads (réponse exacte, ~8x moins de calcul), coupe-bas d'étages et
  serrage abaissés (5150 : 35/50/70 Hz, serrage 100 Hz), shelf de grave du 5150/Uber monté à 170-180 Hz,
  résonance x10 dB, présence x6 dB, pics de présence des baffles réduits. Résultat : +10 dB à 100-200 Hz.
  Suréchantillonnage de l'ampli remonté à x4 (marge CPU libérée par la baffle).
  NB : le simulateur navigateur (sim/) n'a pas encore ces changements (parité à refaire).

## v18 : écran en SPI matériel + DSP sur le 2e cœur
- **Écran** : SPI1 matériel, groupe A (PB6 = CLK, PB7 = DO, IOMAP_CON1 bit 4 = 0, d'après `spi.h` du SDK),
  registres et CON = 0x4021 repris de `fm1_lcd_hw.h` (Felucca), horloge 20 MHz (BAUD 2), pixels envoyés par DMA
  avec double tampon de 1024 pixels. `LCD_HW 0` dans openpocket.c = retour au SPI logiciel.
- **2e cœur** : `fm1_cpu1.h` de Felucca, démarré au début de app_main. Le bloc N part sur CPU1 à la fin de l'IRQ N,
  l'IRQ N+1 le récupère (même latence). `FM1_CPU1_FALLBACK 1` : en cas d'échec, CPU0 reprend seul.
  Affichage en bas à droite : `C1 xx%` (2e cœur) ou `C0 xx%` (repli). `DSP_CPU1 0` = tout sur CPU0.
- CORDIC du SDK (`tanh_float`) étudié : instruction `copex` + division -> pas plus rapide que notre tanh rationnel.
- **v17 sur la pédale : PLUS DE SON.** Cause trouvée par simulation PC avec NOTRE libm (px_math.c) : `cosf` = sin(x+pi/2)
  avec ~4e-6 d'erreur ; les coupe-bas abaissés (30-35 Hz) calculés à 176,4 kHz (x4) demandent cos(w) avec w ~ 1e-3 :
  biquads instables -> NaN -> silence permanent (pas de BYPASS : le temps de calcul reste normal). Sur PC avec la
  libm standard, invisible.
- **v19** : sin/cos réécrits (réduction à [-pi/4, pi/4], pi/2 en deux morceaux, polynômes séparés) ; 4860 combinaisons
  ampli x baffle x réglages extrêmes testées avec px_math.c : 0 NaN. Filet de sécurité : NaN/Inf détecté en sortie ->
  silence + moteur réinitialisé par la boucle principale. Écran SPI matériel (LCD_HW) et 2e cœur (DSP_CPU1) présents
  mais DÉSACTIVÉS dans v19 (à activer une fois le son confirmé). LEÇON : toujours simuler avec px_math.c, pas la libm.
- **v20** = v19 + écran SPI matériel (LCD_HW 1) + DSP sur le 2e cœur (DSP_CPU1 1). Interrupteurs de repli dans
  openpocket.c : `LCD_HW 0` (écran logiciel), `DSP_CPU1 0` (tout sur CPU0) ; build.sh n'assemble fm1_cpu1.S que si
  DSP_CPU1 vaut 1.

## v21–v23 : contenu
- **v21** : moteur v2 (10 blocs, 13 amplis), UI « chaîne + page bloc » sans clignotement. **v22** : police Inter, plus
  d'écran de mesures au démarrage.
- **v23** : moteur v3, 11 blocs (ajout PITCH et SPECIAL FX), 118 modèles voicés d'après des analyses de circuits ;
  UI en anglais ; valeurs lisibles (`+5TH`, ms, dB). Correctifs trouvés sur PC : nombres dénormalisés en silence
  numérique (le biais des étages d'ampli avalait le décalage anti-dénormaux), décaleurs de hauteur alignés en phase
  (SOLA), delays à saturation normalisés (auto-oscillation seulement au-dessus de FDBK ≈ 90). Banc de mesure
  (`BENCH_AT_BOOT 1`) mis à jour : preset GROOVE METAL (5150 III, 4 étages) et blocs les plus lourds.

## v24 : mise à jour par USB
- USB-MIDI (felucca-bsp `chip/usb.c`, sondé par TIMER5 à 2 kHz) et l'entrée de mise à jour M-UPGRADE (`chip/ota.c`) :
  le chargeur du paquet est mis de côté en 0xE0000 (5 × 4 Kio), puis la pédale redémarre dedans.
- Chargeur (`loader/`) : felucca-bsp adapté à la Pocket X : zone de l'application [0x4000, 0xA3000) comme PocketX_016,
  tête de flash jamais écrite, enregistrements de mise à jour effacés entre 0xA3000 et 0xFF000.
- Paquets (`tools/pxpkg.py`) : reconstruisent la zone de l'application officielle PocketX_016 à l'octet près.

## v25 : presets en flash, baffles et ampli plus fins
- Presets (`firmware/px_store.h`) : 42 emplacements de 96 o, deux copies A/B (0xF0000 / 0xF2000, deux secteurs de
  4 Kio chacune), en-tête « OPPS » écrit en dernier (séquence + CRC16), relecture après écriture. Octets 0xF00..0xF7F
  de chaque secteur toujours effacés (zone lue par le chargeur pour ses enregistrements). Pendant l'écriture :
  sortie muette, IRQ ALNK0 masquée, 2e cœur entre deux blocs (sa boucle tourne en RAM).
- Baffle : FIR de 128 points après les biquads (résonances du cône 1,2–6 kHz tirées par modèle, peigne du micro hors
  axe, réflexion arrière des baffles ouverts), normalisé à l'unité en basses fréquences.
- Ampli : triode asymétrique `u≥0 ? 0.7·tanh(u/0.7) : tanh(u)`, décalage de polarisation par enveloppe (100 ms,
  0,45 pour ≥4 étages, 0,35 sinon), sag aussi sur le préampli (×0,3). `px_trim.h` recalibré (29 amplis à 0,160 RMS).
- Mémoire : 327 724 o sur 344 064 dans POOL.

## v26 : entrée et sortie en 24 bits (souffle des amplis à gros gain)
- Constat : souffle constant (« shhh ») sur les amplis high-gain, même sans câble branché, absent avec le firmware
  officiel. Cause : l'entrée était lue en 16 bits ; le bruit de quantification, multiplié par le gain de l'ampli
  (~60 dB), devenait audible.
- Firmware officiel (PocketX_016, désassemblé) : même ALNK0, mêmes broches, maître, 64 SCLK par trame, mais
  - chaque canal est mis en **24 bits** (fonction 0x0200664A : `CON1 |= 1 << (4*ch + 2)`, le bit `data_width` du SDK) ;
  - LEN = 128 et demi-tampon de 512 o traité comme 64 trames de 8 o : en 24 bits, LEN compte des mots de 32 bits ;
  - entrée lue par `(mot << 8) >> 7` : 24 bits alignés à droite (bit 23 = signe), canal gauche seul, ×2 ;
  - sortie écrite en entiers 24 bits écrêtés à ±8 388 600 sur les deux mots de la trame.
- OpenPocket v26 : `data_width = BIT(0) | BIT(1)`, `sr_points = 256` (128 trames par bloc, comme avant), tas de
  4 096 o pour les tampons du pilote (2 canaux × 8 × 256). Entrée = (G + D) / 2 en 24 bits : même niveau qu'avant,
  les réglages des amplis ne changent pas. Sortie en 24 bits (le volume MASTER ne coûte plus de résolution).
- Diagnostic : l'écran de l'accordeur affiche `IN L -xx R -xx dB`, le niveau de chaque canal d'entrée (dBFS),
  pour vérifier où arrive la guitare et le plancher de bruit.
