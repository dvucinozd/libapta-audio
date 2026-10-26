# PF-03: statička izvedivost cijelog puta

Datum: 2026-10-26. **STATIC_INVENTORY_COMPLETE; CURRENT_PATH_NO_GO_AS_IS.**
Produktni izbor novog kandidata i memory/physical kvalifikacija ostaju otvoreni.
Baza: `dee7214603939085d5d34d1919a4582395b08e6b`, `rust-rewrite`.
[Strojni rezultat i identiteti](PRODUCT-FEASIBILITY.json),
[opseg](PRODUCT-SCOPE.md), [budžeti](PRODUCT-BUDGETS.md),
[grid/objava ugovor](PRODUCT-GRID-CONTRACT.md).

Ovo je dopušteni source-only PF-03 prework, ne završeni produktni GO. Ne pokreće
model, package instalaciju, novi corpus pokušaj, trening, quantization ili port.
Izvori su pregledani statički, bez neuralnih importa i čitanja tensor/weight sadržaja.

## 1. Odluka koju činjenice već podržavaju

**Ne portati postojeći host postupak ili odbijeni DD-05 kao gotov M1 algoritam.**
Beat This! final0 ostaje izmjerena offline referenca. BeatNet model1 ostaje
odbijen prema svojem izvornom pokusu. CT-05 ostaje operativno INVALID s glazbenom
evaluacijom NOT_RUN; taj se kandidat ovdje ne proglašava ni glazbeno uspješnim
ni neuspješnim. Trenutačno nema kvalificiranog cijelog native neuralnog puta.

Prepreka nije zaključena iz veličine checkpointa ili Python RSS-a. Postojeći
adapter zadržava cijeli PCM/resamplirani signal i izvodi monolitni host/Torch put.
Samo600s mono float32 signala na22050Hz iznosi52920000 B, više od dokumentirane
33554432 B PSRAM na ciljnom board profilu. Stereo float32 source pri48000Hz
iznosi230400000 B. Takav **neizmijenjeni način zadržavanja ulaza** nije održiv
unutar tog dokumentiranog kapaciteta, čak ni bez modela i ostalih ownera.

To nije dokaz da je svaka drukčija implementacija istog modela nemoguća.
Bounded streaming, druga lifetime organizacija i native backend bili bi stvarni
novi engineering posao, s novom numeričkom/resource kvalifikacijom. Nedostatak
opažene board revizije ili slobodne memorije ne pretvara postojeći host put u GO.

**Preporučeni sljedeći smjer:** zasebno specificirati kompaktan, memory-bounded
P4 kandidat/cijeli put u istom repou, uz Beat This! kao referencu. Nijedan novi
model nije ovdje odabran. Prvo razmotriti postojeći primjeren kandidat i njegov
backend; vlastiti trening, distillation i quantization nisu prešutno odobreni.
Ova promjena istraživačkog opsega treba odluku vlasnika; sama inventura je ne aktivira.

## 2. Točni izvori i granice pregleda

Pinned upstream programi, pročitani kao Git blobs i provjereni prema odabranim
live `.py` datotekama prije/poslije aritmetičkog pregleda:

- Beat This! `b95c8ab0c58c2d9fcfd40508ae8dffbc05ac4f5c`: inference,
  preprocessing, beat_tracker, roformer i minimal postprocessor;
- BeatNet `81cedd4beeb7235262db80969a0c9ce9a48a0ed4`: model, log_spect i PF program;
- LIBAPTA neizmijenjeni `Models.infer`, dense capture/codec/dekoder, native core
  manifests i javni rezultati; consumer fetched
  `4a8c660183910b0950ae7ddabce9e8b395a2f10d` manifests/ugovori.

Samo filesystem metadata točno dviju datoteka: `final0.ckpt`81058141 B i
BeatNet `model_1_weights.pt`1612179 B. Njihov sadašnji sadržaj/digest nije
provjeren ovim korakom, niti je izvršen `torch.load`/pickle/ZIP tensor parsing.
Poznati očekivani checkpoint identiteti ostaju iz originalnih registracija.
Byte size nije broj parametara, uncompressed tensor RAM niti peak aktivacija.
Nema enumeracije ili otvaranja audio/label/logit/old-attempt sadržaja.

## 3. Inventura od PCM-a do izlaza

| Sloj | Izvorna činjenica / izvedeni trošak | Klasifikacija i posljedica |
|---|---|---|
| Source/decode | consumer ima bounded two-pass WAV; MP3/FLAC analysis bridge ostaje otvoren | IMPLEMENTED dio / UNKNOWN kompletni pipeline; ne zadržavati cijeli objekt |
| Host PCM read | `sf.read(...float32,always_2d=True,frames=frames)` čita cijeli zadani prefix | za600s/48k/stereo230400000 B, DERIVED counterfactual; stari30s limit nije povećan |
| Resampling | NumPy channel mean, soxr HQ na22050Hz; `signal2spect` stvara novi Torch f32 tensor |600s mono52920000 B za jedan takav signal; lifetime/copy redesign potreban |
| Frontend | n_fft1024, hop441,128 Slaney mel binova, magnitude/log1p, centrirana geometrija registriranog capturea | source facts; actual native scratch/peak/kernel parity UNKNOWN |
| Cijeli mel | pri registriranoj centriranoj geometriji30001 redak za600s |15360512 B jednog f32 mel tensora, DERIVED; nije kompletni frontend peak |
| Jedan inference chunk |1500 redaka, border6, avoid_short_end, keep_first |768000 B samo ulaznog mel chunka; ostali tensori/weights dodatno |
| Transformer | Conv2D/BatchNorm/GELU, Linear, RMSNorm, rotary attention, SDPA, gates/residuals i heads | native podrška tog grafa nije spojena u pregledani LIBAPTA/M1 dependency/code put |
| Minimal postprocessor |7-row maxpool, logit>0, dedup i mapiranje downbeata na najbliži beat; host thread-pool | potrebno očuvati točno ponašanje ako se portira; nije vlastiti novi grid decoder |
| Dense trace |30001 para f32 s16B headerom za600s |240024 B, DERIVED counterfactual; mala trace datoteka ne znači malen model/DP |
| Stari DD-05 DP |1500*66*4*(8+1) B za jedan najveći meter table |3564000 B povijesni measured/generated payload; algoritam REJECTED |
| Naivno produljen isti DP |30000*66*4*(8+1) B |71280000 B, samo counterfactual linear table račun; ne dizati originalne capove |
| Native/wire/consumer | existing Q32/grid/ownership slojevi i2 banke postoje | PF-01 baseline overlap scenarij898890 B na48k/FPC32768; bez modela i drugih troškova |
| Potpuni result/stack/futures/copies | uključiti retained/pending, decoder/codec/UI/USB i placement | UNKNOWN peak/residual; ne odobravati cijeli32MiB analizi |
| Latencija/progress | current `infer` vraća konačne evente nakon cijelog poziva; nema novog progress ownera | user5s/60s ciljevi APPROVED_TARGET, ne izvršeni PASS |
| Cancel | monolitni Torch/soxr/frontend/postprocessor/DP pozivi nemaju produktni step/max-work callback |50ms nije dokazano; backend/I/O i cleanup moraju imati svoj bounded ugovor |

Legacy runneri su registrirani za30s/1501-row scope. Izračuni za600s ne znače da
se isti CLI može valjano pokrenuti nad10min ili da je postojeći protokol proširen.
To je provjera troška koju bi nedopušteno prosto povećanje limita donijelo.
Nijedan tensor ili DP tablica nisu ovdje alocirani.

## 4. Model i activation račun bez lažne sigurnosti

### Beat This! source-default scenarij

Source defaults su D=512, ff_mult=4, n_layers=6 i head_dim32. Samo dva glavna
FF linear weight matriksa po layeru imaju:

`2 * 512 * 2048 * 6 = 12582912 weights`, odnosno **50331648 B** resident FP32.

To ne uključuje attention/frontend/head/bias/norm weights. Za default shape,
FF hidden tensor jednog1500-row chunka ima12288000 B, a FF matriksi nose
18874368000 MACs po takvom chunku. To nije izmjereno vrijeme na P4.
Naivni dense attention-score scenarij ima144000000 B; nije irreducible lower
bound jer optimized SDPA može izbjeći takvu punu alokaciju.

**Važno:** loader čita `checkpoint['hyper_parameters']` i koristi
`BeatThis(**hparams)` uz podršku small model konfiguracije. Exact final0 hparams
nisu otvoreni/provjereni u ovom scopeu. Zato navedeni source-default brojevi nisu
tvrdnja o stvarnom final0 parameter countu ili njegovom isolated RAM peak-u.
Resident FP32 default scenarij ne stane u dokumentirani32MiB; pitanje drukčijeg
checkpoint-config/backend/weight-streaming dizajna ostaje zasebno i nekvalificirano.

### BeatNet model1

Za registrirani BDA(272,150,2), conv kernel10 i conv_out150, izvorna formula daje
402325 parametara; zatvara raniji javni strict-load rezultat. FP32 parametri su
1609300 B, hidden+cell2400 B. To su komponente, ne cijeli frontend/PF/runtime peak.
Model1 je pao sva četiri event/continuity gatea. Manji parametri ga ne vraćaju
u prihvaćeni kandidatski put. PF, frontend, reset i determinism također pripadaju
cijeloj eventualnoj implementaciji, ne samo LSTM-u.

### Host mjerenja ostaju host mjerenja

Originalni Beat This! worker ima388.25s zbroja100 source inference poziva i
515824KiB process RSS; oba reference modela i runtime bili su učitani.
Dense capture ima648132KiB peak cijelog harnessa. Nijedan broj nije isolated
model RAM ni P4 latency. Ne linearno extrapolirati desktop30s rezultat na
desetominutni P4 run ili usporediti2GiB host cap s produktnom rezervom.

## 5. Backend i prikaz su odvojene prepreke

Pregledani native core koristi `libm` i vlastite DSP/wire/session module.
U pregledanim consumer manifests nije spojen neuralni inference backend za ovaj
Torch graph. Ovo ne tvrdi da takav backend ne postoji drugdje u ekosustavu.
Nema odabira, instalacije ili procjene novog paketa u ovom checkpointu.

Offline/bounded lookahead nije sam po sebi zabranjen stopped-deck ugovorom.
Ali mora imati stvarnu bounded memoriju/rad i zadovoljavati5s/60s/50ms ciljeve.
Čak ni uspješan inference port ne zatvara grid kvalitetu i reprezentaciju:
Beat This! OLS ima187/401 joint complete-context prozora uz dvije joint regresije;
DD-05 ima95 actual joint breaks i ostaje odbijen. Event F1 sa70ms tolerancijom
nije dokaz1% outgoing-period točnosti ili pune source continuity.

PF-02 dodatno pokazuje: native EXPLICIT intervali moraju odgovarati selected
tempu, a variable-tempo route treba valjane segmente/dynamic/revision pravila
unutar8 segmenata ili novu izričitu reprezentacijsku odluku. Ne uklanjati taj
validator, ne podizati kapacitete ili ponovno koristiti rejected piecewise fit
samo zato što se novi model lakše uklapa u memoriju.

## 6. Smjer i konkretan paket za sljedeći korak

| Ruta | Odluka ovog pregleda |
|---|---|
| Neizmijenjeni postojeći host/reference put na standalone P4 | NO_GO_AS_IS: source buffers, backend, full-source/step/quality gateovi nisu zadovoljeni |
| BeatNet1 ili DD-05 kao gotov produktni kandidat | NOT_ELIGIBLE prema njihovim sačuvanim verdictima |
| Kompaktan memory-bounded P4 kandidat/cijeli put | PREPORUKA ZA SPECIFIKACIJU; novi model još nije odabran ni odobren za trening |
| Obvezna desktop predanaliza/offload | NOT_SELECTED; izvan potvrđene standalone arhitekture |

Predaja za odluku/stručnu specifikaciju jednog budućeg kandidata:

1. Ulazni ugovor:44100/48000Hz mono/stereo stream, do600s; bounded frontend,
   preprocessing parity ili unaprijed registrirana promjena i točno source mapiranje.
2. Izlazni ugovor: waveform/BPM, beatgrid i3/4 ili4/4 faza; PF-02 Q32,
   full-source/UNKNOWN/capacity pravila i budući reference-free witness.
3. Timing:5s prvi korisni rezultat,60s FINAL,50ms quiescence; separate progress
   path, bounded kernel steps/cancel i source pass/close trošak. Nema placeholder PASS-a.
4. Resource obrazac: weights/storage, FFT/features, temporal state/activations,
   decoder scratch/trace, two banks i pending copies po lifetime/memory class.
   PF-01 stvarne internal/PSRAM rezerve i cleanup rok još treba zatvoriti.
5. Jedna hipoteza: kompaktan bounded-context put može očuvati potrebnu temporalnu
   informaciju za grid/meter uz dogovorene produktne uvjete. Odabrati i specificirati
   JEDAN algoritam/model, sve dimenzije/operatore/prior/decoder pravila i mali oracle
   prije koda. Slabiji model ne izmišlja parametre iz starih rezultata.
6. Najprije public/synthetic kernel i resource preflight prema novoj registraciji;
   musical razvoj i transfer imaju zaseban dopušten scope. GTZAN je izložen,
   formalni holdouti se ne troše radi izbora modela. Nema treniranja u ovom scopeu.

Sljedeća odluka nije još jedan audit istog runtimea. H1 ne rješava ove memorijske,
backend ili glazbene prepreke i ostaje odgođen. Bez odluke o novom kandidatskom
smjeru i potrebnih numeričkih uvjeta ne prelaziti u PF-04 algoritamsku implementaciju.

## 7. Izvršeno i provjereno

Stdlib-only vanjski `static_inventory.py` parsira izvore AST-om, provjerava osam
pinned programskih blobova/live kopija, izvodi gore navedenu aritmetiku i čita
samo metadata dviju točno odabranih weight datoteka. Nema neuralnih importa,
unpicklea ili executiona odabranih izvora. Script i `facts.json` ostaju pod
`D:/AI/LIBAPTA/outputs/product-pf03-20261026/`, hash-bound u receipt JSON-u.
Za reprodukciju smjestiti neizmijenjeni script u novi vanjski direktorij i
pokrenuti ga system Pythonom; `facts.json` nastaje uz script i postojeći rezultat
se ne prepisuje. Source/script/authority provjere ne predstavljaju
novu musical/host-inference/P4 kvalifikaciju. Prethodni testovi nisu ponovno brojani
kao novi. VERSION ostaje1.0.1.
