# CS-02: zamrznute matematičke tablice i resource oracle

Datum: 2026-10-26. Baza: `76b4f2aa9570b808633eaba5d9d385fd9f9ebe15`,
grana `rust-rewrite`. Status: **STATIC_ASSETS_AND_ORACLES_PASS; PRODUCT_GATES_OPEN**.
[Strojni receipt](PRODUCT-COMPACT-RESOURCE-ORACLES.json),
[CS-01 dizajn](PRODUCT-COMPACT-CANDIDATE-SPEC.md),
[PF-01 budžeti](PRODUCT-BUDGETS.md), [PF-02 ugovor](PRODUCT-GRID-CONTRACT.md).

Hipoteza ovog koraka je da se CS-01 matematički dijelovi mogu jednoznačno vezati
uz bajtove tablica, numerička očekivanja i potpuni popis resource nepoznanica.
To nije hipoteza o točnosti ili brzini detektora. Ne mijenjaju se neuralni model,
pragovi glazbenog prihvata, B256 raspored, grid fit ili prethodni rezultati.
Nema neuralnih importa, čitanja weights/glazbe/oznaka/logita, kandidatnog frontend/
FFT/Viterbi izvođenja, package instalacije, C/Rust izmjene ili device mjerenja.
Izvršene su samo javne matematičke derivacije i provjere tablica/oracle primjera.

## 1. Prenosive tablice i provjera identiteta

Sve datoteke su u [fixtures/product-compact-cs02](fixtures/product-compact-cs02/).
[Manifest](fixtures/product-compact-cs02/assets/manifest.json) veže veličinu,
oblik, scalar type i SHA-256 svake datoteke. Ima17 asset datoteka/582129 B;
od toga16 raw numeričkih datoteka/578489 B i jedan javni `oracles.json`/3640 B.
To nisu model weights, audio ili snimljeni logits.

Raw datoteke su little endian, bez headera, paddinga ili implicitnog alignanja.
Lokalni `.gitattributes` označava raw tablice kao binary, a fixture skripte,
notice i JSON kao LF text; Git checkout ne smije promijeniti hash-bound bajtove.
Prije uporabe provjeri SHA-256, točan byte count i shape; ne ignorirati višak ili
kratak zapis. Dtype se ne zaključuje iz platforme. F64 se ne učitava kao f32.
Sve spremljene floating vrijednosti su konačne; nulti HMM prijelazi nisu zapisani
kao -inf nego su izostavljeni iz sparse grafa. Prava alignment pravila targeta
pripadaju budućem storage layoutu, ne formatu ovih raw datoteka.

| Tablice | Oblici / veličine | Vlasništvo |
|---|---|---|
| `fir-44100.f64le`, `fir-48000.f64le` |1x65 i147x65, ukupno76960 B | CS-FIR65 normalizirani koeficijenti; faza je fractional source faza |
| `hann-1764.f64le` |1764,14112 B | symmetric Hann; centrirani frame/EOF ugovor iz CS-01 |
| `dft-roots-1764.f64le` |1764x2 real/imag,28224 B | `exp(-2*pi*i*k/1764)`; mathematical DFT reference, nije FFT implementacija |
| `filter-edge-bins.u16le` |146,292 B | unique edge binovi;144 trokuta |
| `filter-starts.u16le`, `filter-offsets.u32le` |144/145,288/580 B | početni bin i offset u kontinualni coefficient payload |
| `filter-weights.f32le` |1745,6980 B |144 normalizirana trokuta, uključujući početne nule |
| `hmm3-*` |8415 states,13440 edges,193309 B | u32 pointers, u16 previous, f64 logp, u8 `(beat,interval,q)` mapa |
| `hmm4-*` |11220 states,17920 edges,257744 B | isti SoA raspored; state ID nikad65535 |

FIR source faza pri48000Hz je `(320*k)%147`, integer source centar
`(320*k)//147`. Ne koristiti `k%147`. Pri44100Hz centar je2*k, faza0.
Offseti su -32..32 rastućim redom; denominator uključuje svih65 koeficijenata,
uključujući mjesta gdje je source izvan EOF-a. Nema edge renormalizacije.

Filter shape slijedi pinned madmom iz CS-01: exact integer-rational trokut,
svaki ramp element prvo rounded-to-nearest f32, zatim f32 normalization.
Da redoslijed redukcije ne bi ovisio o novoj NumPy instalaciji, ovdje se fiksira
8-lane pairwise redoslijed iz NumPy1.26.4, commit
`9815c16f449e12915ef35a8255329ba26dacd5c0`,
[loops_utils.h.src](https://github.com/numpy/numpy/blob/9815c16f449e12915ef35a8255329ba26dacd5c0/numpy/core/src/umath/loops_utils.h.src).
[BSD notice](fixtures/product-compact-cs02/NUMPY-NOTICE.txt) je sačuvan.
NumPy nije importan/instaliran. Ovo specificira buduće coefficient bytes;
nije već izvršen upstream runtime parity. Bin mapping i broj značajki se ne mijenjaju.

## 2. Neovisne provjere i granice dokaza

[Generator](fixtures/product-compact-cs02/generate_assets.py) koristi samo stdlib
i ne čita glazbu, weights ili stare pokušaje. Output mora biti NOV direktorij;
postojeći se ne prepisuje. Zamrznuti bajtovi su autoritet. `libm`/Python drift pri
ponovnom generiranju nije dopuštenje da se golden datoteke automatski zamijene.
Manifest bilježi runtime i digest generatora. Novi backend učitava verificirane
tablice umjesto da ponovno računa exp/sin/tablice na uređaju.

[Neovisni verifier](fixtures/product-compact-cs02/verify_assets.py) ne importira
generator, model ili madmom. Decimal80 i racionalne formule provjeravaju:

- svih65 koeficijenata na6 odabranih FIR faza; unit DC sum za svih148 faza;
- 7 Hann i8 DFT root primjera uz globalnu simetriju/sum/unit-circle provjeru;
- svih4356 mogućih intervalskih parova: threshold masku, normalization i logp;
- svih19635 states, incoming predecessor redoslijed, validne ID-eve i oba grafa;
- svih144 filtera prema exact-rational trokutu i area normalization;
- 4 mala HMM primjera iscrpnim racionalnim nabrajanjem svih putova, bez izvođenja
  kandidatne recurrence, checkpoint algoritma ili NN-a.

Maksimalne razlike prema neovisnoj referenci: FIR1.67e-16, Hann1.12e-16,
roots1.84e-16, transition logp1.43e-14, f32 filter prema exact-rational1.99e-8.
Provjere prolaze zadane static-reference granice2e-14 za FIR/Hann/roots,
4e-13 za transition logp i `2^-21*abs(exact)+2^-149` za filter coefficient.
To su provjere matematičkih asseta, ne budućeg target kernela.
Ponovno generiranje u novom vanjskom direktoriju daje identične bajtove svih18
datoteka (17 asseta i manifest). Pet korupcijskih provjera odbija skraćeni FIR,
NaN logp, parent65535, duplicate JSON key i pogrešnu source bazu. Nema preskočenih
provjera ili promjene originalnih asseta pri tim negativnim primjerima.

Racionalni tiny-HMM primjeri duljine1/3/3/8 imaju uniform initial prior. Tie bira
najniži final state pa rekurzivno najnižeg prethodnika, ne forward lexicographic
path. Non-tie3-row primjer daje path[0,0,0], initial0 i joint probability324/15625.
All-half3-row primjer daje path[0,0,0] i1/128. Checked-in oracle ne sadrži velike
generated probability traceove ili stvarne glazbene koordinate.

Threshold0.05 je binary64 RHS; najbliži f32 zapis decimalnog0.05 prolazi, njegov
f32 prethodnik ne prolazi. Jedina podrška na row0 znači `[0,1)`, ne prazan input.
Prazan input je NO_EVIDENCE. Probability triple mora imati konačne komponente
u[0,1] i `abs(sum_f64(p)-1)<=2^-21`; ne renormalizira se. Triple(0,0,0) je
INVALID_INPUT, a valjan(0,0,1) vodi u NO_EVIDENCE. Log(0) unutar valjanih emisija
ostaje -inf. Ako oba metra nemaju konačan put, nema rezultata niti fallback grida.
Ta numerička input pravila ovdje se zatvaraju prije bilo kakvog izvođenja kandidata.

## 3. Zamrznuti numerički kriteriji za buduće primitive

Ovo su unaprijed zadane provjere budućeg synthetic-only prototypea. U ovom
checkpointu NIJEDAN takav kernel nije implementiran ili izmjeren.

| Sloj | Ugovor |
|---|---|
| Asset load | bytes/hash/shape/dtype/coordinates točno; zero implicit conversions |
| CS-FIR65 f32 output | F64 scalar reference pa f32; `abs(error)<=2e-5+2e-5*abs(reference)` za konačan PCM u[-1,1] |
| FFT1764 complex output | unscaled F64 DFT oracle; `abs(complex_error)<=1e-4+2e-5*abs(reference)` po binu za windowed PCM u[-1,1] |
| Magnitude/filter/log/difference | F64 scalar reference s frozen f32 filterima; `abs(error)<=2e-5+2e-5*abs(reference)`; shape/order/first-row zeros točno |
| f32 linear/Conv/LSTM primitive | pri prvom scalar prototypeu f32 multiply pa f32 add u rastućem input indexu; bez FMA/reassociation; gates/bias/reset/chunk odluke točno |
| Sigmoid/tanh primitive | F64 matematička referenca; `abs(error)<=2e-6+2e-5*abs(reference)` na unaprijed navedenim testovima [-20,-2,-1,0,1,2,20] |
| Softmax | subtract-max, klase beat/downbeat/nonbeat; zero logits daju f32(1/3) u svakoj klasi; validity pravilo iz odjeljka2 |
| HMM emission log | F64 `log(p_non/15)`, `log(p_beat)`, `log(p_down)`; konačne vrijednosti prema F64 oracleu unutar4e-13; nule su tagged -inf; nema eps clippinga |
| HMM checkpoint vs monolithic oracle | isti frozen log-density input, prior/table bytes, F64 arithmetic/op order: identični score bits, path, status i ties; nema tolerancije za različit path |
| Q32/fit/meter/wire/cancel | exact integer/rational reference i PF-02 pravila; nema floating tolerance za druge ordinals ili vlasništvo |

Svaka primitive fixture mora imati konačan i unaprijed zamrznut input prije koda.
FFT closed-form primjeri su delta0, constant1, cos(bin7) i delta3; Hann se testira
odvojeno, a composition mora imati zaseban test. Za linear/LSTM je prva referenca
normativni scalar f32 order; optimizirani/FMA backend je zaseban parity korak.
Nulti scalar LSTM daje h=c=0 kao ručno izveden primjer, ne kao pokrenut NN test.
JSON dodatno zamrzava log-prior scalar bits za oba grafa i emission scalar
primjere. Checkpoint parity ne smije potajno uspoređivati dva različita log
backenda umjesto dvije memorijske organizacije istih score operacija.

Primitive tolerancije ne dopuštaju promjenu HMM puta ili glazbenog verdicta u
budućem end-to-end parityu. Loš rezultat ne popravlja se povećavanjem tolerancije.
Kernel failure je preflight FAIL, ne dokaz glazbenog poraza. Ova pravila ne
registriraju checkpoint inferenciju, corpus scope ili puni PF-04 pokus.

## 4. Točan decoder work i lifetime scenariji

Svi brojevi ispod su payload/scenario derivacije, ne measured peak ili odobrena
P4 rezerva. Actual target FFT/backend/control/alignment još nisu implementirani.
Za R30000, B256 i najskuplji winner4:

| Komponenta | Payload |
|---|---:|
|118 checkpoint F64 vektora |10591680 B |
|256x11220 lokalni u16 parents |5744640 B |
|dva rolling F64 vektora |179520 B |
|score/parent subtotal |16515840 B |
|jedan winner4 graph + u8 mape |257744 B |
|decoder scenario arena |16773584 B |
|tri f32 probabilityja x30000 |360000 B |
|u16 winner path x30000 |60000 B |
|3072 event rows-u32 i beat-number-u8 |15360 B |

Skup13440/17920 edges daje za score3+score4+winning-forward+winning-replay
**2016000000 predecessor updateova** ako pobijedi4. Ako pobijedi3,1747200000.
Winner4 ima1262250000 state visits, kao CS-01. Kad bi dobio cijelih60s samo za
taj dio, trebao bi33600000 F64 predecessor updateova/s. Neuralni dio zasebno
traži383865000 MAC/s pod istom hipotetskom pretpostavkom. Oba dijela ne mogu
svaki potrošiti cijeli isti rok; I/O, FFT, nonlinearities i UI također troše vrijeme.

Common coefficient assets su127436 B. Model parameter payload3068500 B.
Konkretan NN row-work scenario je30292 B, popisan po komponenti u JSON-u.
F64 two-complex-buffer FFT reference scenario dodaje56448 B; nije target backend
commitment. S tim scenarijem NN payload je3282676 B. Actual target FFT scratch,
kernel controls, stack/futures, alignment i task interleaving ostaju UNKNOWN.

**Fizički alocirano nije isto što i logički korišteno.** Caller koji sve spremnike
odvoji na begin i zadrži ih do finisha nije oslobodio NN memoriju prelaskom u HMM.
Za prikaz razlike, uz PF-01 overlap scenario898890 B (jednom), probability/path/
event buffere alocirane za cijeli job i gore navedeni reference FFT scenario:

- uz dokazano phase reclamation/rebinding i samo jedan live graph:18107834 B;
- uz odvojene NN/decoder buffere i oba resident grafa:21583819 B.

To nisu dva izmjerena allocator peaka. Prvi zahtijeva stvarno oslobađanje/rebinding
na platformskom owneru, ne samo prestanak čitanja težina. Safe core ne smije
reinterpretirati proizvoljni byte arena u f64/u16 putem novog unsafe koda; taj
layout i platform binding tek se specificiraju u PF-08. Drugi scenarij ne
pretpostavlja takvu uštedu. Actual graph/model flash mapping također treba dokaz.

Oba broja isključuju dodatne retained/pending kopije, actual kernel/platform
overhead, codec/I/O, UI/USB/audio/DMA rezerve, fragmentaciju i ostale PF-01 UNKNOWN
stavke. Native controls se ne dodaju napamet na PF-01 canonical graph subtotal.
Dokumentiranih32MiB nije slobodna analitička rezerva. Nema full PCM, full feature
tenzora ili full30000xS parent alokacije u ovom dizajnu.

## 5. Reprodukcija i sljedeći gateovi

Iz repo roota, system Python3, bez neuralnog venv-a:

```text
python docs/rust/fixtures/product-compact-cs02/verify_assets.py --assets docs/rust/fixtures/product-compact-cs02/assets --receipt NEW_EXTERNAL_RECEIPT.json
python docs/rust/fixtures/product-compact-cs02/generate_assets.py --output NEW_EXTERNAL_DIRECTORY
```

Verifier nad checked-in assetima zatvara hashes, matematiku i exact oracle inpute.
Pri reprodukciji generatora usporedi shapes/hashove s manifestom; na drugačijem
libm-u byte drift traži objašnjenje, ne promjenu goldena. Verifier ne smije
generirati tablice umjesto provjere postojećih niti prepisivati postojeći receipt.
Izvorni generator, verifier i aggregate result hash-bound su u ovom receiptu.

CS-02 statički zadatak je završen. **Sljedeće zatvoriti CS-01 binding i PF-01**:
utvrditi BeatNet+ code/weights prava i training provenance, zasebno provjeriti
finite tensor values, zatim točnu ploču/sliku i numerical RAM/storage/placement/
cleanup budžete. Već odabrani JC1060P470C_I_W_Y i pretpostavljeni v3.2 nisu
očitana chip revizija. Ne flashati drugi uređaj na temelju same COM prisutnosti.
Dok je fizički dio nedostupan, dopušten je nezavisan statički binding rad.

PF-04 ostaje NOT_RUN. Tek nakon potrebnih gateova registrirati jedan synthetic
resource/parity pokus s caps, exact runtime/program/model identitetima i scopeom;
nema automatskog H1 re-run-a, inferencije, treninga, kvantizacije, corpus pokušaja
ili velikog porta. Key/confidence, formalni holdouti i VERSION1.0.1 ostaju sačuvani.
