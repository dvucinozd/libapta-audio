# Plan nastavka nakon pregleda HS01–HS06

Datum: 2026-10-27. Status: **PLAN_ONLY; zadaci RC-01–RC-10 nisu izvršeni.**
Repo: `D:/AI/LIBAPTA/libapta-audio`; grana: `rust-rewrite`.
Pregledana baza: `9d33af765ed8b5fd4f6d581f5d361ea9108bf3a0`.
[Strojni plan](PRODUCT-CONTINUATION-PLAN-20261027.json).

## 1. Svrha, autoritet i granica ovog zahtjeva

Zadržati napisane Rust komponente, zatvoriti HS06 i zatim odlučiti može li
cjelovita analiza zadovoljiti stvarni M1/P4 proizvod. Ne produžavati niz malih
sintetičkih pokusa bez veze s preprekom do isporuke.

Ovaj dokument određuje **redoslijed nastavka nakon navedenog pregleda**.
Ne mijenja zamrznute protokole, tolerancije, povijesne rezultate, korisnički
opseg ni [izvorni produktni plan](PRODUCT-FIRST-IMPLEMENTATION-PLAN-20261026.md).
RC oznake su radni paketi ovog nastavka, a ne nova obitelj DSP eksperimenata.
Numeriku HS06 i dalje određuje njegov protokol. Produktne uvjete određuju
[opseg](PRODUCT-SCOPE.md), [budžeti](PRODUCT-BUDGETS.md) i
[ugovor grida](PRODUCT-GRID-CONTRACT.md). PF faze ostaju autoritet za kasniju
glazbenu evaluaciju, native integraciju i fizičku kvalifikaciju.

Aktualni sljedeći zadatak: **RC-01, uskladiti promjenjivi status**.
RC-02 je već registrirano izvršavanje, ne nova registracija ili implementacija.
Pisanje ovog plana, poveznice u handoffu i objava plana ne izvršavaju RC-01.
Ovaj zahtjev završava provjerom dokumenata, selektivnim commitom i pushom.

## 2. Činjenice koje model mora sačuvati

| Područje | Stanje na bazi pregleda | Što nije potvrđeno |
|---|---|---|
| HS01 | Sintetički host HMM: score, trace, replay, puni broj redaka | P4 brzina, cijeli glazbeni proizvod |
| HS02 | Putanja -> događaji/grid/metar; native/wire provjere | Točnost detektora na glazbi |
| HS03 | Ograničeni FIR resampler 44,1/48 kHz -> 22,05 kHz | Cijeli produktni media/codec lifecycle |
| HS04 | Frontend, FFT i 288 značajki; završeni sintetički pokus | Potpuni neuronski proizvođač |
| HS05 | Umjetni 3->2, dva LSTM sloja širine 2, head3 | Conv/pool, projekcija278->150 i četiri LSTM150 |
| HS06 | Implementacija i preflight prolaze | Četiri čista registrirana mjerenja još NOT_RUN |
| Core | Postojeći no_std rezultati, waveform, wire i ownership | Novi cjeloviti product_analysis koordinator |
| Uređaj | JC1060P470C_I_W_Y; posljednji odgovor: nedostupan | Stvarna revizija, raspoloživi budžeti i exact-image test |
| Model | Statički vezan kandidat; prava/provenijencija otvoreni | Dopuštenje i neovisnost konkretnog checkpointa |

Potvrđeni produktni ciljevi ostaju: waveform/BPM/grid/metar, najviše600s,
44,1/48kHz mono/stereo, prvi korisni waveform/BPM<=5s, konačni grid/metar<=60s,
cancel do prestanka rada i blokade objave<=50ms. Key i kalibrirani confidence
ostaju kasnije. Nijedan cilj nije potvrđen P4 mjerenjem. Nula confidencea nije
kalibracija. Za prvi puni ulaz koristiti WAV/PCM; MP3/FLAC zahtijevaju zaseban
decoded-PCM bridge prije tvrdnje da je cijeli ulazni opseg podržan.

Povijesni snapshotovi `REGISTERED_NOT_IMPLEMENTED` nisu aktualni status.
Ne prepisivati ih. Aktualno stanje vodi `PRODUCT-FIRST-STATUS.json`.

## 3. Pravila izvršavanja za model koji preuzima

1. Čitati vrh `MIGRATION.md`, ovaj plan/JSON, lokalni `AGENTS.md` i skill
   `libapta-dsp-development`. Korisnikov `rust-rewrite` ima prednost nad starom
   napomenom o grani1.1.0. `D:/AI/LIBAPTA` nije Git repo.
2. Provjeriti checkout, puni SHA, lokalne promjene, worktreeove i remote.
   Sačuvati tuđi rad. Ne koristiti reset/clean/force push ili prebacivati granu.
3. Na „nastavi” odraditi prvi spreman nezavršen paket, njegove provjere,
   dokumentaciju i commit/push. Prije novog paketa potvrditi remote SHA.
   Registracija i kandidatsko mjerenje imaju vlastite čiste commit granice.
4. Napredak spremati u `PRODUCT-FIRST-STATUS.json.continuation_progress`.
   Ovaj plan i njegov JSON su početni snapshot. Ne održavati dva promjenjiva
   popisa napretka niti retroaktivno označavati plan kao izvršni dokaz.
5. Koristiti postojeće alate iz `tools/`; ne pisati zamjenski scorer/supervisor.
   Ako je potrebna nova mogućnost, to navesti u odgovarajućoj registraciji.
6. Bez nove dozvole ne slati poruke autorima, otvarati issue/PR kod njih,
   trenirati, birati drugi model, kvantizirati, offloadati ili aktivirati provider.
   Već odobrene rutinske radnje, provjere i popravci ne traže ponovnu potvrdu.
7. Nedostajući hardver/dopuštenje ostaje UNKNOWN/BLOCKED. Dovršiti neovisan
   posao; nakon toga navesti konkretan nedostajući ulaz. Ne vrtjeti iste
   provjere, ponavljati pitanje svaki turn ili izmišljati novi HS pokus.
8. Držati VERSION1.0.1, default, core/wire ugovore, stare odbijene/spent banke
   i privatne podatke neizmijenjenima. Novi opt-in kod nije produkcijska objava.
9. Ne koristiti procjenu postotka dovršenosti iz broja testova/dokumenata.
   Izvijestiti koja je produktna prepreka zatvorena i što ostaje neprovjereno.

Početna provjera u PowerShellu, u stvarnom repou; provjeriti svaki exit code:

```powershell
git status --short --branch
git rev-parse HEAD
git worktree list
git fetch --prune origin
git rev-list --left-right --count HEAD...origin/rust-rewrite
```

Ako je remote napredovao, pregledati nove commitove prije nastavka. Ne miješati
novu implementaciju s već registriranim mjerenjem bez izričito zabilježene odluke.

## 4. Redoslijed i granice

| Paket | Isporuka | Ovisnost | Početno stanje |
|---|---|---|---|
| RC-01 | Dosljedan aktualni status | Ovaj objavljeni plan | READY |
| RC-02 | Čisti HS06 pokus i audit rezultata | RC-01, neizmijenjena implementacija/protokol | WAITING_DEPENDENCY |
| RC-03 | Jedna cjelovita odluka-priprema: resursi, prava, plan mjerenja | RC-01; RC-02 mora završiti izvještajem prije redovitog nastavka | WAITING_DEPENDENCY |
| RC-04 | Fizički budžeti i reprezentativan target probe | RC-03 i stvarno dostupan točan uređaj | BLOCKED_EXTERNAL |
| RC-05 | Obrazloženi GO / NO_GO / WAIT za sljedeću implementaciju | RC-03; pozitivan GO zahtijeva RC-04 i zatvorena prava/provenijenciju | WAITING_GATES |
| RC-06 | Registrirana puna numerička host implementacija | RC-02 PASS i pozitivan RC-05 | WAITING_GATES |
| RC-07 | Cijeli host put, razvojna glazbena provjera i neovisni transfer | RC-06 i registrirani PF04–PF07 uvjeti | WAITING_GATES |
| RC-08 | Native koordinator i target port prihvaćenog puta | RC-07 i potvrđeni budžeti; PF08 | WAITING_GATES |
| RC-09 | M1 integracija, codec opseg i fizička kvalifikacija | RC-08; PF09–PF10 i consumer uvjeti | WAITING_GATES |
| RC-10 | Završni zapis isporučivosti | RC-09 i kriteriji stvarnog kanala; PF11 | WAITING_GATES |

RC-02 neuspjeh ne sprečava dokumentacijsku inventuru RC-03, ali ostaje vidljiv
bloker za RC-06. U RC-03 smije se završiti priprema s nepoznatim podacima.
Završena priprema nije GO. Ako ploča ili prava nedostaju, RC-05 može zapisati
WAIT prije RC-04; time RC-04 nije završen niti je dozvoljen veći port.

## 5. RC-01 — uskladiti aktualni status bez prepisivanja povijesti

**Datoteke:** samo `PRODUCT-FIRST-STATUS.json` i aktualni vrh `MIGRATION.md`.

Postupak:

1. Provjeriti postojeće HS04/HS05 result JSON/MD, source commitove i njihove
   hashove u statusu. Ne ponavljati pokuse radi administrativnog popravka.
2. Ispraviti `execution.HS04_candidate_executed` na true i
   `execution.HS04_registration_only` na false jer završeni dokaz postoji.
   Provjeriti ostala execution polja; mijenjati samo dokazano zastarjela.
   Polja `*_this_step` ne pretvarati u kumulativne tvrdnje bez objašnjenja.
3. Dodati dosljedna eksplicitna HS04/HS05 polja dovršenog sintetičkog mjerenja
   ako su potrebna za strojno čitanje. HS06 measured-run ostaje false.
4. Dodati `continuation_progress`: ID plana, RC-01 COMPLETE, ostali NOT_RUN/
   BLOCKED s razlogom, te sljedeći RC-02. Za dovršene stavke navesti putanju
   dokaza i puni source SHA; za buduće koristiti null, nikad izmišljeni SHA.
5. U aktualnom handoffu postaviti RC-02, uz poveznicu na ovaj plan.
   Povijesna poglavlja s nekadašnjim „Current next” ne koristiti za dispatch.

**Provjera:** JSON parse; slaganje aktualnih polja s result receiptovima;
hashovi postojećih referenci; `git diff --check`; diff smije sadržavati samo
dvije namijenjene datoteke. Nema DSP testova za ovaj statusni popravak.
**Done:** povijest sačuvana, aktualna polja nisu proturječna, commit/push/SHA
provjereni. Commit prijedlog: `Align product status with completed HS04 and HS05 evidence`.

## 6. RC-02 — dovršiti već registrirani HS06, bez novog kandidata

**Ulazi:** [protokol](PRODUCT-HOST-PROBABILITY-PROTOCOL.md),
[registracija](PRODUCT-HOST-PROBABILITY-PROTOCOL.json),
[implementacijski receipt](PRODUCT-HOST-PROBABILITY-IMPLEMENTATION.json),
`tools/apta_compact_probabilities_synthetic.py` i postojeće javne fixtures.

### RC-02a: provjera i novi čisti build

1. Checkout mora biti čist i pushed. Izmjerena revizija bit će stvarni HEAD
   nakon dokumentacijskih checkpointova, ne automatski9d33af7. Zabilježiti oba:
   originalni implementation SHA i stvarni measured-source SHA.
2. Svih šest `component_sha256` iz implementation JSON mora odgovarati živim
   datotekama. Protokol SHA mora biti
   `50082ad1d7a9fccac4b939cc42f7f4709b78eff0735bdc8ef221b07dc12a5650`.
   Provjeriti svih395 bindinga postojećim verifierom. Njegov `candidate=NOT_RUN`
   odnosi se na registracijski verifier, ne briše implementacijski dokaz.
3. Ne mijenjati niti ponovno stvarati fixtures. Ako hashovi ne odgovaraju,
   stati i opisati drift. Ne popravljati expected vrijednosti prema kandidatu.
4. Koristiti WSL/Linux i postojeći toolchain. Posljednji stvarni Rust/Cargo
   bio je1.95.0, a crate deklarira minimum1.81; jedno nije dokaz drugoga.
   Bez instalacije/promjene ovisnosti. Ako alat nedostaje, zapisati prelaunch
   blocker; ne zamijeniti backend ili OS usred mjerenja.
5. Odabrati novi jedinstveni ID pokušaja, npr. `hs06-measured-YYYYMMDD-HHMMSS`.
   Build: `/home/daniel/libapta-<ID>-build`; bin:
   `/home/daniel/libapta-<ID>-bin`; output:
   `/mnt/d/AI/LIBAPTA/outputs/<ID>`. Svi moraju biti novi i izvan checkouta.
   Ako bilo koji postoji, odabrati drugi ID; ne brisati/prepisivati.
   To je izbor prije pokušaja, ne dopuštenje za ponavljanje neuspjelog pokusa.
6. Sačuvati build log i binarni hash izvan source treea; u auditu navesti
   njegove putanje/hashove. Build metadata odvojiti od ograničenog direktorija
   izvještaja, a sve retained run receipts brojiti prema registriranom1MiB capu.

Predložak naredbi u WSL bashu, gdje izvršitelj prvo zamijeni `<ID>` doslovnom
jedinstvenom vrijednošću. Izvršavati redom i stati na svaki neuspjeli exit code:

```bash
cd /mnt/d/AI/LIBAPTA/libapta-audio
python3 docs/rust/fixtures/product-host-probability-hs06/verify_registration.py
/home/daniel/.cargo/bin/cargo build --locked --offline --release \
  --manifest-path rust/runtime/Cargo.toml \
  --features experimental-compact-probabilities \
  --bin apta-compact-probabilities-synthetic \
  --target-dir /home/daniel/libapta-<ID>-build
```

`--locked --offline` ne mijenjaju DSP profil; ako cache nedostaje, prijaviti
nedostajuću ovisnost prije pokusa. Ne koristiti automatsku instalaciju kao fallback.
Kopirati upravo izgrađeni executable u novi bin direktorij, provjeriti da su
source/copy SHA-256 jednaki. Ne koristiti stari preflight executable.
Zabilježiti compiler/Cargo verbose verzije i stvarne feature zastavice.

### RC-02b: jedno izvršavanje

```bash
python3 tools/apta_compact_probabilities_synthetic.py \
  --executable /home/daniel/libapta-<ID>-bin/apta-compact-probabilities-synthetic \
  --output /mnt/d/AI/LIBAPTA/outputs/<ID>
```

Ne dodavati `--preflight` ili `--worker`. Runner sam pokreće provjere transporta
i resource-refusal probeove pa točno ove slučajeve jednom, redom:

| Slučaj | Očekivani opseg | Ne smije se tvrditi |
|---|---|---|
| fixtures |35 opažanja,850 konačnih redaka,11 selektora,8 malih kompozicija | Da je time testirana cijela pjesma |
| full-mixed |30000 redaka,150001 jedinica,Ready; svaka gustoća provjerena | Da je izvršen puni HMM/neural pipeline |
| full-no-support |30000 redaka,150001 jedinica,NoEvidence,bez HMM-a | Da je prazan izlaz ispravan glazbeni grid |
| cancel |138 prihvaćenih/137 committed redaka,686 jedinica,NonBeatLog,bez rezultata | Asinkroni ili produktni P4 cancel |

Po slučaju: CPU30s za worker+waited descendants, wall45s, RSS64MiB/process,
AS128MiB. Ukupno retained run artifacts<=1048576B. Konačni potpuni numeric
redci:60850; ne brojiti cancel/invalid prefikse kao certificirane redke.
Pending cancel<=50ms je samo točno definirani host test.

**STOP:** pri fail/timeout/hash promjeni/nepotpunom skupu sačuvati direktorij,
stderr, partial receipts i sve završene/nezavršene slučajeve. Ne ponovno pokretati
runner, birati uspješan podskup, širiti cap, mijenjati toleranciju ili koristiti
drugi backend. Razlikovati SYNTHETIC_FAIL od INVALID/INCOMPLETE. Ispravak
implementacije zahtijeva zaseban opis problema, test i čisti checkpoint; ponovni
stvarni pokušaj mora slijediti izričito dokumentiranu dopuštenu retry odluku.

### RC-02c: audit i objava zasebnog rezultata

Nove datoteke: `PRODUCT-HOST-PROBABILITY-RESULT.md/.json` u `docs/rust/`.
Promjenjive: `PRODUCT-FIRST-STATUS.json`, aktualni vrh `MIGRATION.md`.
Stare protocol/implementation/result datoteke ostaju byte-identične.

Audit mora provjeriti, ne samo prepisati završnu riječ PASS:

- sva četiri imena i redoslijed, kompletne denominatore i očekivane statuse;
- source/šest komponenti/protokol/bin/toolchain/runtime identitete; sve worker
  runtime datoteke bile su prebound; bindinge ponovno provjeriti;
- numeric/bit/support/work/event/selector kontrole i nepromijenjene tolerancije;
- kompletan worker+descendants CPU, wall/RSS/AS, sve refusal probeove;
- vrijeme Session.step, composition, pipe/checker i cancel sa stvarnim scopeom;
- izlazne array kapacitete i layout odvojeno od process RSS i fizičkog peaka;
- sve retained datoteke, hashove i ukupne byteove, uključujući manifest i završni
  audit ako su dio retained attempta; ne izuzeti izvještaj samo zbog capa;
- čist i nepromijenjen source prije/poslije pokusa; nema novog modela/corpusa/devicea.

Manifest je nova ekskluzivna datoteka; jasno navesti uključuje li broj datoteka
sam manifest. On ne hashira sam sebe; finalni receipt veže njegov hash.
Po potrebi audit smjestiti uz direktorij uz eksplicitno zajedničko accounting.
U Git spremiti agregat, identitete i putanje; bez raw tensor/audio/private podataka.

**Done:** zasebno auditiran puni PASS ili pošteno sačuvan neuspjeh; commit/push
rezultata prije RC-03. PASS dopušta tvrdnju `HS06_SYNTHETIC_HOST_PASS_ONLY`.
PF04/music/P4/consumer ostaju NOT_RUN. Ne ponavljati sve stare testove kada se
izvori nisu mijenjali; koristiti hash-bound prethodne testove i navesti njihov
scope, bez predstavljanja kao novih izvršavanja.

## 7. RC-03 — jedna cjelovita priprema odluke o izvedivosti

**Isporuka:** novi `PRODUCT-ROUTE-DECISION-20261027.md/.json` i
`PRODUCT-TARGET-PROBE-PROTOCOL.md/.json`; status/handoff. To su planirana imena.
Ne prepisivati zamrznute BUDGETS/FULL-GRAPH-READINESS/RIGHTS snapshotove.
Novi receipt ih referencira hashom, uz nova opažanja odvojeno od starih.

### RC-03a: tablica živih vlasnika memorije

Za svaki owner navesti: izvor/liniju/reviziju, broj elemenata, veličinu, alignment,
payload, stvarnu ili nepoznatu allocation capacity, memory class, trenutak
alokacije/oslobađanja, borrow koji produžuje život i mogućnost preklapanja.
Razdvojiti internal RAM/PSRAM/DMA/flash; ne prebacivati owner u PSRAM bez
target dokaza o pristupu, latenciji i DMA/cache pravilima.

Obvezni owneri: source/codec/cache; resampler; frontend/tables; model weights;
activations i hidden/cell/pending state; probabilities; densities; oba score
prolaza i winner trace; graph/map; events; native rezultat; retained/pending
consumer kopije; waveform banke; audio/USB/UI; stack/futures/allocator/reserve.

Napraviti timeline najmanje za admit, source/frontend/neural, score3, score4,
trace, postprocess, publish, cancel i cleanup. Peak računati kao maksimum
istodobno živih alokacija po memory class, a ne zbroj svih mogućih scenarija.
Nula je dopuštena samo ako je dokazano da owner tada ne postoji; nepoznato je null.

Provjerljive polazne brojke:

- 30000 redaka ->360000B probabilities +720000B densities;
- winner4 trace16575840B uključuje60000B putanje;
- graph numeric235304B, map33660B, event scratch24576B;
- opisani postojeći scenarij17949380B bez PF01 overlap ili18848270B s898890B
  overlap točno jednom; to nisu izmjereni peakovi;
- 767125 f32 parametara ->3068500B težina, ako stvarno ostaju rezidentne;
- path posuđuje Workspace: završetak HMM računanja ne oslobađa taj owner.

Kao odvojene **neimplementirane alternative** procijeniti: izdvajanje owned
putanje prije postprocessa, lazy/recomputed densities te drukčiji traceback.
Za svaku zapisati uštedu, dodatni compute/I/O i točan ugovor koji bi se mijenjao.
Ne zbrojiti međusobno nespojive uštede niti ih uračunati u osnovni scenarij.
Ne implementirati optimizaciju u zamrznuti HS01/HS06 radi uljepšavanja budžeta.

### RC-03b: računanje, rokovi i nedostajući put

Provjeriti formule iz postojeće inventure: Conv5580 + projection41700 +
LSTM720000 + head450 =767730 MAC/row; za30000 redaka23031900000 MAC.
383865000 MAC/s je potreban prosjek ako bi svih60s bilo dostupno samo tim
operacijama; nije ostvareni throughput. Posebno evidentirati nelinearnosti,
FFT/FIR, HMM, media I/O, setup, hashes/source passes, publication i coexistence.

Postojeći HS04 host step20,561–20,646s i HS01 full worker14,628/16,634s
imaju različite scopeove. Navesti ih s tim scopeovima, bez zbrajanja u izmjereni
pipeline rok ili pretvaranja u P4 procjenu. Ne skalirati toy LSTM vrijeme.

Za5s prvi rezultat odrediti koji postojeći waveform/BPM put daje korisnu
provisional generaciju prije konačnog grida. Ne predstavljati prazni progress
kao rezultat. Za60s početak je prihvaćanje posla, ne početak posljednjeg kernela.
Za50ms uključiti stvarno zaustavljanje i publication fence; cleanup ima zaseban
još neodobren budžet koji ne smije skrivati blokiranje playbacka.

### RC-03c: prava i provenijencija

Pročitati postojeći rights receipt. Ako se radi svjež javni pregled, koristiti
službeni source/repository i zapisati URL, vrijeme, revision/blob/digest i
zaključak; povijesni nalaz ne predstavljati kao novo stanje upstreama.
Ne učitavati/izvršavati model ni otvarati audio/holdout.

Potrebno: affirmative terms za točan kod i checkpoint digest, nositelj prava,
use/modification/redistribution opseg i attribution; zasebno checkpoint-specific
training/validation/test/selection lineage i podaci za recording-level overlap.
Prava za članak ili dostupna datoteka ne zatvaraju ovaj uvjet. Privatne popise
čuvati izvan Gita. Pripremiti konkretnu poruku/upit kao lokalni nacrt ako podaci
nedostaju; slanje samo nakon izričite korisničke autorizacije, bez ponovnog
traženja ako je već dana. UNKNOWN ne postaje independent transfer PASS.

### RC-03d: registracija malog target probea

Pripremiti izvediv protokol koji NE zahtijeva veliki port ili trenirane težine.
Prvo pregledati postojeće consumer probeove i hardware runbook na točnoj
reviziji. Za svaki slučaj navesti konkretan file/target/command, fixed synthetic
input, broj iteracija, timer, očekivani checksum/numeričku referencu, allocation
placement, cap, repeat policy, telemetry i uvjet prekida. Ako nešto nije poznato,
prije izvršavanja ostaje UNRESOLVED; ne isporučiti naredbu s izmišljenim COM portom.

Probe treba obuhvatiti baseline resident proizvod, raspoloživi/largest-contiguous
internal/PSRAM, stack high-water, memory bandwidth i relevantnu floating-point
aritmetiku na umjetnim podacima. Representative kernel work ograničiti unaprijed;
ne kopirati cijeli std runtime ili graditi puni neural graph radi samog probea.
Ako target math/API još ne postoji, to je eksplicitan mali implementacijski dio
registracije s referentnim testom, buildom i zasebnim freezeom prije mjerenja.

**Done RC-03:** svi resursi imaju dokaz ili UNKNOWN; prava imaju dokaz ili jasan
nedostajući dokument; target protokol ima konkretne provjere i blokere;
tablica odluke i idući dopušten korak postoje. Commit/push. Sama priprema ne
zatvara PF01. Ne stvarati još jedan identičan pregled ako nema novih ulaza.

## 8. RC-04 — mali target probe i stvarni PF01 budžeti

**Ulazni uvjeti:** potvrđena dostupnost JC1060P470C_I_W_Y; stvarno očitana
revizija i konkretan port; točan firmware/board konfiguracija; recovery put;
RC-03 protokol bez otvorenih izvršnih parametara. Posljednji korisnički odgovor
„nije dostupna” nije dokaz današnje veze i ne smije se zamijeniti pretpostavkom.

1. Pripremiti najmanji registrirani probe u prikladnom postojećem target projektu.
   Prije izmjene consumer repoa provjeriti worktreeove i tuđi rad; ne mijenjati
   core pin/provider ili radnu main granu radi dijagnostike.
2. Provesti host/reference testove i target compile/link/map. Zabilježiti stvarni
   target/toolchain/features/linker placement. Build PASS nije boot PASS.
3. Commit/push probe implementacije prije fizičkog izvršavanja. Flash/boot prema
   stvarnom runbooku i session autorizaciji; ne nagađati adresu/port/image.
4. Mjeriti registrirane slučajeve na exact imageu, s resident UI/audio/USB
   ownerima koji postoje u proizvodu. Stopped-deck ne znači da su ti owneri
   oslobođeni. Bilježiti free/minimum/largest block, stog, overflow/underrun,
   quiescence/cleanup i workload scope odvojeno od sintetičke brzine.
5. Izmjereni slobodni prostor pretvoriti u odobren analysis allowance tek uz
   eksplicitan safety/fragmentation/resident reserve i njegovu osnovu. Ne uzeti
   svih slobodnih bajtova niti izmišljeni postotak kao automatski budžet.
6. Napisati `PRODUCT-P4-BUDGET-RESULT.md/.json`, exact binary hash, board/revision,
   image/config, log/manifeste i devet PF01 kategorija. Ne zatvarati neizmjerene.

**Done:** reproducibilan evidence paket i provjeren commit/push. Target probe
potvrđuje samo svoj scope; whole pipeline60s i music još nisu PASS.
Ako ploča nije dostupna, završiti bez flasha s BLOCKED_EXTERNAL i jednim jasnim
potrebnim ulazom. Ne ulaziti u RC-06 zato da se popuni vrijeme čekanja.

## 9. RC-05 — eksplicitna odluka prije većeg ulaganja

Aktualizirati RC-03 decision dokument novim odvojenim opažanjima i hashovima.
Ne mijenjati njegove stare izmjerene podatke. Zaključak mora biti jedan od:

| Odluka | Obvezno obrazloženje | Sljedeće |
|---|---|---|
| WAIT_EXTERNAL | Nedostaje board allowance, prava ili provenijencija; navesti vlasnika/ulaz | Čekati taj ulaz, bez novog modela/pokusa |
| NO_GO_CURRENT_ROUTE | Dokazan resursni/ugovorni problem s izvorom i scopeom | Jedan reviewable prijedlog promjene za vlasnika |
| GO_SCOPED_HOST_IMPLEMENTATION | Zatvoreni prerequisitei, konzervativni resursni scenarij i provjerljiva target osnova opravdavaju sljedeći opseg | Registrirati RC-06; nije product GO |

GO zahtijeva: HS06 PASS, zatvorena prava i provenijenciju, izmjerene/odobrene
PF01 allowancee, jasno definirane live owners i izvediv plan provjere punih
dimenzija. Mikrobenchmark ne dokazuje60s; odluka mora razlikovati izmjereno,
konzervativno izvedeno i još neizmjereno te imati veto za RC-06/07.

Ako konzervativni proračun ne stane ili nema dovoljno podataka, ne odabrati GO.
NO_GO može voditi samo u konkretan prijedlog: štedljiviji decoder uz očuvanu
numeriku, manji model ili promjenu produktnog opsega/arhitekture. Nove modele,
kvantizaciju, trening, offload ili slabije rokove ne uključivati automatski.
Navesti očekivanu korist, rizik točnosti, potrebne nove dokaze i što se ponovno
kvalificira. Korisniku dati odluku tek kada je prijedlog konkretan.

## 10. RC-06 — puni neuronski host put, samo nakon GO

Ovaj paket je uvjetovani provedbeni okvir, **nije registracija novog kandidata**.
RC-05 mora odabrati algoritam/backend i resursne veto uvjete prije koda.
Koristiti PF04 artefakte iz izvornog plana, ne paralelnu evaluacijsku obitelj.

### RC-06a: zaključati specifikaciju i oracles; commit/push

Za zadržani kandidat iz source-bound modela potvrditi točne operacije:
Conv1d1->2,k10,288->279; ReLU; pool2x279->2x139; channel-major flatten278;
projection278->150 s biasom; četiri unidirectional LSTM150 s oba biasa;
head150->3; softmax i imenovane beat/downbeat/nonbeat klase.
Ako izabrana ruta promijeni model, te dimenzije više nisu autoritet i moraju
dobiti novu registraciju; ne prepisati stare dokaze.

Registracija mora odrediti dtype/akumulacijski redoslijed/backend, weights
layout, Conv orientation, signed-zero/ReLU, pool ties/odd tail, flatten order,
gate order, recurrent reset, centered context/EOF, output sum i HS06 prijem.
Pragovi/parity i capovi moraju biti brojčano navedeni prije izvršavanja; ovaj
plan ih namjerno ne izmišlja. HS06 guard2^-21 ostaje dok važi postojeći ugovor.

Za svaki stage: mali nezavisni matematički oracle, full-shape javni umjetni
slučaj, malformed/capacity/EOF/cancel scenariji i cross-chunk state test.
Odvojiti očekivane izlaze/scorer od konstruktora. Puni weights/checkpoint run
dozvoljen tek u točno registriranom opsegu uz zatvorena prava.

### RC-06b: implementirati jedan povezani opseg; test/freeze/push

Planirani novi modul npr. `rust/runtime/src/product_neural.rs`, feature
`experimental-product-neural`, test i fixed CLI/runner. To su predložena imena;
registracija mora zamrznuti točne putanje. Ne širiti hardcoded HS05 Profile i
ne promijeniti stare protokole/kernela radi novih dimenzija.

Podzadaci redom: checked storage/geometry -> Conv/ReLU/pool/flatten -> projection
-> LSTM state/pending commit -> head/softmax ->288-feature ulaz i HS06 izlaz.
Svaki tick mora imati definirani omeđeni rad; dugi dot/matrix posao ne smije
postati jedan neprekidivi „unit”. Nema allocationa po retku, whole feature
tensor zadržavanja ili ponovnog RNN izvršavanja radi HMM replaya.

Testirati primitive/full-shape parity, class order, reset vs carry, quotas,
source gaps/duplicates, bad inputs, oba kratka buffera, canaries, cancellation
u svakoj fazi i nepovratnu zabranu objave; zatim HS04->full neural->HS06 spoj.
Ne padati na toy fallback ako puni stage nedostaje. Sačuvati explicit unsupported.
Provesti relevantne kontrole, fmt/Clippy/targeted tests; zasebno navesti MSRV
provjeru ako se tvrdi podrška1.81. Default ponašanje ostaje nepromijenjeno.

### RC-06c: novi čisti registrirani slučajevi; audit/push

Pokrenuti isključivo novi registrirani redoslijed/capove, ne HS06 runner nad
drugim workloadom. Zabilježiti full-shape compute, lifetimes i stvarne peakove
u njihovom scopeu. PASS znači numeričku/operativnu host podudarnost, ne glazbu.
Ako resursni ili numeric veto padne, sačuvati rezultat i vratiti odluku RC-05;
bez automatskog mijenjanja backend/tolerance/shape ili novog pokušaja.

## 11. RC-07 — cijeli host posao i glazbena provjera (PF04–PF07)

**Ulazi:** RC-06 PASS, dopušten konkretan checkpoint, source-bound pre/postprocess,
identiteti audio/labela, registriran development/transfer/holdout plan.
**Datoteke:** PF04 `PRODUCT-EXPERIMENT`, PF05 `PRODUCT-PREFLIGHT`, PF06
`PRODUCT-DEVELOPMENT-RESULT`, PF07 `PRODUCT-TRANSFER` iz izvornog plana.

1. Sastaviti host put WAV/streamed PCM -> resampler -> frontend -> puni model
   -> HS06 -> score3/score4 -> winner trace -> HS02 -> validirani native rezultat.
   Sačuvati puni source N/fs, trim origin, future context, ekskluzivni EOF i
   lifetime svih ownera. Prvi end-to-end test javni je umjetni signal; ne koristiti
   njegov prolaz za tvrdnju o glazbenoj točnosti.
2. Registrirati točan cijeli posao: svi prolazi, admission/first-useful/final
   clocks, source identity, cancel i publication fence. Cijeli waveform/BPM
   proizvod ne smije čekati da se završi samo posljednji testni adapter.
3. Provesti sintetičku preflight integraciju i malformed/replacement/truncation
   slučajeve. Freeze/push prije stvarne glazbene evaluacije.
4. Evaluirati label-blind konstruktor na dopuštenom razvojnom skupu, postojećim
   scorerom i unaprijed određenim produktnim kriterijima. Posebno prikazati
   waveform/BPM/beatgrid/metar/downbeat, unsupported i unknown denominatore.
   Event F1 ne zamjenjuje full-source grid/period/phase contract.
5. Nakon development PASS zasebno provjeriti recording/training/selection
   neovisnost transfera i izvršiti registrirani transfer. GTZAN/spent/finalni
   stari corpus ne postaju svjež acceptance set promjenom naziva ili hashova.
6. Ako musical gate padne, REJECTED; integrity/timeout je INVALID/INCOMPLETE.
   Nema label-driven tuninga na istom formalnom holdoutu. Zasebni izvještaji/
   commit granice za development i transfer; ne spajati ih u jednu tvrdnju.

**Done:** cijeli host put i traženi razvojni/transfer uvjeti prolaze; objavljeni
točni kriteriji/denominatori/identiteti. Key/confidence kasnije; puni APTA1.1
release i svježi formalni acceptance ostaju zasebni od prvog native M1 opsega.

## 12. RC-08 — native koordinator i target port (PF08)

Prvo `PRODUCT-NATIVE-API.md`, ownership/lifetime dijagram i storage contract;
zatim feature-gated implementacija. Planirane putanje su
`rust/src/product_analysis/{mod,storage,job}.rs` i
`rust/tests/product_analysis.rs`; prije koda ih potvrditi prema aktualnom coreu.
Ponovno koristiti postojeće publication/session/wav-streaming/result buildere;
ne uvoditi drugu generacijsku/bank implementaciju.

Predloženi koncepti, čije se stvarne Rust signature zamrzavaju u API dokumentu:

```text
required_storage(profile, geometry) -> checked plan OR explicit unsupported
begin(validated plan, caller storage, source identity) -> job OR busy/error
feed(source offset, bounded PCM) -> consumed count/backpressure/error
step(work quota, cancel signal) -> bounded progress
finish_input() -> EOF validation and drain, never fabricated tail
cancel() -> quiescence and no later publication for that generation
result() -> immutable certified result only at allowed terminal state
```

Ne unositi `std`, filesystem ili OS timer u no_std jezgru. Izabrani target math
mora proći registriranu numeričku provjeru; host libm nije automatski P4 parity.
Caller-owned storage i memory placement slijede RC-04 allowancee, uz checked
capacity prije writes. Ne pretpostaviti da lokalni f64/Vec code samo recompilira.

Obvezni integracijski testovi: busy admission; source replacement/truncated EOF;
read/decode failure nakon provisional izlaza; cancel prije/u svakom stageu i
između finalizacije i objave; novi posao dok reader drži stari rezultat;
rezultat preživi scratch reuse; kratka memorija ne ošteti staru generaciju;
maximum duration/event/segment limits;44,1/48kHz mono/stereo i exclusive EOF.
Otkazivanje ne smije obrisati raniji prihvaćeni rezultat ili dopustiti stale publish.

**Done:** native API, relevante host/target compile i numeric kontrole prolaze;
storage plan odgovara stvarnim alokacijama, lifecycle je dokazan testovima.
Commit/push. Target build sam ne zatvara RC-09 fizičku kvalifikaciju.

## 13. RC-09 — M1 consumer, ulazi i exact-image kvalifikacija (PF09–PF10)

**Datoteke:** `PRODUCT-INTEGRATION.md`, `PRODUCT-P4-RESULT.md/.json` te točne
consumer datoteke tek nakon svježeg read-only pregleda na pinned reviziji.
Snapshot4a8c660/core pin25d09de je povijesni kontekst, ne nalog za checkout.

1. Provjeriti aktualni M1 architecture/requirements i M1R-P1-001/upstream
   embedded/release uvjete. Ne mijenjati pin ili zadani provider prije zatvaranja
   njihovih stvarnih uvjeta. Eksperimentalni harness ne zaobilazi ovu odluku.
2. U izoliranom prikladnom consumer checkoutu integrirati prihvaćeni native
   API s TrackAnalysis/adapterom, stopped-deck media leaseom, playback-start
   cancellationom, waveform/beat bankama i generacijskom publikacijom.
3. WAV/PCM prvo. MP3/FLAC zasebni bounded decoded-PCM adapter: rate/channels/
   frames/EOF/source identity, decode/read errors, seeks/passes/cancel i cache
   ownership. Bez whole-file PCM buffera ili prikrivenog unlimited cap-a.
4. Registrirati fizički workload na točnom board/imageu: podržani minimalni i
   maksimalni izvori, stvarni codec putevi, retained result, memory pressure,
   UI/USB/audio coexistence, playback-start, source removal i ponovljeni poslovi.
   Točne slučajeve/broj ponavljanja/capove zamrznuti prije izvršavanja.
5. Mjeriti od prihvaćenog zahtjeva: prvi korisni waveform/BPM<=5s, konačni puni
   grid/metar<=60s, cancel/quiescence/publication fence<=50ms. Cleanup zasebno
   prema prethodno odobrenom budžetu. Potvrditi memoriju/largest-block/stog i
   odsutnost neželjenih audio/USB posljedica prema registriranim kriterijima.
6. Sačuvati exact image/toolchain/config/board revision/source/input hashes,
   telemetry i operator opažanja. Host test, target build, boot, listening i
   fizički stress/cancel su zasebne stavke; preskočena stavka nije PASS.

**Done:** svi obvezni kriteriji prihvaćenog opsega prolaze na exact imageu,
consumer ugovor zadovoljen i izvještaj/commit/push verificirani. Ako faila,
isključeni eksperimentalni put ostaje isključen i prijavljuje se konkretan veto;
ne smanjivati opseg pjesme ili mijenjati rok prešutno.

## 14. RC-10 — završetak prvog isporučivog opsega (PF11)

Ispuniti `PRODUCT-RELEASE-CHECKLIST.md` s linkovima na dokaz za svaki zahtjev:
native/wire, puni izvor, glazbeni razvoj/transfer, prava/provenijencija, consumer,
stvarna memorija,5s/60s/50ms, error/cleanup i podržani codeci. Nepokriven ulaz
mora biti eksplicitno unsupported i ne smije se oglašavati podržanim.

Razdvojiti prvi M1 native feature od potpune C ABI/API/CLI/platform zamjene
(zasebni PF12) i od stabilnog APTA1.1 releasea sa svježim formalnim acceptanceom.
Ne označiti cijelu Rust migraciju završenom samo zbog prvog M1 puta.
VERSION/tag/package/provider aktivacija slijede stvarnu release autorizaciju;
plan nije nalog za javnu objavu. Završni izvještaj navodi što radi, točan image/
commit, ograničenja i otvorene zasebne ciljeve, bez postotka „gotovo”.

## 15. Obvezni obrazac predaje svakog paketa

```text
package: RC-xx
source_revision: full SHA; razlikovati bazu plana i izmjereni izvor
changed_files: samo stvarno promijenjene datoteke
inputs_and_protocol: identiteti i putanje; privatni sadržaj izvan Gita
closed_obstacle: koji konkretni uvjet za proizvod je zatvoren
checks: stvarne naredbe, PASS/FAIL/IGNORED/NOT_RUN i scope
resources: DERIVED / HOST_MEASURED / TARGET_MEASURED / UNKNOWN
verdict: COMPLETE / FAIL / INVALID / BLOCKED_EXTERNAL / NEEDS_DECISION
remaining: obvezni uvjeti, vlasnik i dokaz koji nedostaje
next: prvi dopušten nezavršen paket, ili konkretan vanjski ulaz
publication: commit, tracking SHA i ls-remote SHA
```

Selektivni Git završetak nakon pregleda diff-a i primjerenih provjera:

```powershell
git diff --check
git diff --stat
# git add -- <isključivo pregledane putanje tog paketa>
git diff --cached --check
git diff --cached --stat
# git commit -m '<konkretan problem i rezultat>'
git push origin rust-rewrite
git rev-parse HEAD
git rev-parse origin/rust-rewrite
git ls-remote origin refs/heads/rust-rewrite
git status --short --branch
```

Ne koristiti `git add .`. Ako postoji tuđi rad, ne obećati čist cijeli checkout;
navesti što je ostalo i ne pokretati measured run koji zahtijeva čist izvor.
Za ovaj plan dovoljni su JSON/schema-like consistency, poveznice/hashovi i
diff provjera. Rust/corpus/device testovi nisu ponovno izvršeni pisanjem plana.
