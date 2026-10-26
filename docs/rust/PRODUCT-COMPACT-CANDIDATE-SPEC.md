# CS-01: specifikacija kompaktnog P4 kandidata

Datum: 2026-10-26. Baza: `c5a3dea0adfda497add11aba23f8b440a1a0939a`,
grana `rust-rewrite`. Status: **DESIGN_SPECIFIED; BINDING_AND_BUDGET_GATES_OPEN**.
[Strojna specifikacija](PRODUCT-COMPACT-CANDIDATE-SPEC.json),
[produktni opseg](PRODUCT-SCOPE.md), [budžeti](PRODUCT-BUDGETS.md),
[ugovor izlaza](PRODUCT-GRID-CONTRACT.md), [prethodni pregled](PRODUCT-FEASIBILITY.md).

Korisnik je odabrao specifikaciju kompaktnog P4 kandidata prije fizičkih mjerenja.
To dopušta ovaj dizajn i provjeru javnih izvora. Ne odobrava trening, inferenciju,
novi glazbeni pokus, kvantizaciju, implementaciju ili aktivaciju providera.
PF-01 ostaje NEEDS_MEASUREMENT; ovo nije završeni PF-04 protokol ni produktni GO.
Prethodni PF-03 receipt čuva tada otvorenu odluku; aktualnu odluku vodi
[PRODUCT-FIRST-STATUS.json](PRODUCT-FIRST-STATUS.json).

## 1. Jedan kandidat i jedna provjerljiva hipoteza

Predlaže se **CS-01-BNP-MAIN-REGULAR**: jedna objavljena BeatNet+ main grana,
ograničeni frontend, deterministički 3/4–4/4 HMM s checkpointed tracebackom i
jedna globalna pravilna rešetka. Hipoteza: ta cjelina može dati korisnu neuralnu
beat/downbeat evidenciju bez cijelog PCM-a, full-feature tensora ili full-trace
tablice u memoriji. Brzina i glazbena kvaliteta tek se moraju dokazati.

Ovo nije stari BeatNet model1: mijenjaju se weights, četiri umjesto dva LSTM
sloja i frontend1764/288 umjesto1411/272. Stari REJECTED ostaje REJECTED.
DD-05 i njegovi troškovi/traceback ne koriste se. Beat This! final0 ostaje offline
referenca. Ne uspoređuju se tri BeatNet+ grane, seedovi, decoder sweeps ili modeli.
Odabir main grane je projektna odluka za početni DJ profil, ne rezultat mjerenja.

Ovaj kandidat namjerno ne pokušava prikazati promjenjivi tempo jednim pravilnim
gridom. Ako dolje navedeni globalni support test ne prođe, vraća UNSUPPORTED_GRID.
Takav izostanak ulazi u nazivnik i može odbiti cijelog kandidata. Ne smije se
prešutno suziti potvrđeni produktni opseg ili izostaviti neuspješne pjesme.
Drugi model, piecewise fit ili novi grid algoritam zahtijeva novu specifikaciju.

## 2. Vezani izvori i obvezni preduvjeti

BeatNet+ source revision: `bb90eb0a9065b101a4b4c4cb2b2061950266cb4b`.
[Model](https://github.com/mjhydri/BeatNet-Plus/blob/bb90eb0a9065b101a4b4c4cb2b2061950266cb4b/src/BeatNetPlus/model.py),
[inference](https://github.com/mjhydri/BeatNet-Plus/blob/bb90eb0a9065b101a4b4c4cb2b2061950266cb4b/src/BeatNetPlus/inference.py),
[frontend](https://github.com/mjhydri/BeatNet-Plus/blob/bb90eb0a9065b101a4b4c4cb2b2061950266cb4b/src/BeatNetPlus/log_spect.py)
i [config](https://github.com/mjhydri/BeatNet-Plus/blob/bb90eb0a9065b101a4b4c4cb2b2061950266cb4b/src/BeatNetPlus/configs/generic.yaml)
definiraju izvorni graf. Madmom frontend/HMM source veže se na v0.16.1 commit
`6545fbb3ecdae0aac336a95ec591f642e0b74f3e`; ne ovisi o promjenjivom `main`.
JSON veže točne source SHA-256 i Git blob identitete. Pregledani su programi,
metadata stabla i statička struktura jednog checkpointa, bez audio, oznaka ili logita.

Namijenjeni artefakt je `src/BeatNetPlus/models/generic_main_weights.pt`,
Git blob `7aefd5e56ef65482be6f671c3cca8de457dad102`, metadata size3072890 B.
Preuzet je samo taj3.07MB artefakt iz pinned upstream commita radi hash/metadata
provjere. SHA-256 je `5bbae630b4112f3c1193654e3dbc946b850caa06a2a82e10e8de9e48bb673519`;
Git blob, svih22 key/shape/stride/storage-extent zapisa odgovaraju specifikaciji.
Stdlib ZIP/pickle-opcode parser samo bilježi simboličke strukture: nema unpicklea,
poziva pickle GLOBAL/REDUCE callables, neuralnih importa ili tensor deserializacije.
Numeričke vrijednosti/finite weights i trening provenance nisu ovdje provjereni.
Prije buduće inferencije treba zatvoriti i njih. `strict=False` iz upstream wrappera
nije dopušten: missing/unexpected keys, druga grana ili shape mismatch su FAIL.

Izvorni wrapper učitava cijeli audio i vraća prazno polje kod nekih DBN grešaka.
To se ne prenosi. Ne koristiti njegov runtime kao dokaz bounded obrade.
Repo stablo, README i setup ne zatvaraju zasebnu eksplicitnu licencu BeatNet+
koda/weights. Status je LICENSE_UNRESOLVED; ne naslijediti automatski BeatNet
CC-BY ili madmom licencu. Nema redistribucije/ugradnje dok se prava ne utvrde.
Madmom source i model-data licence su odvojene; njegovi RNN weights se ne koriste.

## 3. Ulaz, source vrijeme i ograničeni resampler

Ulaz ostaje mono/stereo44100/48000Hz, `0 < N <= 600*fs`, poznat točan EOF.
MP3/WAV/FLAC codec mora dati identične source frame koordinate i mogućnost close/
cancel; compressed bridge ostaje otvoren. Float32 PCM je konačan, normaliziran
na postojeći PCM ugovor; stereo mono je `(L+R)/2`, bez automatskog gaina/normalizacije.

Za ovaj novi kandidat specificira se **CS-FIR65**, zasebno od starog soxr HQ puta.
To je eksplicitna preprocessing promjena za buduću registraciju, ne tvrdnja o
paritetu s librosa/soxr treningom. Nema korištenja starih resampliranih ulaza kao
da su novi. F64 reference formula, konačni uzorak zaokružen na f32:

```text
K = ceil(N * 22050 / fs)
u(k) = k * fs / 22050; n = floor(u); c = 22050 / fs
sinc(z) = sin(pi*z)/(pi*z), sinc(0)=1
w(z) = c*sinc(c*z)*(1+cos(pi*z/33))/2, za |z| < 33; inače 0
y[k] = sum(j=n-32..n+32, zero_padded_x[j]*w(u-j)) /
       sum(j=n-32..n+32, w(u-j)), k=0..K-1
```

Normalizacijski nazivnik uključuje svih65 koeficijenata i na EOF-u. Ne
renormalizirati samo postojeće uzorke. Nema grupnog pomaka timestampova; formula
koristi budući kontekst do32 source samplea. Za44100Hz postoji jedna faza, za
48000Hz147 faza (`u=320*k/147`). Dvije odvojene F64 tablice zauzimaju76960 B.
To su storage potrebe, ne odobreni RAM. Tablice/implementacija i numerički oracle
moraju biti hash-bound prije bilo kojeg budućeg mjerenja.

Pjesma se čita jednom redom. Ring zadržava samo potrebne uzorke; uzorke proslijedi
postojećem waveform owneru prije oslobađanja. Nema full PCM/mel/feature spremnika.
Za analitički red r, source Q32 centar je `r*fs*2^32/50`; za oba fs je cjelobrojan.
Availability centra razlikuje se od vremena čitanja budućeg konteksta.

## 4. Frontend i neuralni graf

Frontend je pinned `LOG_SPECT` online/offline geometrija, izvedena redak po redak:
22050Hz, hop441, centriran frame1764, symmetric Hann, zero padding na krajevima,
`end='normal'`, `R=ceil(K/441)` i `R<=30000`. FFT je **1764**, ne2048;
Nyquist je izostavljen, magnitude ima882 bina. Native FFT može koristiti drugi
računski backend samo uz isti1764-bin mathematical transform i verificiran parity.
Nije dopušteno promijeniti FFT dimenziju da postojeći radix-2 kernel prođe.

Pinned LogarithmicFilterbank:24 bands/octave, fmin30, fmax17000, fref440,
norm_filters=True, unique_filters=True; zadržati upstream bin-mapping i clipping.
Log je `log10(1+filtered_magnitude)`, ne Slaney mel niti `ln(1+1000*x)`.
Pozitivna razlika je prema `_diff_frames(0.5,441,1764,Hann)`; prvi red ima nule.
Konkatenacija je `[current_log_bands, positive_differences]`.
Neovisna stdlib derivacija pinned formula potvrđuje144+144=288 vrijednosti i
diff_frames=1; JSON čuva točne unique edge binove. Madmom frontend nije izvršen.
Koeficijente i numerički parity s budućim frontend oracleom još treba zamrznuti;
mismatch je STOP, bez crop/pad. Geometry derivation nije izvedeni model shape PASS.

Jedna grana, batch1, jedan red po pozivu, bez resetiranja između chunkova:

| Sloj | Točan oblik / pravilo | Parametri |
|---|---|---:|
| Conv1d |1->2, kernel10, stride1, bez paddinga; izlaz2x279, bias |22 |
| ReLU + MaxPool1d |pool2/stride2, floor; flattened channel-major278 |0 |
| Linear0 |278->150, bias, bez dodatnog activationa |41850 |
| LSTM |4 sloja, input/hidden150, unidirectional, bias ih/hh |724800 |
| Output linear |150->3, bias |453 |
| Softmax |klase beat/downbeat/nonbeat, stabilan subtract-max |0 |

Ukupno **767125 parametara /3068500 B FP32**; source-defined konfiguraciju
potvrđuju pregledani tensor metadata shapes, bez čitanja numeričkih težina.
Hidden+cell su4800 B. LSTM gate order je
i,f,g,o; `c'=sigmoid(f)*c+sigmoid(i)*tanh(g)`,
`h'=sigmoid(o)*tanh(c')`. Oba biasa se pribrajaju. State na begin/reset je nula.
Svaki sloj koristi vlastiti prethodni state; sljedeći sloj dobiva novi h donjeg.
Nema Dropouta, drugog branch modela, demixera, attentiona, ensemblea ili treninga.

Vezani weights keys: conv1 weight/bias (2,1,10)/(2), linear0 (150,278)/(150),
lstm weight_ih_l0..3 i weight_hh_l0..3 (600,150), bias_ih_l0..3 i bias_hh_l0..3
(600), output_linear (3,150)/(3). Puni prefix i shape moraju ostati strict-bound.
Čuvati tri f32 probabilityja po retku, najviše360000 B, samo izvan Git artefakata.

## 5. Deterministički HMM i ograničeni traceback

Ovo je **CS-HMM-40-300**, nova eksplicitna konfiguracija pinned madmom modela:
fps50, meter candidates [3,4], intervals **10..75 uključivo**, linearno svih66,
transition_lambda100, observation_lambda16, threshold0.05, correct=True.
Min/max40/300 i svih66 intervala nisu prešutno upstream defaults55/215/60-tempi.
Svaki meter ima odvojen graph, bez meter promjena. Prvo3 pa4; jednak final score
bira3. Nema PF-a ili RNG-a. F64 score/prior/transition arithmetic ostaje F64;
ne proglašavati f32 ili kvantizirani decoder ekvivalentnim.

State enumeration je `(beat_index=0..m-1, interval=10..75, subframe=0..i-1)`.
Unutar intervala postoji jedini prethodnik s podokvirom q-1 i vjerojatnošću1.
Na q=0 prethodnici su zadnji subframe svakog intervala prethodnog beata mod m.
Za old interval i i novi j, `v=exp(-100*abs(j/i-1))`; vrijednosti<=2^-52 su0,
ostale se F64 normaliziraju po izlaznom i, uz rastući j. Incoming predecessor
liste su sortirane po state ID; nema ovisnosti o SciPy sparse redoslijedu.
Početni prior je uniforman preko S stanja pojedinog metra.

Ako je q/i<1/16, density je p_down za beat_index0, inače p_beat; druga stanja
koriste `p_non/15`. Log(0) je -inf. Nema eps clippinga ili prebacivanja -inf na0.
Korištenje treće softmax klase izbjegava `1-p_beat-p_down` cancellation; to je
explicit numerical delta prema upstream wrapperu i mora imati vlastiti oracle.
Nevaljane vjerojatnosti ili nepostojanje konačnog puta daju eksplicitan status.

Threshold zadržava raspon od prvog do zadnjeg retka s p_beat ili p_down>=0.05;
all-below-threshold daje NO_EVIDENCE. Row0 sam mora biti prepoznat; ne kopirati
upstream `idx.any()` rubni bug. Početni offset ostaje u originalnim koordinatama.
To je decoder support trim, nikad skraćivanje deklariranog source trajanja.

Viterbi recurrence za svaki red/state/predecessor:
`candidate=(previous[p]+log_transition)+log_density`; update samo na `>`.
Tie zadržava prvi predecessor; final state je najniži ID s maksimalnim scoreom.
Nema score normalizacije ni promjene asocijativnosti u checkpoint replayu.

**Traceback, B=256 redaka:**

1. Score oba metra odvojeno samo s dva rolling F64 vektora; odaberi jedan.
2. Ponovi pobjednički forward. Prije svakog bloka sačuvaj cijeli prethodni score
   vektor, uključujući prior prije prvog bloka. Ne čuvaj parent tablicu cijele pjesme.
3. Od zadnjeg bloka prema prvom: vrati njegov sačuvani ulazni vektor; ponovi
   identične operacije i zapiši samo lokalnu `[<=256][S]` u16 parent tablicu.
4. Od poznatog final statea slijedi parents unatrag. State prije bloka postaje
   zadani end state prethodnog bloka. U izlazni path zapiši globalne row indexe.
5. Po zadnjem replayu ne koristiti pointer prvog retka kao dio output patha.
   `S<=11220<65535`; 65535 je invalid sentinel i nikad valjan parent.

Checkpoint i monolitni mali oracle moraju dati identičan path/score, uključujući
ties, neparan zadnji blok i budget prekide. RNN se u decoder replayu ne pokreće.
Iz pobjedničkog patha uzmi maksimalne povezane beat-range intervale (q/i<1/16).
U svakom uzmi row/class s najvećim p_beat ili p_down: raniji row, zatim beat class
dobiva tie. Beat number je path beat_index+1 na tom retku, kao pinned correct path.
Granice početnog/završnog regiona uključi; ništa ne zaokruži na EOF.

## 6. Točan izlazni most: jedna globalna MODEL rešetka

Ovo je novi, strogi whole-source most; nije stari S6 evaluator niti piecewise fit.
Prima samo globalne ispravne row indexe r_j i modelove beat numbers, bez oznaka,
source ID-eva ili reference. Traži `2*m+1 <= L <=3072`, strogo rastuće r_j i
jedan neprekinuti ciklus brojeva1..m. Najraniji downbeat index d određuje fazu.

Za j=0..L-1 izračunaj checked integer sums:
`D=L*sum(j*j)-sum(j)^2`, `U=L*sum(j*r_j)-sum(j)*sum(r_j)`.
Traži D>0 i U>0. Tempo je `T=round_half_up(3000000*D/U)` millibpm, unutar
40000..300000. Koristi exact integer/rational round_half_up, ne binary64 rounding.
Public period `P=floor(fs*60000*Q/T)`, `Q=2^32`.
Raw source `q_j=r_j*fs*Q/50`; anchor za fiksirani P je
`A=round_half_up(sum(q_j)/L - P*(L-1)/2)`.

Support traži sve sljedeće:

- `0<=A<P`; nema izmišljene točke na negativnom vremenu ili pomicanja sourcea;
- točno L owned točaka A+jP u [0,NQ), i A+LP>=NQ; nema prefix-only FINAL-a;
- za svaki model event `10*abs(q_j-(A+jP))<=P` (0.10-beat evidence residual);
- ciklična faza svih beat numbers mora odgovarati istom d i izabranom m;
- existing PF-02/native validation i kapaciteti moraju proći bez izmjene.

Residual je unaprijed izabrana evidence-support tolerancija iz postojećeg phase
ugovora, ne ground-truth ocjena niti dokaz glazbene točnosti. Prosječan period
može biti pogrešan i uz mali residual; to mora odbiti buduća glazbena evaluacija.
Ispad, missed/extra beat, promjenjivi tempo ili nepodržani metar ne smiju se
popravljati odbacivanjem događaja, extrapolacijom velikog uvoda ili novim segmentom.

Izlaz: jedan MODEL segment [0,N), b0=0, A/P/beat_count=L, selected tempo T,
jedan meter segment3/4 ili4/4, denominator4, exact earliest-owned downbeat witness
ordinal d i whole frame `(A+dP)//Q`. Q32 frakcija ostaje u gridu. Confidence je
uncalibrated/unavailable (wire0 samo gdje je obvezan numerički field); key unavailable.
Native builder/validator/serializer ostaju autoriteti, bez novog wire formata.
Neuspjeli obvezni output daje neuspjeli job uz očuvan prethodni prihvaćeni rezultat.

## 7. Izvedeni resursi i scheduling

R=30000 na600s. Neural linear/conv recurrent dio zahtijeva767730 MACs/red,
odnosno23031900000 MACs; čak kad bi dobio svih60s, treba383865000 MAC/s.
To isključuje FFT, resampling, activations, HMM, I/O i UI. To je potrebna
throughput stopa, ne dokaz da P4 može ili ne može ostvariti rok.

HMM S3=8415, S4=11220. Za pobjednika4 i B256, najviše118 checkpoint vektora:
`118*S4*8 + 256*S4*2 + 2*S4*8 =16515840 B` score/parent payload.
Path u16 je60000 B; tri f32 probabilityja360000 B. Naivni full parent array
`30000*S4*4=1346400000 B` ne alocira se. Konzervativni graph E4<=28380;
SoA predecessor-u16/logprob-f64/pointers-u32 payload<=328684 B, bez drugih mapa.
Score3+score4+winning-forward+winning-replay traže najviše1262250000 state updates
za pobjednika4. Broj incoming transitions i soft-F64 throughput također treba mjeriti.

Owner raspored: read/frontend/RNN -> immutable probabilities -> HMM -> grid ->
validate/build FINAL. NN weights/frontend/ring/FFT scratch nisu potrebni za HMM
i smiju se osloboditi prije decoder peak-a; ako ih platforma zadržava, dodaju se
istodobnom zbroju. Dvije output banke, pending/retained kopije i drugi M1 owneri
ostaju u PF-01 ledgeru. Ne brojati baseline898890 B dvaput. Navedeni decoder
payload nije total peak: graph/maps/alignment, FFT, stack/futures, allocator,
source codec, UI/USB i firmware/storage rezerve ostaju zasebne stavke/UNKNOWN.

Prije svakog novog mjerenja popuniti internal/PSRAM/storage ownership i actual
placement. B256 je fiksni block schedule, ne fizički RAM budget. Heap u coreu je0.
Promjena B može se razmatrati samo kao zaseban schedule-equivalence korak nakon
oraclea; bez model/threshold/decoder promjene radi pokušaja koji je već odbijen.

## 8. API, progress i cancellation

Planirani opt-in modul je `rust/src/product_analysis/`, pod budućim
`experimental-product-analysis`. Ovi potpisi su dizajn; ne dodavati feature sada.

```text
required_storage(profile, bound_model_manifest) -> StorageRequirements | Error
begin(profile, source_identity, known_N, caller_storage) -> Session | Error
step(session, work_budget, bounded_pcm_source) -> StepResult | Error
cancel(session) -> Cancelled | PendingCleanup
finish(session, caller_owned_result_bank) -> ValidatedFinal | Error
```

StorageRequirements navodi odvojeno byte count/alignment/lifetime po memory class;
unsupported identity, overflow ili premalen buffer odbijaju se prije writea.
States: ReadFrontend, Neural, ScoreMeters, CheckpointWinner, Replay, Construct,
Validate, Ready, Cancelled, Failed. WorkBudget mjeri scalar MACs/FFT butterflies/
HMM predecessor updates, ne cijele redove ili pjesme. Svaki kernel/I-O callback
mora moći vratiti kontrolu unutar izmjerenog najgoreg quanta; neblokirajući callback
ne smije skrivati monolitni codec ili softmax/FFT poziv. LSTM djelomični novi state
ostaje privatan dok se cijeli row ne dovrši. Budget prekid ne resetira history.

Prvi waveform/BPM progress koristi postojeće bounded waveform/tempo komponente
uz zaseban progress owner. Nema upisa u accepted TrackAnalysis slot prije FINAL.
Ako izvor nema dovoljno dokaza za koristan BPM do5s, to je promašen produktni
cilj, ne odobrenje za izmišljeni BPM. Progress i FINAL ukupno vrijeme uključuju
decode/read/close. Cancel50ms uključuje publication fence i quiescence; cleanup
rok/memory reclaim ostaje nepoznat dok se ne izmjeri. Cancel ili playback-start
nikad ne objavljuje novi djelomični mandatory rezultat.

## 9. Mali oracle i obvezne provjere prije koda/mjerenja

Javni matematički primjeri nisu glazbeni dokaz:

| Slučaj | Ulaz | Očekivanje |
|---|---|---|
| Regular4 | r=[5,30,55,80,105,130,155,180,205], numbers=[1,2,3,4,1,2,3,4,1], N=4.5*fs | T120000, A=fs/10, P=fs/2, L9, d0, jedan MODEL |
| Regular3 | prvih7 istih rowova, numbers=[2,3,1,2,3,1,2], N=3.5*fs | T120000, L7, earliest downbeat d2 |
| Phase break | zamijeni jedan regular4 beat number | UNSUPPORTED_GRID |
| Timing break | regular4 row105 zamijeni s115 | UNSUPPORTED_GRID; ne odbaciti outlier |
| Long uncovered tail | regular4 uz N=10*fs | UNSUPPORTED_GRID; ne extrapolirati |
| Capacity | više3072 događaja | CAPACITY; bez djelomične objave |
| Empty / invalid | empty, nonfinite, unordered, all-zero probs, all -inf path | NO_EVIDENCE / INVALID_INPUT prema uzroku |

Oba regular slučaja provjeriti pri44100 i48000Hz. Mali neovisni HMM oracle koristi
2-state graph, brute-force sve paths do duljine8, uniform prior i eksplicitne ties;
usporedi s recurrence i checkpoint rasporedom B1/2/3. Realni S3/S4 graph ima
fiksniB256; provjeri257/513 rowova, prekid unutar edge loopa, zadnji parcijalni
blok, sve -inf, state65535, replay input hash drift i absence of labels.
Resampler: DC/interior, impulse, rubovi, oba ratios, chunk1/7/256/large;
frontend: exact geometry/band/order/boundary i FFT1764, bez neural executiona sada.
LSTM: dvije kratke synthetic sekvence s poznatim malim weights, reset/no-reset i
izlazna softmax klasa. Kasniji model parity treba zaseban runtime/scope registration.

## 10. Redoslijed za slabiji model

| Korak | Datoteke / isporuka | Done i zabrana prelaska |
|---|---|---|
| CS-00 | ova specifikacija + mutable status | smjer potvrđen; nema executiona |
| CS-01 | binding zapisi u ovoj specifikaciji; budući `PRODUCT-COMPACT-BINDING.md/.json` za closure | digest/22 shapes/frontend144 ovdje statički zatvoreni; preostaju licence, finite values i training provenance; nema inferencije/treninga/oznaka |
| CS-02 | `PRODUCT-COMPACT-RESOURCE-ORACLES.md/.json` | frozen source-only resampler/filter/graph tables, total ledger i ručni mali primjeri; veži coefficient/graph hashes, primitive tolerances i oracle fixtures prije implementacije |
| PF-01 closure | postojeći budgets + exact-device evidence | brojčani internal/PSRAM/storage/stack/placement/cleanup limiti i ciljevi5s/60s/50ms; UNKNOWN nije PASS |
| CS-03 / PF-04 | `PRODUCT-EXPERIMENT.md/.json` | nakon prethodnih gateova registriraj JEDAN synthetic resource/parity pokus, exact programs/weights/runtime/dopuštene ulaze/caps i STOP; H1 samo ako se izričito traži |
| PF-05 | samo registrirani host prototype/testovi | najprije mali oracle i replay parity; tek zatim maksimalni synthetic workload pod caps; bez corpus runa ili native porta |
| PF-06/07 | odvojeno dopušten razvojni/transfer skup | postojeće event/grid/meter/phase metrike, paired veto i UNKNOWN nazivnici, bez threshold rescuea; odbijen kandidat ne ide u port |
| PF-08/09/10 | opt-in Rust, consumer, exact-image P4 | prihvaćen algoritam i resursi prije velikog porta; MP3/FLAC bridge/progress/cancel uključeni |

CS-01/02 su dopušteni statički/specification prework. Nisu zaobilazak PF-01 ili
prerano označen PF-04 PASS. Točna jednokratna akvizicija jednog checkpointa radi
specifikacije/metadata nije model execution. Ne preuzimati druge grane, runtime
ili dataset niti pokretati weights kako bi se zaobišla granica ovog opsega.
Nema proizvoljnog host RSS/time limita: tek stvarni ledger i poznato okruženje
određuju registrirane caps. Ne pokretati stari run CLI radi "samo preflighta".

Prije real-data executiona zamrznuti točne razvojne kriterije i cohort registraciju.
Postojeći event pragovi0.85/0.70 F1 i0.75/0.60 CMLt,1% period,0.10-beat phase i
zero-confirmed-breaks veto ostaju referentna neoslabljena ograničenja, ne gotova
nova full-source registracija. GTZAN je već izložen; nije fresh acceptance ili
dokaz neovisnosti. M/D/E/UNKNOWN, cijeli obvezni denominator i failure cases
moraju ostati vidljivi. Konačni APTA prihvat/key/confidence ostaju odvojeni.

**Stop rano:** nepodržana prava/weights/frontend, trošak iznad potvrđenog budžeta,
neidentičan checkpoint path, neomeđen kernel ili pad support/kvalitete odbija
trenutačni dizajn za iduću fazu. Ne graditi paketni/runtime framework oko tog pada.
VERSION ostaje1.0.1. U ovom checkpointu nema novog C/Rust koda, testnog model
executiona, corpus ili device mjerenja.
