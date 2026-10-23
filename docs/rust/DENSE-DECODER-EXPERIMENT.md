# Registered dense beat/downbeat decoding experiment

Date: 2026-10-23. Status: **REGISTERED, NOT IMPLEMENTED OR RUN**.
Protocol SHA-256: `5dc1241087a08663b06766f91faade5a5758f9791fd85e0077c62e78d5a186fd`.
Parent/DD-00 checkpoint: `76849bddb95c4d8e72775caf365a5cd1c1e1bfea`.

The [machine-readable protocol](DENSE-DECODER-PROTOCOL.json) binds the
[implementation plan](DENSE-DECODER-IMPLEMENTATION-PLAN-20261023.md), its
[proposed constants](DENSE-DECODER-PLAN-20261023.json) and the completed
[DD-00 audit](DENSE-DECODER-P4-CONSTRAINTS.md). The full algorithm, runner and
measurement instructions are reproduced below from that bound plan. Their DD
headings identify future implementation checkpoints, not completed work.

## Hypothesis and baseline

Full frozen beat/downbeat logits may preserve competing weak events and timing
that peak-only events discard. One integer source-level renewal decoder jointly
chooses variable intervals, meter 3/4 or 4/4 and bar phase, followed by fixed
sub-hop localization. Original OLS projection remains the compatibility adapter.
The failed learned-piecewise fit is not reused or retuned.

Baseline stays 187/401 nominal joint and 178 complete, with the separately matched
actual-successor diagnostic 101 joint/97 complete. All 100 sources/600 windows
remain, 401 complete and 199 incomplete reference contexts. New candidates may not
be selected by labels or old-correct verdicts. Both original coarse-to-OLS
regressions remain tracked; zero coarse joint breaks requires repairing them too.

## Isolation, cost and stop

No new audio read, neural forward, checkpoint, model training, dataset, dependency
installation, holdout, native port, consumer change or product activation.
Original captures/legacy events stay immutable and hash-bound. Candidate output
must freeze before reference labels are parsed in a separate evaluator process.
The constructor takes only dense channel arrays and plain source/window/gap
geometry; no legacy event array, track ID, truth or coarse tempo.

For 1501 rows, total DP states across both meters are at most 693462; only one
meter table is live at once. The largest score+parent allocation is 3566376 bytes.
The loose transition bound is1501*7*324=3404268 predecessor examinations/source
(before seed/domain restrictions), approximately 340 million for 100 sources.
These are derived operation/array bounds, not measured runtime or P4 fit.
Host construction is capped 60 s/source and 1800 s total; evaluation and independent
verification each 1800 s. Each process is capped 262144 KiB RSS; all attempt outputs
combined 64 MiB. Full-size synthetic operational preflight must demonstrate these
limits before real decoding. Parent/worker accounting and wall-clock enforcement
must include bindings and final rechecks.

A valid complete cohort failing any conjunctive musical gate is REJECTED.
Integrity/hash/resource/partial failure is INVALID/INCOMPLETE without a musical
verdict. Retain evidence, do not overwrite/retry automatically or change parameters
after seeing real results. All gates passing means DEVELOPMENT_PASS_ONLY;
independent transfer, confidence, supported native representation and physical
whole-pipeline P4 remain separate. Unknown product budgets are documented in
DD-00 and block product-directed porting, not this host experiment.

## Specification precision

Score equality uses the registered integer tie ordering. Refinement uses exact
rational arithmetic and half-up frame rounding. Its asymmetric example and pulse
fixtures in the copied specification are generated tests, not measurements of
music. A strict f32 input check may be implemented via exact f32 roundtrip; decoded
captured f32 is the runner's source of truth. Window schema accepts only its two
geometry fields. Output reason/status and ownership are explicit; malformed
coordinates must raise an integrity error, not silently become missing support.

The independent oracle must expose pre-abstention optimum states so short
fixtures can exercise min9 separately. The scalar verifier may use a different
bounded table layout but must not share production recurrence/tie/traceback logic.
Its comparisons cover both per-meter optima, winner, refinement and all ownership.
Capture geometry verification must retain endpoint rows externally while excluding
them from candidate emissions/refinement neighbors.

No threshold or algorithm parameter differs from the bound plan. Deterministic
schema names and reporting precision are fixed in the JSON construction contract.
Commit/push this registration before DD-02 code. Later preflight records the
actual registration SHA; this file intentionally does not embed its own commit.

## Verbatim bound implementation, preflight and evaluation instructions

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
