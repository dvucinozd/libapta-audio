# Plan implementacije: dense beat/downbeat dekoder i put do Rust/P4

Datum: 2026-10-23. Status: **PLAN ONLY — algoritam nije implementiran ni izmjeren.**
Polazni commit: `ea7e67af10e641cd190dc0fd0b3496e6ed3ee817`, grana `rust-rewrite`.
Ovaj plan ne označava nijedan budući rezultat kao PASS.

## 1. Uputa modelu koji preuzima posao

Pročitaj ovaj dokument cijeli, zatim [strojne parametre](DENSE-DECODER-PLAN-20261023.json).
Radi jedan DD korak odjednom. Svaki korak završi dokazima, selektivnim commitom,
pushom i usporedbom punog lokalnog/remote SHA prije idućeg koraka.
Ne implementiraj sve faze u jednom prolazu. Ako uvjet prolaza nije zadovoljen,
slijedi navedeni STOP; ne mijenjaj pragove da bi dobio PASS.

Trenutačni korisnikov zahtjev je izrada plana. Ovaj checkpoint dodaje samo
dokumentaciju. Budući zahtjev za nastavak pokreće DD-00, ne automatski sve
uvjetovane faze. Ne traži ponovno odobrenje za već odobren korak, ali ne
izmišljaj podatke, budžet uređaja ili dokaz neovisnosti koji nedostaju.

Redoslijed:
`DD-00 -> DD-01 -> DD-02 -> DD-03 -> DD-04 -> DD-05`.
Samo ako DD-05 prođe:
`DD-06 -> DD-07 -> DD-08 -> DD-09`.
DD-00 uključuje rano razjašnjenje cilja P4; DD-07 je tvrda granica prije porta.
DD-06/07 mogu završiti dokumentiranim nedostatkom dokaza; to nije PASS.
DD-10 okuplja preostale uvjete proizvoda i izdanja.

## 2. Što rješavamo i što čuvamo

Hipoteza: potpuni beat/downbeat logiti sadrže slabe i konkurentske događaje
koje minimalni peak postprocessor gubi. Zajednički odabir vremena beatova,
metra 3/4 ili 4/4 i faze takta može poboljšati događaje i njihov postojeći
APTA prikaz. Promjenjivi međubeatni razmak dopušten je u dekoderu.

To je hipoteza, ne obećanje. Ovdje odabrani parametri su unaprijed definirane
inženjerske vrijednosti, nisu dobiveni učenjem niti optimizacijom na oznakama.

Zadržati:
- isti Beat This! final0 checkpoint, spremljene f32 logite i originalne evente;
- svih 100 izvora, 600 S6 prozora, 401 potpun i 199 nepotpun kontekst;
- neizmijenjene evaluatore, source frame koordinate i endpoint pravila;
- odvojene rezultate događaja, nominalnog OLS grida i stvarnih intervala;
- C, default Rust DSP, consumer pin/provider, wire v1 i limit osam segmenata;
- odbijene eksperimente kao povijesne rezultate, bez prepravljanja.

Ne raditi: novi model, treniranje, inference nad glazbom, nova preuzimanja,
sweep parametara, DBN paket, prilagodbu starog piecewise fita, nove
reference-anchor optimizatore, pristup holdoutima ili aktivaciju proizvoda.

## 3. Provjerena polazna točka

| Dokaz | Vrijednost i značenje |
|---|---|
| Dense capture | 150095 parova f32, 149997 in-domain redaka i 98 endpoint redaka |
| Legacy parity | 6095 beatova + 1851 downbeat, zasebni endpoint događaji 10/3 |
| Originalni OLS | 187/401 nominal joint; 178/401 nominal complete sequence |
| Usporedivi OLS actual | 101/401 actual joint; 97/401 actual complete sequence |
| Coarse S6 | 47/401 joint; originalni OLS ima dvije coarse joint regresije |
| Odbijeni piecewise | 174/401 nominal joint, 0 fixes / 13 breaks |
| Event baseline | Beat F1 89.83%, downbeat F1 78.55%; ovo su zaokruženi prikazi |
| Prihvat proizvoda | nije dokazan; navedene metrike nisu zamjena za završni prihvat |

Pri računanju koristi pune spremljene brojeve, ne zaokružene postotke iz tablice.
Izvori:
[dense rezultat](NEURAL-DENSE-RESULT-20261023.md),
[neuralni rezultat](NEURAL-REFERENCE-RESULT-20261022.md),
[piecewise rezultat](LEARNED-PIECEWISE-RESULT-20261023.md),
[migracija](MIGRATION.md).

Privatni WSL direktoriji:
- dense: `/home/daniel/.local/share/libapta-audio/rust-rewrite/neural-dense-20261023/attempt`;
- legacy: `/home/daniel/.local/share/libapta-audio/rust-rewrite/neural-reference-20261022/comparison-source-domain`;
- oznake: `/home/daniel/.local/share/libapta-audio/rust-rewrite/neural-reference-20261022/prepared-availability`;
- postojeći runtime: `/home/daniel/.local/share/libapta-audio/rust-rewrite/neural-reference-20261022/venv/bin/python`.

Putanje su lokacije, a hash iz JSON plana je autoritet identiteta.
Ne kopirati audio, logite, oznake, ID-eve ili per-source rezultate u Git.

## 4. Datoteke i odgovornosti

Sljedeće datoteke su **planirane**, osim onih izričito označenih kao postojeće.

| Datoteka | Odgovornost |
|---|---|
| `docs/rust/DENSE-DECODER-PROTOCOL.json` | DD-01: zaključani parametri, ulazi, gateovi i limiti |
| `docs/rust/DENSE-DECODER-EXPERIMENT.md` | DD-01: hipoteza, numerika, STOP i očekivani trošak |
| `tools/apta_dense_decoder.py` | DD-02: čisti deterministički algoritam; bez I/O |
| `tools/apta_1_1_dense_decoder.py` | DD-03: bind, construct-only proces, freeze, evaluacija |
| `tools/apta_dense_decoder_verify.py` | DD-04: neovisna provjera DP-a i artefakata |
| `tests/unit/apta_1_1_dense_decoder.py` | numerika, mali brute-force oracle, geometrija |
| `tests/unit/apta_1_1_dense_decoder_runner.py` | izolacija oznaka, manifesti, kvarovi i gateovi |
| `docs/rust/DENSE-DECODER-PREFLIGHT.md` | DD-04: točni testovi, resursi, commit i naredbe |
| `docs/rust/DENSE-DECODER-RESULT-<date>.json/.md` | DD-05: samo javni agregati, odluka i hashovi |
| `rust/check.py` (postojeći) | dodati pozive dvaju novih stdlib testova |
| `docs/rust/MIGRATION.md` (postojeći) | jedan jasan aktualni sljedeći korak |

Ponovno koristiti postojeće funkcije, ne kopirati ih i ne mijenjati im semantiku:
- `apta_neural_dense.decode/mapping/coverage`: payload/geometrija;
- `apta_1_1_neural_reference.project`: postojeći OLS adapter;
- `apta_1_1_event_representation.score_points/points_for_control`;
- `apta_1_1_learned_piecewise.control_points/score_point_windows/paired`
  samo kao postojeće evaluator pomoćne funkcije; NE `model.construct`;
- `apta_1_1_neural_comparison.event_metrics` i njegov postojeći mir_eval runtime;
- postojeće `bind/recheck/write_new` pomoćne funkcije.

Ne uvoziti capture runner u čisti dekoder: povlači inference ovisnosti.
Ne pozivati `Models`, Torch model ili pristupati audio datotekama.

## 5. DD-00 — provjera početka i cilja

1. `git fetch --prune origin`, status, branch, HEAD i origin SHA.
   Ako postoje tuđe promjene, sačuvati ih i izolirati vlastiti rad.
2. Pročitati lokalni AGENTS, DSP skill, aktualni status i ovaj plan.
   Izričita korisnikova grana rust-rewrite ima prednost nad starim zapisom 1.1.0.
3. Provjeriti da dense private report odgovara planu, zatim sve njegove
   payload/record hashove. Oznake ne parsirati.
4. Inventarirati u consumer dokumentaciji: analiza tijekom reprodukcije ili
   unaprijed, dopušteno čekanje, ciljni P4 i raspoloživi memorijski razredi.
   Zapisati postojeće činjenice i nepoznato u planirani
   `docs/rust/DENSE-DECODER-P4-CONSTRAINTS.md`.
   Ne pretpostaviti da offline znači neupotrebljivo za unaprijed analizirane
   datoteke; ne pretpostaviti ni da to znači izvedivo na P4.
5. Evidentirati da capture RSS uključuje Python/Torch i oba modela:
   nije RAM samo Beat This! i nije budžet uređaja.

Done: ulazi postoje i hashovi se slažu, poznata je granica host istraživanja.
STOP: nedostajući/oštećeni ulaz; ne regenerirati capture automatski.
Nepoznat konkretni P4 budžet ne blokira ovaj host eksperiment, ali blokira DD-08.

## 6. DD-01 — registracija prije implementacije

Strojni JSON uz ovaj plan je predložak konstanti i gateova. Prenijeti ih bez
promjene u novi protokol; `status` postaviti na `registered-not-run`,
dodati točni registration parent HEAD i hash plana. U tekst eksperimenta
prenijeti specifikaciju DD-02/03/05. Izračunati SHA protokola; runner/testovi
poslije moraju provjeravati taj SHA i podudaranje svih konstanti.

Ne ugrađivati budući vlastiti commit SHA u datoteku koja određuje isti commit.
Registration commit navodi kasniji preflight; implementation SHA piše runner.

Jednom pregledati konzistentnost specifikacije prije realnog runa.
Ako postoji logička greška, ispraviti uz obrazloženje PRIJE glazbenog mjerenja.
Nakon prve evaluacije stvarnih podataka ne mijenjati kandidat ni pragove.
Commit: `docs: register bounded dense beat and downbeat decoder`.

## 7. DD-02 — točna specifikacija čistog dekodera

### 7.1 API i dopušteni ulaz

`construct(beat_logits, downbeat_logits, total_frames, windows, known_gaps=()) -> dict`

Ulaz nema track ID, audio putanju, oznake, baseline verdict, coarse tempo,
reference-derived put ili staru listu beatova. Dozvoljeni importi: stdlib
`array`, `math`, `fractions`; pomoćni tipovi iz stdlib po potrebi.
Bez I/O, environment lookup, subprocess, mreže, randoma ili evaluator importa.

Validirati:
- 48000-Hz source domena, 1..1440000 frames, jednake liste 1..1501 konačnih f32;
- obične S6 geometrije s ISKLJUČIVO `first_frame/end_frame` ključevima;
- 1..8 prozora pokrivaju cijeli [0,total), bez rupa ili preklopa;
- očekivani broj redaka prethodno dokazan kroz capture metadata/mapping;
- indeksi s centrom `960*i >= total_frames` nisu kandidati za beat;
- poznate PCM rupe: validirati raspon; ako postoji barem jedna rupa, ovaj v1
  vrati source status `known_pcm_gap` i sve prozore unsupported.
  Ne spajati logite preko rupe i ne tvrditi da je okolni offline kontekst čist.
  Obrada rupa i dugih izvora nije dio prve hipoteze.

Svi originalni f32 redci ostaju neizmijenjeni na disku. Dekoder obrađuje
in-domain centre; rubni redci se evidentiraju, ne pretvaraju u audio događaje.

### 7.2 Deterministička numerika

Prije DP-a pretvori svaki finite f32 logit x u signed cijeli broj:
`q(x) = sign(x) * floor(abs(clamp(x,-32,32))*1024 + 0.5)`.
Za nulu vrati 0. Točno implementirati preko `float.as_integer_ratio()`
ili `Fraction.from_float`, kako round-to-even ne bi promijenio tie.
Clipping je svojstvo novog dekodera, ne promjena spremljenih podataka.
Zapisati privatne clipped countove po kanalu; ne koristiti ih za retuning.

Sve ocjene i kazne su integer. Ne računati sigmoid/log/exp.
Beat logit je log-odds odabira beat vs nonbeat; downbeat logit analogno.
Zbroj pretpostavlja te odds kao hipotezu modela; to nije kalibrirana pouzdanost.

### 7.3 DP stanje i prijelazi

Odvojeno riješi meter `m=3` i `m=4`. Unutar izvora meter je konstantan.
Dopušteni razmaci d su cijeli redci 10..75 (300..40 BPM pri 50 Hz).
Stanje `S[t,d,k]`: zadnji beat u retku t, dolazni razmak d, pozicija
u taktu k=0..m-1; k=0 znači downbeat.

Emission:
`E(t,k) = qb[t] + (qd[t] if k==0 else 0)`.

Početno stanje dopušteno je samo za `t<d`.
Njegova ocjena je E(t,k), predecessor je SEED; svaka k je dopuštena.
Taj d je pretpostavljeni prethodni razmak, ne izmišljeni izlazni beat.

Za `t>=d`, prethodni beat je točno `t-d`.
Prethodna faza je `(k-1) mod m`; prethodni razmak p je u
`[max(10,d-2), min(75,d+2)]`.
Iz svih dosegljivih prethodnika odaberi:
`S[t-d,p,(k-1)%m] + E(t,k) - 2048*(d-p)^2`.

Bez restartanja usred izvora, beam pruninga, fallbacka i skrivene normalizacije.
Promjenjivi d omogućuje promjenjivi tempo; ±2 ograničava promjenu susjednih
intervala. Polovična/dvostruka obitelj natječe se kroz cijeli raspon početnih d;
nije dopušten naknadni izbor obitelji iz oznaka.

Petlje: t uzlazno, d uzlazno, k uzlazno, p uzlazno.
Kod jednakog scorea zadrži najmanji p. Nedosegljivo je zaseban sentinel.
Terminalno stanje mora imati `(t+d)*960 >= total_frames`.
Time put pokriva izvor bez skrivenog ranog završetka.

Za svaki meter uzmi terminal s najvećim scoreom; tie: najmanji (t,d,k).
Između metara uzmi veći score; tie: m=3.
Globalni score <=0 daje `nonpositive_path`; null hipoteza ima score 0.
Pobjednik s manje od 9 beatova daje `insufficient_events`.
Nema zamjene drugoplasiranim ako pobjednik naknadno ne zadovolji minimum.
Samo jedan pobjednički događajni niz ulazi u evaluaciju.

Privatno spremi pobjednika svakog metra i razliku njihovih scoreova.
To nisu globalna top-2 puta niti confidence; najbolji alternativni put
unutar istog metra nije izračunat. Nemoj razliku koristiti za abstention.

### 7.4 Prostor i deterministički traceback

Koristi flat typed arrays, ne Python objekte/dict za svako stanje.
Za pojedini meter: index `((t*66)+(d-10))*m+k`.
Score: signed 64-bit (`array('q')`, provjeriti itemsize=8),
predecessor p: bytearray; 0=SEED, 255=UNREACHABLE, 10..75=prethodni d.
Unreachable score = `-(1<<62)`; nikad ga ne zbrajati.
Nema in-place rolling overwritanja prethodnika.

Obradi m=3 pa m=4, nakon svakoga izdvoji put i oslobodi state arrays.
Gornja granica score+parent tablica za m=4:
`1501*66*4*9 = 3566376 bytes`, bez ulaza/izlaza/Python overheada.
To je host tablica; nije tvrdnja da stane u dopušteni P4 razred memorije.

Traceback: dodaj (t,k,d); ako p=SEED, stani; inače
`t <- t-d; d <- p; k <- (k-1)%m`. Obrnuti listu.
Ne promijeniti d prije računanja prethodnog t.
Ne dopustiti ciklus, nepostojeći predecessor ili više od 1501 koraka.
Neovisno ponovno zbroji emissions i transition kazne te usporedi terminal score.

### 7.5 Vremenske koordinate i sub-hop lokalizacija

DP bira retke; zatim jednom lokaliziraj svaki odabrani beat pomoću SAMO
beat kanala (downbeat ostaje vezan uz isti ordinal).

Za in-domain susjede l=qb[t-1], c=qb[t], r=qb[t+1], ako c>l i c>r:
`delta = (l-r) / (2*(l-2*c+r))`, točan rational, clamp [-1/2,1/2].
Inače delta=0. Ako bilo koji susjed nije in-domain, delta=0.
Pozicija = round-half-up(`960*(t+delta)`) u cijelim source frames.
Nemoj koristiti endpoint logit kao desnog susjeda.

Sve pozicije moraju biti u [0,total), strogo rastuće, bez deduplikacije/clampa.
Neispravan izlaz je integrity failure, ne legitimna abstention.
Lokalizacija ne mijenja odabir retka, fazu ili broj događaja.
Spremi i row indekse i refined frames; za evaluaciju koristi refined frames.
Sve naknadne periode računaj iz finalnih frames, ne iz nominalnog d.

### 7.6 Izlaz i statusi

Minimalna schema:
- `format, decoder_name, status, confidence=0, confidence_calibrated=false`;
- `total_frames, selected_meter, terminal_score, per_meter_diagnostics`;
- `events`: `beat_frames, downbeat_frames, beat_rows, beat_phases`;
- `windows`: originalna geometrija, liste globalnih beat ordinals,
  status `candidate_events` ili `no_events`;
- `boundary_rows_excluded, clipped_logits, state_cells, transitions_examined`;
- `native_graph_constructed=false, production_promotion=false`.

Downbeats su točno podskup refined beats s k=0. Ordinals su 0..N-1 po izvoru.
Window membership se računa iz refined frame, ne starog retka; pomak preko
S6 granice se evidentira, ne gubi događaj. Svaki event pripada točno jednom prozoru.
Za source abstention sve event liste su prazne i svi prozori nose isti razlog.
Empty/unsupported nikada ne znači PASS. Confidence 0 ne zatvara safety gate.
Native graf i segment fitting ne implementirati u ovom koraku.

Done: čisti algoritam i njemu pripadajući generated/oracle testovi opisani u
DD-04 prolaze, bez realnih podataka. Runner testovi i potpuni preflight dolaze
nakon DD-03; nisu preduvjet za ovaj međukorak.
Commit: `tools: implement deterministic dense timing decoder`.

## 8. DD-03 — runner, izolacija i usporedba

### 8.1 Dvije procesne faze

CLI podnaredbe:
- `construct --dense DIR --legacy DIR --output NEW_DIR`;
- `evaluate --candidates DIR --legacy DIR --prepared DIR --output NEW_DIR`.

Construct CLI NEMA prepared/labels opciju. Uvoz dekodera ne smije uvoziti
evaluatore. Runner učitava evaluator importe tek u evaluate grani, ne na
module top-levelu. Evaluator je zaseban proces pokrenut tek nakon svih 100 kandidata.
Injekcijski test mora dokazati da nedostupne/promijenjene labels ne utječu na
construct rezultate. Ne tvrditi OS sandbox ako se samo testira programska izolacija.

Prije konstrukcije:
1. Provjeri clean source/revision i hash registriranog protokola.
2. Učitaj hash-bound dense report, njegove metadata/payload datoteke i legacy
   event report. Provjeri 100 jedinstvenih ID-eva i isti redoslijed.
3. Samo iz dense metadata uzmi total/windows/resampled length.
   Provjeri `codec.mapping` i coverage u skladu s capture ugovorom.
4. Veži sve tracked source datoteke, relevantni Python runtime i sve ulaze
   koje će ovaj run stvarno čitati; ne uvozi neuralni runtime za konstrukciju.
5. Refuse existing output; kreiraj private freeze i novi candidates direktorij.

Za svaki izvor jednom pozovi construct, spremi rezultat i hash.
Zaključi `candidates-frozen.private.json` tek kad ih je svih 100.
Recheck svih bindinga i HEAD/clean status; partial run nije cohort completion.
Ne čitaj labels ni ranije spremljene verdictove u ovoj fazi.

Evaluate prvo validira potpuni candidate freeze, protokol i source identitet.
Tek onda bind i parsiranje prepared oznaka i saved scoring reporta.
Kasniji documentation commit ne smije prešutno zamijeniti implementation SHA.
Obje faze prvog mjerenja izvode se na istom čistom implementation commitu.

### 8.2 Tri odvojene evaluacijske staze

A. **Događaji:** `apta_1_1_neural_comparison.event_metrics`, ista 5-s trim politika,
70-ms F1 tolerancija i CMLt definicija. Ta postojeća funkcija za oba izlaza
koristi integer `beat_frames/downbeat_frames` podijeljene s 48000; ne čita
`beat_seconds/downbeat_seconds`. Kandidat predaje svoje refined frames, baseline
točne spremljene legacy frames. Ne uvoditi novu konverziju iz seconds niti
mijenjati scorer. Puni baseline scoreovi moraju reproducirati stari report.
Prazan kandidat ostaje u nazivniku i dobiva 0 prema postojećem scoreru.

B. **Postojeći APTA prikaz:** na candidate beat_frames/downbeat_frames i svakom
originalnom S6 prozoru pozvati isti `ref.project`.
To je neizmijenjeni OLS adapter novih događaja; nije oživljavanje failed piecewise
fita. Spremiti projektirani tuple i koristiti neizmijenjeni `score_window`.
Usporediti sve old OLS/coarse tupleove i verdictove s baseline zapisima.
Meter/downbeat za ovu stazu dolazi isključivo iz tog candidate adaptera,
uključujući njegov postojeći two-complete-bars uvjet. Ne kopirati stari meter.

C. **Promjenjivi događajni niz:** svaki refined beat p dobiva outgoing interval
`next_p-p` kad postoji sljedeći source ordinal; zadnji ima None.
Interval preko S6 granice se smije koristiti jer dekoder ima isti neprekinuti
source put. Pozovi neizmijenjeni `score_points` po originalnim prozorima.
Kao usporedbu reproducirati OLS actual diagnostic iz failed-piecewise runnera,
s njegovim točnim pravilima ordinala/adjacency/unknown, 101 joint i 97 complete.
Zasebno evidentirati razliku dostupnosti successora između tih reprezentacija.
Ne uspoređivati candidate actual s OLS nominal 187 kao da su ista metrika.

Za nominal complete-sequence koristiti `points_for_control` nad candidate
projekcijom i `score_points`; provjeriti nominal joint paritet sa score_window.
Raw row-centre rezultati mogu biti samo privatna debugging provjera numerike,
ne drugi kandidat iz kojeg se nakon oznaka bira povoljniji rezultat.

### 8.3 Izvještaj

Svi agregati zadržavaju 100/600/401/199. Za svaki izvor/prozor privatno sačuvati:
candidate hash, event/projection/actual score, support status, fixes/breaks
po period/phase/joint/meter/downbeat, resurse i oba izvorna regression slučaja.
Javno: agregati, gate booleans, source/protocol/report hashovi, status evidencije.
Nikakvi ID-evi, per-source koordinate, labeli ili originalna imena u Git.

Report ima zasebne:
`integrity_passed, development_passed, independent_transfer=false,
confidence_calibrated=false, native_graph_constructed=false,
physical_p4=false, formal_acceptance=false, production_promotion=false`.

Commit: `tools: freeze dense candidates before independent scoring`.

## 9. DD-04 — obvezni preflight i neovisna provjera

Ne mjeriti stvarne logite dok svi donji testovi ne prođu.

| Test | Obvezna provjera |
|---|---|
| Numerika | ±half ties, signed zero, subnormal, clipping ±32, extreme finite, NaN/Inf reject |
| Schema | krivi ključevi, label-bearing window, rupe/preklopi, granice broja redaka |
| Small oracle | brute-force svi dopušteni putevi za N<=24, 3/4, usporedba scorea i tie odluke |
| Seed/end | početak točno t=d nije seed; terminal uvjet; zadnji endpoint nikad event |
| Traceback | precizna rekonstrukcija scorea, faza modulo m, bez pogreške promjene d |
| Constant 4/4 | 401 redak, total=400*960 frames (zadnji redak endpoint): baseline logits -8; beat +8 svakih 25 od retka 10; downbeat +8 svaka 4 beata, drugdje -8; očekivan meter/rows |
| Constant 3/4 | isto, downbeat svaka 3 beata; očekivan meter/rows |
| Variable timing | razmaci 25,25,26,26,27,27,28,28,29,29,30,30,29,29,28,28,27,27,26,26 ponavljani; nema skoka >2; phase progression i neuniformni izlaz |
| Competition | kratki enumerabilni fixture s jačim lokalnim mamcem ali boljim ukupnim drugim putem; oracle odlučuje očekivanje |
| Silence/null | svi logiti <=0 daju abstention; ne emitirati izmišljeni pouzdani grid |
| No support | manje od 9 pobjedničkih beatova; poznata PCM rupa; svi prozori ostaju prisutni |
| Refinement | simetrični peak delta0; (l,c,r)=(0,8192,4096) daje delta=1/6 i +160 frames; plateau delta0; rub bez susjeda |
| Coordinates | refined beat prelazi S6 granicu: jednom u novom prozoru; downbeat isti ordinal |
| Meter isolation | oznake metra mijenjaju score, nikada construct bytes |
| Baseline adapter | novi runner reproducira stare OLS tupleove na generated legacy fixtures |
| Missing successor | zadnji interval None, ne zamjenjuje se prethodnim/nominalnim |
| Freeze | permutiran payload, krivi hash, dupli ID, nedostajući source, partial freeze, overwrite reject |
| Label isolation | construct prolazi kad labels nije dostupan; candidate bytes jednaki uz permutirane synthetic oznake |
| Gates | jedan failure dovoljan za reject; missing/unknown nije pass; zero confidence ne daje qualified |
| Resource | full 1501-row synthetic slučaj, worst flat/ambiguous case, worker timeout i RSS/output cap failure |

Brute-force oracle ne smije zvati production recurrence, tie helper ili traceback.
Za N<=24 dopušten je isti javni raspon d10..75; enumerirati početni d/fazu i
prijelaze, uključujući razlikovanje početnih latentnih d. Razriješiti tie prema
isti pravilu: terminal order, a zatim predecessor p unatrag kroz put.
Pored toga napraviti ručno izračunate slučajeve s >=3 beatova za tempo penalty.
Minimalni event gate je naknadni wrapper: test DP primititve prije min9 gatea.

Neovisni verifier:
- jednostavnija scalar/tablična recurrence odvojena od optimizirane;
- isti javni konstante, ali vlastite petlje/indexiranje/traceback;
- na svim generated fixtures potvrditi iste pobjednike i output hashes;
- poslije realnog runa reproducirati svih 100 candidate putova prije releasea
  agregata, bez novih neuronskih forwarda;
- za svaku spremljenu putanju zasebno aritmetički provjeriti score,
  redoslijed, phase progression, refine koordinate i window partition.

Test naredbe (WSL, iz /mnt/d/AI/LIBAPTA/libapta-audio):
```sh
python3 tests/unit/apta_1_1_dense_decoder.py -v
python3 tests/unit/apta_1_1_dense_decoder_runner.py -v
python3 tests/unit/apta_1_1_neural_dense_capture.py -v
python3 tests/unit/apta_1_1_learned_piecewise.py -v
python3 tests/unit/apta_1_1_event_representation.py -v
cargo fmt --all -- --check
cargo test -p libapta --locked --test builder --test grid --test native_validation --example segmented_reference_probe
cargo test -p libapta --locked --release --test builder --test grid --test native_validation --example segmented_reference_probe
cargo clippy -p libapta --locked --lib --test builder --test grid --test native_validation --example segmented_reference_probe -- -D warnings
cargo check -p libapta --locked --no-default-features --lib
```

Cargo target i logovi izvan repoa. Koristiti postojeći ispravni toolchain,
`CARGO_BUILD_JOBS=2`, `RUSTFLAGS=-Dwarnings`.
Ako stvarna CLI sintaksa primjera zahtijeva dodatnu opciju, provjeriti Cargo
target definiciju i zabilježiti točnu izvršenu naredbu, ne tvrditi neizvršen PASS.
Za ovaj host-only korak ne ponavljati cijeli C/P4 matrix.

Preflight report sadrži stvaran broj testova, maksimalni generated runtime,
RSS i state bytes. Resursni limiti nisu cilj koji treba dosegnuti.
Ako synthetic cap padne, zaustaviti prije glazbe; dopušten je dokazivo ekvivalentan
performance fix i novi preflight. Promjena recurrence nije performance fix.
Commit: `docs: verify dense decoder preflight before music evaluation`.

## 10. DD-05 — jedan razvojni run i tvrda odluka

Implementation i preflight moraju prethodno biti commitani/pushani.
Izlaz je nov direktorij izvan Git stabla; ne overwrite/retry.
Oba procesa koriste isti čist source SHA; nema izmjena dokumentacije usred runa.

Planirana CLI uporaba, nakon implementacije:
```sh
python3 tools/apta_1_1_dense_decoder.py construct \
  --dense /home/daniel/.local/share/libapta-audio/rust-rewrite/neural-dense-20261023/attempt \
  --legacy /home/daniel/.local/share/libapta-audio/rust-rewrite/neural-reference-20261022/comparison-source-domain \
  --output /home/daniel/.local/share/libapta-audio/rust-rewrite/dense-decoder-v1/candidates-attempt-1
/home/daniel/.local/share/libapta-audio/rust-rewrite/neural-reference-20261022/venv/bin/python \
  tools/apta_1_1_dense_decoder.py evaluate \
  --candidates /home/daniel/.local/share/libapta-audio/rust-rewrite/dense-decoder-v1/candidates-attempt-1 \
  --legacy /home/daniel/.local/share/libapta-audio/rust-rewrite/neural-reference-20261022/comparison-source-domain \
  --prepared /home/daniel/.local/share/libapta-audio/rust-rewrite/neural-reference-20261022/prepared-availability \
  --output /home/daniel/.local/share/libapta-audio/rust-rewrite/dense-decoder-v1/evaluation-attempt-1
```

Rokovi: construct 60 s/source i 1800 s cijeli proces; evaluator 1800 s;
neovisni verifier 1800 s. Svaki proces RSS <=262144 KiB; ukupni artefakti
pokušaja <=64 MiB. Wall timer uključuje binding i završni recheck.
Koristiti parent watchdog (timeout/kill worker), ne samo provjeru nakon završetka.
Za RSS Linux ru_maxrss u KiB + hard worker limit gdje primjenjivo; dokazati da
worker/subprocess usage nije prešućen. Timeout/kill nikad nije musical rejection.

Svi sljedeći gateovi moraju proći:
1. Integrity/replay/denominator/resource gates.
2. Candidate nominal projection joint >=188; complete sequence >=179.
3. Zero OLS period/phase/joint breaks i zero coarse joint breaks.
4. Zero OLS-correct meter/downbeat breaks u projected stazi.
5. Candidate actual complete sequence >=98 i zero actual OLS period/phase/joint
   breaks po prozoru, s eksplicitnim unknown successor statusima.
6. Event macro beat F1, downbeat F1, beat CMLt i downbeat CMLt svaki >=
   odgovarajućeg točnog legacy macro rezultata, tolerancija računske usporedbe
   1e-12 u jedinici [0,1]. Ne koristiti 89.83 kao exact baseline.
7. Sve pojedinačne promjene event metrika i žanrovski agregati su evidentirani;
   macro PASS sam ne kvalificira žanrove ili confidence.

Ako bilo koji musical gate padne: objaviti REJECTED s fixes/breaks, bez transfera,
porta, retuninga, fallback izbora ili povećanja segment capa. Ako integrity padne:
INCOMPLETE/INVALID, ne donositi musical verdict. Sačuvati partial artefakte.
Ne ponavljati stvarni pokušaj automatski; najprije opisati uzrok i odvojenu
odluku o novoj registraciji, ne pretvarati implementation bug u povoljniju hipotezu.

Ako sve prođe: DEVELOPMENT_PASS_ONLY. Ne zvati accepted product ili P4-ready.
Objaviti rezultat i push prije DD-06.
Commit: `docs: record fixed dense decoder development verdict`.

## 11. DD-06 — neovisni transfer, samo nakon PASS

Prvi zadatak je manifest dokaza neovisnosti, ne ponovno mjerenje dostupnih skupova.
Postojeći ASAP/Ballroom development dijelovi su dopušteni dijagnostički izvori,
ali nisu automatski neovisni od model training podataka. Provjeriti source/audio
identity, pjesmu i performance/recording veze, public training membership i
raniju uporabu za tuning. Različit filename ili hash transkoda nije dovoljan.

Deliverables:
- `DENSE-DECODER-TRANSFER-PROTOCOL.json/.md`: točan skup/redi, prava pristupa,
  train/development/holdout povezanost, audio/label hashovi, capovi;
- unaprijed odabrani odgovarajući baseline i svi paired kriteriji;
- isti decoder/protocol hash, bez promjene parametara;
- capture novog skupa zasebno vezan uz isti checkpoint/preprocessing;
- svi kandidati zamrznuti prije scoringa, javni agregati i private audit.

Za slabiji model: NE izmišljati transfer manifest. Ako neovisnost nije dokaziva,
zapisati INDEPENDENCE_UNRESOLVED i stati na toj granici. Prethodno otvoreni skup
smije dati diagnostic rezultat, ali ne zatvara ovaj korak.
Pragovi se registriraju prije otvaranja novog skupa: zero baseline breaks po
istim obiteljima, poboljšanje joint/complete i nepadanje event metrika.
Apsolutne countove iz 401-window GTZAN skupa ne prenositi na drugi nazivnik.
Ne otvarati formalne holdoute niti svježi završni prihvat za ovaj korak.

## 12. DD-07 — izvedivost kompletnog modela i dekodera na P4

Ovo je odluka prije velikog porta. Poboljšan host decoder ne rješava automatski
Beat This! frontend, težine, inference, kontekst i ukupnu potrošnju.

U constraints dokument DD-00 dodati tablicu sa stupcima:
`resource / product limit / measured or derived need / evidence / pass`.
Obavezni redci: internal RAM, PSRAM, stack, scratch, weights/model storage,
FFT/features/activations, DP traceback, output banks, cancellation granularity,
latencija prvog/upotrebljivog/final rezultata, vrijeme po minuti audija,
dopušten background CPU uz reprodukciju i audio/USB interference.

Svaka brojka mora navesti izvor. Nepoznat product limit ostaje UNKNOWN.
Desktop RSS ili veličina checkpointa nisu isolated activation RAM.
Samo statički izračun nije fizički PASS.

Tri moguća zaključka:
- cijeli odabrani put realno stane u utvrđene granice -> uski opt-in Rust port;
- prikladan samo za desktop/pre-analysis -> potrebna izričita odluka o takvoj
  produktnoj arhitekturi; ne prebacivati računanje izvan P4 prešutno;
- nedostaje održiv model za standalone P4 -> zaseban projekt kompaktnog modela,
  quantization/distillation/training, novi protokol i nova mjerenja.

Ne oživljavati odbijeni BeatNet samo zato što je manji.
Ne birati novi model unutar ovog plana. Ne mijenjati consumer provider ili pin.
Done: dokumentirana odluka i konkretni budgeti, ili pošten STOP/UNKNOWN.

## 13. DD-08 — Rust dekoder i reprezentacija, uvjetovan DD-06/07

Dva zasebna checkpointa, bez izmjene default DSP-a.

A. Port decoder:
- planirane datoteke `rust/src/dense_decoder.rs`,
  `rust/tests/dense_decoder.rs`, gated export u `rust/src/lib.rs`;
- opt-in `experimental-dense-decoder` feature u rust/Cargo.toml;
- isti integer score/tie/refinement/endpoint/abstention ugovor;
- caller-owned scratch i output; checked capacity, bez alloc/unsafe u coreu;
- `required_scratch`, `begin`, `step(max_transitions)`, `finish`, `cancel`;
- konkretne struct/API definicije zaključati u DD-08-API.md prema DD-07 budžetu
  PRIJE koda; slabiji model ne smije izmisliti ciljni scratch ili trade-off;
- step granica je broj razmotrenih prijelaza, ne jedan čitav izvor;
- chunk budgets 1/7/256 i veliki budget daju jednake izlaze;
- cancel/resume politika eksplicitna; cancelled posao ne objavljuje rezultat;
- Python/Rust exact integer score, rows, frames, phases i status na generated
  fixtures i cijelom već zamrznutom candidate skupu, bez ponovnog tuninga.

B. Native/wire projection:
- prvo koristiti DD-05 neizmijenjeni OLS-compatible izlaz za točnu usporedbu;
- variable raw events ostaju zasebni; ne tvrditi da OLS čuva promjenjivi tempo;
- provjeriti `grid.rs`, `native_validation.rs`, `builder.rs` i consumer
  supported representation prije definiranja novog adaptera;
- osam segmenata i 3072 beata ostaju granice; capacity failure je eksplicitan;
- native->wire->reader->consumer replay mora potvrditi isti grid/meter/ordinals;
- ako prihvaćene događaje nije moguće prenijeti kroz podržanu reprezentaciju
  uz utvrđene tolerancije, STOP za novu registraciju adaptera.
  Ne obnoviti failed piecewise algoritam ili reference optimizer.

Ne obećavati novi segment-fit bez podataka iz DD-05/07. Njegova specifikacija
je izričit budući decision gate, ne implementacijska sloboda slabijeg modela.
Model inference port je zaseban zadatak ovisan o DD-07; decoder port bez
inference puta nije end-to-end P4 implementacija.

## 14. DD-09 — potrošač i fizički uređaj

Nakon native parity i odabranog inference puta:
- u stvarnom consumer repou provjeriti aktualni branch/pin i owner lifecycle;
- zaseban opt-in job, identitet izvora/generacije, bounded queue, cancellation;
- stale source, media removal, superseded selection i retained reader testovi;
- izmjeriti ukupnu memoriju s playbackom, FFT/inference/DP scratch i stack;
- razlikovati compile/link, boot, funkcionalni rezultat i fizičku kvalifikaciju;
- testirati USB/audio coexistence, watchdog, prekide, memory placement/DMA/cache;
- prikupiti važeći fizički 1800-s evidence ugovor za točan flashed image;
- production activation tek nakon svih uvjeta; ne nasljeđivati prolaz drugog imagea.

Provjere i firmware buildovi pripadaju stvarnom consumer projektu.
Ovaj plan ne daje nepostojeći endpoint, port, board config ili serial path.
Nedostajući uređaj/operator dokaz ostaje OPEN; ne simulirati PASS.

## 15. DD-10 — što još treba do završenog proizvoda

Beat/downbeat prolaz ne zatvara:
- kvalitetu key/tonality i postojeću portable numerical boundary;
- kalibraciju pouzdanosti na odvojenim podacima i high-confidence safety;
- cijele pjesme, duge izvore, promjene metra, EOF/gaps i dugotrajni lifecycle;
- preostale native/platform matrix i production-owner policy;
- svježi >=48-track završni prihvat po zamrznutom protokolu;
- puni C ABI/packaging ako se traži zamjena C potrošača (odvojeno od native Rust);
- fizičku P4 kvalifikaciju i exact-candidate release freeze.

Za svaki otvoreni blok otvoriti zaseban specifikacijski checkpoint tek kad
postane sljedeći odobreni zadatak. Ne proširivati ovaj beat decoder kandidat.

Završni acceptance gates ostaju: key>=75%, meter>=95%, downbeat>=90%,
beatgrid>=90%, period tolerance<=1%, cyclic downbeat<=0.10beat,
high-confidence errors<=5% po obitelji, >=48 svježih provjerenih pjesama.
Ne zamijeniti ih GTZAN window metricom ni 70-ms event F1 tolerancijom.
VERSION ostaje 1.0.1 do izričitog release postupka.

## 16. Checkpoint zapis i pravilo objave

Za svaki DD korak završni zapis mora sadržavati:
- što je implementirano/izmjereno i što ostaje OPEN;
- puni source SHA, protocol/input hashove i točne naredbe;
- stvarne testove i rezultate, resurse, fixes/breaks gdje primjenjivo;
- public report i private evidence lokaciju;
- PASS/REJECTED/INVALID/UNKNOWN uz razlog;
- čisto stablo i jednak puni local/remote SHA nakon push;
- sljedeći dopušteni DD korak ili eksplicitan STOP.

Nikad blanket stage/reset/clean. Stage samo datoteke tog checkpointa.
Prije commita pregledati staged diff, privacy, linkove i `git diff --cached --check`.
Push samo `HEAD:rust-rewrite`; bez force. Remote napredak rješavati pregledom,
ne overwriteom. Ne pisati memorije i ne otvarati PR/release u sklopu ovog plana.

## 17. Kratka predaja za nastavak

> Nastavi prema DENSE-DECODER-IMPLEMENTATION-PLAN-20261023.md, počevši od prvog
> nedovršenog DD koraka. Ovaj checkpoint je plan-only. Provedi DD-00 i DD-01
> prije decoder koda. Ne diraj modele, scorere, private oznake, default DSP ili
> consumer pin. Slijedi točnu recurrence, numeriku i testove iz plana. Svaki
> checkpoint provjeri, komitaj, pushaj i potvrdi SHA prije idućeg. Ako gate padne,
> sačuvaj rezultat i stani na navedenoj granici; ne podešavaj kandidat na oznakama.
