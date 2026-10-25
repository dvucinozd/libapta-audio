# Plan nastavka: isporučivi Rust proizvod, algoritam i P4

Datum: 2026-10-26. **PLAN ONLY; nijedna faza implementacije nije izvršena.**
Repo: `D:/AI/LIBAPTA/libapta-audio`, grana `rust-rewrite`.
Polazni commit: `327ef2177888ef406e23d674d59c187e00f171ef`.
[Strojni plan i vezani izvori](PRODUCT-FIRST-IMPLEMENTATION-PLAN-20261026.json).

Ovaj plan mijenja REDOSLIJED budućeg rada: aktualni sljedeći korak je **PF-00**.
H1-P/H1-Q su odgođeni do odluke PF-04. Njihova registracija ostaje neizmijenjena;
odgoda nije izvršenje, odbacivanje rezultata ili dopuštenje za kraći H1 PASS.
DD, CT, P, MC i H0 rezultati ostaju povijesni dokazi sa svojim izvornim značenjem.

## 1. Upute modelu koji preuzima rad

1. Pročitaj ovaj dokument i JSON, aktualni vrh `MIGRATION.md`, `AGENTS.md` te
   `libapta-dsp-development` skill. Korisnikova grana `rust-rewrite` ima prednost
   pred starom napomenom o grani `1.1.0` u lokalnim uputama.
2. Provjeri stvarni checkout, puni HEAD, lokalne promjene, worktreeove i remote.
   Fetch je dopušten. Ne checkoutaj, resetiraj ili prepisuj tuđi rad.
   Ako je remote napredovao, prvo pregledaj razliku i uskladi nastavak bez force pusha.
3. Na zahtjev „nastavi” izvrši PRVI spreman nezavršen korak. Završi njegovu
   implementaciju, potrebne testove, dokumentaciju, selektivni commit i push.
   Usporedi puni lokalni/tracking/remote SHA. Ne preskači uvjete ovisnosti.
4. Za svaku tvrdnju koristi oznaku: IMPLEMENTED, MEASURED, DERIVED, UNKNOWN,
   NOT_RUN, PASS, REJECTED ili INVALID. Navedi predmet tvrdnje i dokaz.
5. Prvo traži činjenice u aktualnom kodu/dokumentaciji. Ako nedostaje odluka
   vlasnika proizvoda, postavi konkretno pitanje s poznatim alternativama.
   Ne izmišljaj dopuštenu latenciju, slobodnu PSRAM ili prihvatljiv pad točnosti.
   Nastavi nezavisan posao; ovisan posao čeka odgovor.
6. Ne otvaraj novi istraživački niz zato što je trenutni kandidat pao.
   Napiši razlog i najviše jednu konkretnu sljedeću hipotezu za odluku.
7. Rutinski popravak javnog testa ne traži novu dozvolu. Promjena arhitekture,
   modela, treninga ili ciljnog ugovora nije rutinski popravak.
8. Ovaj zahtjev završava planom i njegovom objavom. **Ne pokreći PF-00 sada.**

### Minimalni početni pregled

U PowerShellu, iz stvarnog repoa, izvršavaj naredbe i provjeri exit code:

```powershell
git status --short --branch
git rev-parse HEAD
git worktree list
git fetch --prune origin
git rev-list --left-right --count HEAD...origin/rust-rewrite
```

Consumer čitaj na točnoj reviziji, primjerice `git show <sha>:<path>`.
Lokalni `D:/AI/M1_PCB` checkout nije automatski aktualni `origin/main`.
Ne mijenjaj njegov branch, pin ili datoteke tijekom inventure.

## 2. Što već postoji i što nije dokazano

| Područje | Provjerena polazna činjenica | Posljedica |
|---|---|---|
| Rust core/runtime | PCM, waveform, container/wire, native rezultati, scheduler, ownership i default DSP postoje; core je `no_std`, bez unsafe | Ne pisati ih ponovno |
| Potpuna C zamjena | C ABI, dio platformi, CLI/package parity i dio numeričkih granica otvoreni | Native M1 cilj i potpuna C zamjena imaju odvojene završne kriterije |
| BeatNet model 1 | Pada sva četiri registrirana event/continuity kriterija | Nije kandidat za port samo zato što je malen |
| Beat This! final0 | Prolazi četiri event kriterija na izloženom GTZAN-u; OLS joint 187/401 i regresije | Offline referenca; nema prihvata proizvoda ni dokaza P4 izvedivosti |
| Dense DD-05 | REJECTED, 11/16 kriterija pada | Ne portati odbijeni dekoder kao završni algoritam |
| CT-05 | INVALID zbog vremena verifikacije; glazbena evaluacija nije izvršena | Nema glazbenog verdicta koji bi se smio izmisliti |
| MC-08 | Razjašnjena osjetljivost objavljene downbeat točke na referentni raspon; 17 prozora UNKNOWN | Nije detektorski dobitak; MC-03 ostaje odbijen |
| H0 | 11361/11362 programa jednako; promijenjen zlib i nepotpuna stara pokrivenost | Nova izvršavanja trebaju vlastiti runtime identitet; nema dokaza numeričkog uzroka starih grešaka |
| H1 | Registriran; implementacija i kvalifikacija NOT_RUN | Nije automatski sljedeći korak |
| P4 | Nema odobrenog cjelovitog budžeta modela/analize ni fizičke kvalifikacije | Prvo zatvoriti budžete i odabrati izvediv smjer |

Na početku plana consumer `origin/main` je
`4a8c660183910b0950ae7ddabce9e8b395a2f10d`.
Aktualna imena su `ruzina-apta-adapter` i neutralni `TrackAnalysis`.
Analiza se prekida tijekom playbacka; prihvaćena analiza i generacije se štite.
APTA nije aktiviran na Guitionu, a M1R-P1-001 odgađa produkcijsko usvajanje do
upstream embedded/release uvjeta. Svjež pregled mora provjeriti jesu li se ti
ugovori promijenili; ova revizija nije trajni consumer pin.

## 3. Ciljevi i putanje

**Cilj A: native Rust analiza za M1.** Jedna podržana putanja od PCM-a do korisnog,
konzistentnog rezultata, s granicama memorije, trajanja i otkazivanja, zatim
provjera potrošača i uređaja. Eksperimentalni put i produkcijsko uključivanje
moraju ostati odvojeni dok M1R-P1-001 i odgovarajući upstream kriteriji nisu zatvoreni.

**Cilj B: potpuna zamjena C paketa.** Dodatni ABI/API/CLI/platform/package zahtjevi.
Ne dodavati ih kritičnom putu A bez izričitog zahtjeva. Završetak A nije završetak B.

Predloženi prioritet je standalone P4, jer je to postojeći smjer proizvoda.
To nije potvrda da Beat This! stane na P4. Desktop predanaliza je zasebna
arhitekturna alternativa i traži izričitu produktnu odluku; ne uključivati je prešutno.

Redoslijed:

`PF-00 -> PF-01 -> PF-02 -> PF-03 -> PF-04 -> PF-05 -> PF-06 -> PF-07 -> PF-08 -> PF-09 -> PF-10 -> PF-11`.

PF-12 je zaseban, uvjetovan završetak cilja B. Ugovor PF-02 i
inventura postojeće jezgre mogu napredovati dok se čeka odgovor iz PF-00/01;
ne pokretati ovisnu implementaciju ili mjerenja bez potrebnih odluka.

## 4. Datoteke i obrazac predaje

Navedene nove datoteke su PLANIRANE, ne dokaz da postoje.

| Datoteka | Vlasnik / svrha |
|---|---|
| `docs/rust/PRODUCT-FIRST-STATUS.json` | PF-00: jedino promjenjivo stanje faza i sljedeći korak |
| `docs/rust/PRODUCT-SCOPE.md` | PF-00: cilj A/B, način rada i odluke vlasnika |
| `docs/rust/PRODUCT-BUDGETS.json` | PF-01: vrijednosti, jedinice, izvori, status i vlasništvo memorije |
| `docs/rust/PRODUCT-GRID-CONTRACT.md` | PF-02: točno značenje korisničkog izlaza i budućeg mjerenja |
| `docs/rust/PRODUCT-FEASIBILITY.md` | PF-03: izvedivost odabranog puta, ne samo dekodera |
| `docs/rust/PRODUCT-EXPERIMENT.md/.json` | PF-04: jedan novi pokus, sve odluke prije mjerenja |
| `docs/rust/PRODUCT-PREFLIGHT.md/.json` | PF-05: operativni i sintetički dokazi |
| `docs/rust/PRODUCT-DEVELOPMENT-RESULT.md/.json` | PF-06: potpuni rezultat jednog pokusa |
| `docs/rust/PRODUCT-TRANSFER.md/.json` | PF-07: odvojena neovisnost, transfer i confidence |
| `docs/rust/PRODUCT-NATIVE-API.md` | PF-08: prihvaćeni algoritam i native ugovor prije koda |
| `docs/rust/PRODUCT-INTEGRATION.md` | PF-09: exact consumer pin, ownership, lifecycle, testovi |
| `docs/rust/PRODUCT-P4-RESULT.md/.json` | PF-10: exact image, uređaj, resursi i fizički rezultat |
| `docs/rust/PRODUCT-RELEASE-CHECKLIST.md` | PF-11: stvarno zatvoreni i otvoreni uvjeti |
| `docs/rust/COMPATIBILITY-CLOSURE.md` | PF-12: zahtijevani puni C/CLI/platform/package ostatak |

Ne duplicirati velike stare izvještaje: poveži izvor i zapiši samo novu odluku.
Jedna faza normalno završava jednim izvještajem i jednim checkpointom; ne uvoditi
nove prefikse faza za svaki parser, hash, pregled ili popravak javnog testa.
Registracija i stvarno mjerenje ipak moraju imati odvojene commit granice.

Obvezna predaja faze:

```text
phase: PF-xx
source_sha: puni izmjereni commit ili baza plana, jasno označeno
inputs: javne putanje/hashovi; privatni sadržaj izvan Gita
change: što se promijenilo i koju odluku zatvara
checks: točne izvršene naredbe i broj pass/fail/skip, bez starih testova kao novih
resources: relevantan opseg, izmjereno/izvedeno/nepoznato
verdict: PASS / REJECTED / INVALID / INCOMPLETE / NEEDS_DECISION
open: preostale stavke i njihov vlasnik
next: jedan dopušten korak i njegovi preduvjeti
publication: commit i provjeren remote SHA
```

## 5. PF-00 — zaključaj prvi isporučivi opseg

**Ulazi:** ovaj plan, capability checklist u `MIGRATION.md`, aktualni consumer
requirements/architecture i adapter README. Nema audio/model sadržaja.

**Postupak:**
1. Razdvoji već implementirano, neprovjereno i nedostajuće; ne računaj dokumente
   ili sintetičke PASS-ove kao završene produktne funkcije.
2. U `PRODUCT-SCOPE.md` zapiši način rada: standalone uređaj ili desktop priprema;
   kada analiza smije raditi; prihvaća li se unaprijed pripremljena analiza;
   tražene izlaze; podržane formate/sample rateove, trajanje i promjene tempa/metra.
3. Označi što je pronađen postojeći zahtjev, što je preporuka, a što odluka korisnika.
   Zatraži samo nedostajuće odluke. Ne postavljaj ponovno već odgovorena pitanja.
4. Posebno odluči jesu li key i confidence obvezni za prvi cilj ili se privremeno
   izostavljaju s eksplicitnim unavailable stanjem. Izostavljanje ne zadovoljava
   puni APTA DJ prihvat i ne smije se prikazati kao takvo.
5. Napravi `PRODUCT-FIRST-STATUS.json` s fazama, preduvjetima i otvorenim odlukama.

**Provjere:** svaki zahtjev ima izvor/vlasnika i testabilan kriterij; nema
prešutnog offloada, provider aktivacije ili izmišljenog potpunog C pariteta.
**Done:** jedan nedvosmislen prvi cilj. **STOP:** neriješena arhitektura označena
NEEDS_DECISION; smije se nastaviti samo neovisna inventura.

## 6. PF-01 — budžeti prije velikog porta

**Ulazi:** PF-00, `DENSE-DECODER-P4-CONSTRAINTS.md`, noviji
`METER-CONTEXT-P4-BUDGET-20261025.md`, aktualni consumer memory/board profile.

**Postupak:**
1. Čitaj izvore na punom consumer SHA. Potvrdi stvarni uređaj/reviziju i razrede
   memorije. Dokumentiranih 32 MiB PSRAM ili 64 KiB probe nisu slobodna alokacija.
2. Popiši audio/USB/UI/decoder/broker/stog/futures/channel rezerve, oba izlazna
   spremnika, prihvaćene/pending kopije te moguća preklapanja njihovih životnih vijekova.
3. Odredi vlasnika i numerički limit za internal RAM, PSRAM, model storage,
   stog, prvi/upotrebljivi/final rezultat, vrijeme po minuti audija i cancel latenciju.
4. Za nepoznate brojke najprije izračunaj dostupnu statičku evidenciju. Pripremi
   ciljano mjerenje postojećeg uređaja ako je dostupno; ne tretiraj build/boot kao
   mjerenje slobodne memorije pod opterećenjem. Ako uređaj/identitet nije poznat,
   pitaj za njega prije flashanja. Ne treba novo odobrenje za već autorizirano mjerenje.
5. Korisnik određuje prihvatljivo čekanje i produktni opseg; inženjerski izmjeren
   ostatak memorije postavlja gornju granicu, ne proizvoljni broj modela.

Zapis svakog budžeta:
`name, value|null, unit, status, source_revision, evidence, owner, lifetime,
storage_class, safety_margin_and_rationale`. UNKNOWN mora imati `value=null`.
Izvedeni peak je maksimum zbroja istodobno živih alokacija; navedi nepoznat overhead.
Ne oduzimaj rezervu dvaput i ne zamijeni PSRAM s internal/DMA memorijom.

**Done:** odobreni numerički limiti za odabrani način rada ili točno opisan
NEEDS_MEASUREMENT/NEEDS_DECISION. **STOP:** nepoznat obvezni limit blokira veliki
P4 port i fizički PASS. Dopušteni su dokumentirani statički izračuni, ne aktivacija.

## 7. PF-02 — ugovor grida, metra i evaluacije

**Ulazi:** `specification/beatgrid.md`, `specification/APTA-1.1-DJ-SECTIONS.md`,
`rust/src/grid.rs`, `native_validation.rs`, `builder.rs`, consumer adapter,
P0/P1/P2, `METER-CONTEXT-MC04-CONTRACT-REVIEW-20261025.md`, MC-03 i MC-08.

**Postupak:**
1. Napiši jedan worked example za kontekstni anchor prije prozora: globalni
   source frame, Q32 period, prvi owned beat, signed ordinal, meter/downbeat.
2. Definiraj [first,end), exclusive endpoint, diskontinuitet/gap, UNKNOWN kontekst,
   promjenu tempa/metra, unsupported/capacity status i full-source kontinuitet.
3. Razdvoji točnost detektora, geometriju reprezentacije i dostupnost reference.
   Za svaku buduću metriku navedi numerator, denominator, toleranciju, paired
   usporedbu i tretman UNKNOWN. Ne uzimaj conditional 583/583 kao 600/600.
4. Odluči kakav se downbeat zapis objavljuje korisniku. MC-08 objašnjava 25
   gubitaka podrške stare pojedinačne točke; ne dokazuje da su svi gridovi jednaki.
5. Sačuvaj postojeće wire/consumer kapacitete. Novi format/kapacitet zahtijeva
   zasebno obrazloženu odluku s consumerom; ne podiži limit da primjer prođe.
6. Definiraj javne primjere: 3/4, 4/4, anchor prije prozora, rubne točke,
   nedostatak referentnog raspona, promjena tempa, gap, overflow i puni izvor.

**Provjere:** svaki primjer ima ručno izveden očekivani izlaz i status; relevantni
postojeći native testovi provjeravaju geometriju. Nema novih real-data scoreova.
**Done:** budući konstruktor/evaluator mogu biti implementirani bez tumačenja
namjere. Stari evaluatori, kriteriji i verdicti nisu promijenjeni.
**STOP:** ako ugovor traži novu wire/product odluku, dokumentiraj je prije koda.

## 8. PF-03 — provjeri cijeli put i odaberi rutu

**Ulazi:** PF-00/01/02, postojeći model/decoder izvori i javni agregatni rezultati.
Ne otvarati oznake, glazbu ili pokretati model radi ovog statičkog pregleda.

**Postupak:**
1. U jednu tablicu stavi weights/storage, frontend/FFT, tensors/activations,
   kontekst, decoder scratch/traceback, rezultate, stog, cancellation i full-track
   ponašanje. Za svaku stavku odvoji DERIVED, MEASURED i UNKNOWN.
2. Veličina checkpointa nije peak RAM. Stari Python RSS uključuje harness i oba
   modela; ne koristi ga kao isolated P4 zahtjev niti kao dokaz nemogućnosti.
3. Provjeri postojeće prihvatljive backendove i podržane operacije iz točnih
   izvora. Ako treba nov vanjski podatak, koristi primarni izvor i zapiši reviziju.
   Ne instaliraj backend ili odaberi novi model u sklopu ove inventure.
4. Usporedi s PF-01 limitima i PF-02 geometrijom. Statičko uklapanje daje samo
   STATIC_CANDIDATE, ne fizički PASS. Navedi koje jedno mjerenje rješava nepoznanicu.
5. Izaberi jednu rutu: postojeći kandidat vrijedi ograničenog prototipa;
   potreban je zaseban kompaktan model; ili je prihvatljiva odobrena desktop ruta.

**Done:** obrazložena ruta i njen točan sljedeći pokus. **STOP:** ako nijedna ruta
ne zadovoljava cilj, predaj odluku vlasniku umjesto gradnje runtime platforme.
Ne oživljavati BeatNet/DD-05, ne zaključivati quantization/training plan bez
posebne odluke. Ne skrivati potrebu za novim algoritmom iza izraza „samo port”.

## 9. PF-04 — jedna nova hipoteza i najmanja potrebna infrastruktura

**Ulazi:** zatvorene PF-00/01/02 odluke i PF-03 ruta.

**Postupak:**
1. Napiši `PRODUCT-EXPERIMENT.md/.json`: konkretna pogreška, jedna mijenjana os,
   točan baseline, model/algorithm revision, svi parametri, dopušteni ulazi,
   izlazi, metrike, resursi, no-regression veto i stop/retry pravilo.
2. Ako algoritam još nije određen, zatraži stručnu specifikaciju. Predaj paket:
   cilj/budžeti/ugovor, postojeći porazi, predložena jedna hipoteza, API i testovi
   koje treba definirati. Slabiji model ne smije izabrati DP troškove, thresholdove,
   mrežnu arhitekturu ili kvantizaciju na temelju već viđenih rezultata.
3. Odredi treba li uopće nova neuralna inferencija. Ako pokus koristi spremljene
   logite, registriraj njihovu izloženost i identitete; H1 model runtime nije nužan
   samo radi pure dekodera. Ni taj put ne dopušta novu real evaluaciju bez scopea.
4. Ako je neuralni runtime potreban, odaberi IZRIČITO: puni postojeći H1, ili novi
   uži protokol za konkretan pokus. Uži protokol ima vlastiti ID, opseg i ograničenja;
   ne tvrdi da je prošao H1. Sačuvaj H0 nalaz i originalne H1 dokumente.
5. Ponovno koristi postojeći binding/supervision i runtime kod gdje odgovara.
   Jedna reproducibilna konfiguracija, točne težine/programi, izolirani ulazi,
   nepromjenjivost tijekom rada i relevantne sintetičke provjere su obvezni.
   Ne graditi opći package manager, univerzalni sandbox ili novi evaluator framework.
6. Registriraj točne funkcije/formule/tie-breakove/API, mali oracle i očekivane
   negativne slučajeve prije implementacije. Scope mora razlikovati profiliranje
   resursa, razvojnu glazbenu evaluaciju i budući transfer.

**Done:** drugi model može implementirati bez znanstvenog nagađanja; commit/push
registracije prije koda/mjerenja. **STOP:** nedostajuća algoritamska specifikacija,
nedozvoljen skup ili nepoznat nužan budget ne popunjava se proizvoljno.

## 10. PF-05 — implementacija i relevantni preflight

**Promjene:** samo novi opt-in eksperimentalni modul i njegovi testovi/runner,
imenovani u PF-04. Postojeće čiste numeričke i wire funkcije ponovno koristiti.
Ne mijenjati frozen evaluatore ni default DSP da bi novi test prošao.

**Postupak:**
1. Implementiraj čisti konstruktor bez file I/O, oznaka, source identiteta i starih
   scoreova. Dodaj neovisni mali oracle, determinističke tie slučajeve, granice i UNKNOWN.
2. Runner odvaja konstrukciju od evaluacije; kandidati i hashovi zamrzavaju se prije
   referenci. Testiraj neovisnost izlaza o prisutnosti/promjeni oznaka.
3. Testiraj stvarne rizike: malformed/NaN/Inf, hash drift, pogrešan broj izlaza,
   postojeći output, timeout/cancel, prekoračenje memorije i djelomično izvršenje.
4. Ako je neuralni put odabran, poseban synthetic-only entrypoint. Ne pozivati
   stari dense `run(...preflight_only=True)`, jer `bind_inputs` otvara stvarne ulaze.
5. Izmjeri full-size generirani slučaj i troškove odabranih faza. Host resursi
   ostaju host; za P4 usporedbu koristi samo kompatibilne veličine/izračune.
6. Popravi javne testove prije freezea. Potom commit/push implementacije i točnih
   preflight dokaza. Stvarni PF-06 run zahtijeva tu čistu reviziju i scope.

**Done:** svi relevantni javni/operativni testovi prolaze, bez prikrivenih skipova;
registrirani resursi zadovoljeni. **STOP:** fail ostaje preflight fail, ne glazbeni
poraz/pobjeda. Ako se promijeni znanstvena specifikacija, nova registracija prije mjerenja.

## 11. PF-06 — jedan razvojni pokus s odlukom

**Preduvjeti:** PF-04 scope, PF-05 exact-source preflight, svi nužni hashovi,
valjano dopuštenje za određeni razvojni skup. Sama riječ „nastavi” ne briše
ograničenja formalnih holdouta ili ponavlja potrošeni pokušaj.

1. Izvrši sve registrirane izvore/redoslijed; bez podskupova nakon greške.
2. Prije oznaka zamrzni rezultate konstrukcije; zatim izvrši neizmijenjeni
   registrirani evaluator i neovisne provjere potrebne za konkretan rizik.
3. Objavi denominatore, UNKNOWN, paired fixes/breaks, event/grid/meter i confidence
   odvojeno te wall/RSS/storage točnog opsega. Bez privatnih ID-eva/koordinata u Gitu.
4. PASS znači DEVELOPMENT_PASS_ONLY. REJECTED za valjan pad kriterija;
   INVALID/INCOMPLETE za runtime/integrity/resource/cohort problem.

**Done:** jedna jasna odluka. **STOP:** odbijen kandidat ne ide u transfer/port.
Bez sweepa i automatskog ponavljanja. Daljnji rad traži konkretnu novu hipotezu
ili promjenu produktnog opsega; dodatni audit nije zadani odgovor na svaki poraz.

## 12. PF-07 — neovisan transfer i confidence

**Preduvjet:** PF-06 prolaz. Razvojni GTZAN i izloženi skupovi nisu novi prihvat.

1. Dokumentiraj trening/tuning/selection izloženost, recording/performance identitet,
   duplicate/transcode provjere i prava pristupa. Novo ime dataseta nije neovisnost.
2. Registriraj transfer skup i kriterije prije otvaranja novih rezultata. Zamrzni
   kandidata; prenesi bez tuninga na transfer oznakama.
3. Ako proizvod zahtijeva confidence, definiraj odvojenu kalibraciju i validaciju;
   null/0 ne dokazuje safety. Ako confidence nije dostupan, izlaz to mora jasno nositi.
4. Za obvezni key zatvori zaseban algoritamski/transfer dokaz; beatgrid rezultat
   ne zatvara MKEY. Ako key nije u prvom opsegu, navedi granicu i preostali puni prihvat.

**Done:** neovisnost i relevantni transfer/safety kriteriji potvrđeni zasebno.
**STOP:** UNRESOLVED ostaje otvoreno i blokira promociju, bez preimenovanja starog
skupa u „svjež”. Ne trošiti završni prihvat za razvoj.

## 13. PF-08 — uski native Rust port i zatvaranje relevantne jezgre

**Preduvjeti:** PF-06/07 prolaz, PF-01 budžeti, PF-03 izvediva ruta, PF-02 ugovor.
Native port mora obuhvatiti potrebni inference/frontend put za standalone cilj;
port dekodera sam nije end-to-end analiza.
Ako je izričito odabrana desktop ruta, PF-08 na uređaju obuhvaća native import,
validaciju i rezultat, a inference ostaje u zasebno kvalificiranom host sloju.
Takav rezultat ne označavati kao standalone neuralnu P4 implementaciju.

1. Prvo `PRODUCT-NATIVE-API.md`: točni tipovi i stanja, storage plan, lifetime,
   numeričke tolerancije, izlazni kapaciteti i bounded work/cancel ugovor.
2. Planirana modularna lokacija: `rust/src/product_analysis/mod.rs` i samo potrebni
   `decoder.rs`, `features.rs`, `inference.rs`. Gated export u `lib.rs`, opt-in
   `experimental-product-analysis` u `Cargo.toml`. Ne duplicirati postojeći grid,
   session, builder, key ili serialization sloj.
3. API mora specificirati `required_storage`, `begin`, `step`, `cancel`, `finish`;
   stvarni potpisi slijede prihvaćeni algoritam. Koraci su omeđeni brojem
   operacija/prijelaza, ne cijelom pjesmom. BufferTooSmall/overflow su eksplicitni.
4. Caller-owned memorija; core bez alloc/unsafe. Platformski backend ide u
   odgovarajući zaseban sloj s dokumentiranim granicama, ne skriveni heap u coreu.
5. Parity testovi protiv zamrznute prihvaćene reference: vrijeme/ordinals/status,
   granice i ties, chunk/budget 1/7/256/large, cancellation, ponovno korištenje
   storagea, lifetime izlaza, maksimumi i nedostatni kapaciteti.
6. Popiši otvorene core probleme koji utječu na cilj A: portable key rounding,
   capability/failure matrix i relevantne platforme. Za svaki prvo regresija,
   zatim minimalni popravak. Ne prepisuj golden očekivanje da sakriješ razliku.
7. Ne uvoditi C ABI ovdje. Ne mijenjati default aktivaciju ili consumer pin.

**Naredbe, prilagoditi odabranim testovima nakon stvaranja modula:**

```text
cargo fmt --all -- --check
cargo test -p libapta --locked --features experimental-product-analysis --test product_analysis
cargo test -p libapta --locked --release --features experimental-product-analysis --test product_analysis
cargo clippy -p libapta --locked --lib --test product_analysis --features experimental-product-analysis -- -D warnings
cargo check -p libapta --locked --lib --no-default-features
cargo check -p libapta --locked --lib --no-default-features --features experimental-product-analysis
```

Planirani test: `rust/tests/product_analysis.rs`. Dodati relevantne postojeće
grid/builder/native-validation kontrole i C oracle samo gdje se mijenja njihov ugovor.
Potrebni target compile/link nije fizički test. **Done:** native parity i bounded
storage/work dokazi za prihvaćeni put. **STOP:** neprihvatljiva numerika ili memorija.

## 14. PF-09 — stvarni consumer i puni tok

**Ulazi:** PF-08, aktualni consumer točni izvori i njegov test/build postupak.

1. Pripremi izoliranu eksperimentalnu integraciju u aktualnom `ruzina-apta-adapter`;
   prije izmjena pregledaj njegove upute i tuđi lokalni rad. Pin mijenjati samo
   na dokazanu exact reviziju u tom checkpointu, uz dokumentiranu odluku integracije.
2. Provedi PCM -> native analiza -> nepromjenjivi rezultat -> wire/read -> adapter
   -> neutralni TrackAnalysis; potrošač mora prihvatiti stvarni izlaz, ne imitaciju.
3. Provjeri full-source continuity, ordinals/meter, gap i unsupported put, oba
   spremnika, retained/pending generacije i pritisak na kapacitet.
4. Testiraj cancel/playback start, novu pjesmu/media generation, stale completion,
   zauzete banke, recycle/retry, I/O fail, source loss i ponovni početak.
5. Za desktop rutu dodatno registrirati cache/source identity, import/invalidaciju
   i ponašanje kad pripremljena analiza nedostaje; nema skrivenog neuralnog fallbacka.

**Done:** jedan host end-to-end tok i lifecycle testovi prolaze na točnim pinovima.
**STOP:** nekompatibilan consumer ugovor ili prekoračen bank budget. Produkcijski
provider ostaje odvojen od eksperimentalnih testova do upstream/product gateova.

## 15. PF-10 — ciljni uređaj i stvarni resursi

1. Najprije target compile/link, linker/storage provjera i točan firmware hash.
2. Potvrdi fizički uređaj/reviziju, postupak backup/recovery i siguran testni način
   prema consumer runbooku. Ne pogađaj serijski port niti koristi tuđu ploču.
3. Izmjeri sve PF-01 budžete na točnoj slici: peak internal/PSRAM, stog, storage,
   prvi/final rezultat, sekunde/minuti audija, cancel deadline i cijelu životnu putanju.
4. Koristi registrirani raspon duljina, maksimum, različite sample rateove/formate,
   promjenjivi tempo/metar ako su podržani te gap/corrupt/source-loss slučajeve.
5. Testiraj prijelaz u playback i audio/USB/UI učinak uz postojeću cancel politiku;
   ne aktiviraj paralelnu analizu tijekom playbacka bez promjene produktnog ugovora.
6. Bilježi operator/listening opažanja odvojeno od telemetrije. Boot, build,
   host-time ili waiver ne označavaju neizvršeni fizički test kao PASS.

Za odobrenu desktop rutu ovdje se na P4 mjere import, cache, consumer i playback
put; troškovi predanalize mjere se na odobrenom hostu. Oba su potrebna za taj
produktni opseg, a nijedan nije dokaz model inferencea na P4.

**Done:** svaki obvezni limit ima izmjeren rezultat i prolaz. **STOP:** bilo koji
pad vraća na konkretni dizajn/budžet; ne podizati limit nakon mjerenja bez odluke.
Ako nema uređaja, rezultat HARDWARE_PENDING; ne izmišljati završetak.

## 16. PF-11 — prihvat i izdanje

1. Mapiraj scope A i puni APTA zahtjev na stvarno zatvorene gateove. Za M1
   produkcijsku aktivaciju poštuj aktualni M1R-P1-001; ne proglasiti eksperiment izdanjem.
2. Puni DJ prihvat ide isključivo kroz
   `docs/status/APTA-1.1-QUALIFICATION-RUNBOOK.md` i
   `APTA-1.1-DJ-ACCEPTANCE-PROTOCOL.md`. Rust analyzer izlaz prvo mora biti
   kompatibilan s tim putem i verifikatorom; ne zamijeniti ga synthetic izvještajem.
3. Zamrznuti zahtjevi su >=48 svježih neovisno potvrđenih pjesama, key>=75%,
   meter>=95%, downbeat/grid>=90%, <=5% high-confidence grešaka po obitelji,
   period<=1%, cyclic phase<=0.10 beat. Izvorni evaluator je autoritet za detalje.
4. Nijedan izloženi skup ili formalni poraz ne preimenovati u svjež prihvat.
5. Izvrši potrebnu konačnu native/platform/conformance matricu, pakiranje i
   provenance/SBOM provjere. Tek tada zasebna odluka o aktivaciji/verziji/tagu.

**Done:** samo stvarno ostvareni opseg izdanja. **STOP:** otvoreni key, confidence,
transfer, fizički ili formalni gate sprječava odgovarajuću tvrdnju o prihvatu.
VERSION ostaje 1.0.1 dok zasebni release postupak ne odobri promjenu.

## 17. PF-12 — opcionalno dovršenje pune C zamjene

Pokrenuti samo ako PF-00 izričito uključuje cilj B ili ga korisnik naknadno zatraži.
Iz postojećeg capability checklista izvuci točne rupe; ne implementiraj sve ponovno.

Redoslijed: referentni contract tests -> mali FFI sloj -> allocator/context/callback
ownership -> acquire/release i thread/lifetime ugovori -> CLI parity ->
static/shared/CMake/pkg-config -> platform/conformance/security matrica.
C ABI sloj mora biti odvojen od `forbid(unsafe_code)` portable jezgre.
Provjeri struct layout/status/version, C source callback release, stare binarne
potrošače, malformed ulaze i stvarni link/run na obećanim platformama.
MSVC compile bez SDK link/run nije Windows PASS; ILP32/core compile nije device PASS.

**Done:** svaki obećani C/API/ABI/package/platform ugovor ima stvarni dokaz.
**STOP:** ne objavljivati djelomični Rust FFI pod postojećim punim C ABI obećanjem.

## 18. Kontrola opsega i završni kriterij

- Nijedna faza ne zatvara UNKNOWN pretpostavkom. Odluka o nepoznanici mora imati
  vlasnika, konkretan podatak koji treba prikupiti i utjecaj na sljedeći korak.
- Ne ponavljati cijeli C/Rust matrix za dokument ili host-only alat. Za core
  promjenu pokrenuti relevantne regresije; za konačni rezultat potrebnu punu matricu.
- Za svaku dodatnu infrastrukturnu promjenu navesti koji registrirani pokus ili
  produktni zahtjev bez nje ne može proći. Bez takvog razloga staviti je u backlog.
- Nema procenta dovršenosti na temelju testova/commitova. Napredak se mjeri zatvorenim
  produktnim odlukama, algoritamskim dokazima i izvršenim end-to-end putem.
- Plan nije algoritamska specifikacija budućeg neodabranog modela. Točno mjesto
  stručne odluke je PF-03/04; slabiji model tamo staje s konkretnim paketom pitanja.

### Prompt za sljedeći nastavak

> Pročitaj PRODUCT-FIRST-IMPLEMENTATION-PLAN-20261026.md i njegov JSON te aktualni
> vrh MIGRATION.md. Izvrši PF-00: inventuru i odluku o prvom isporučivom opsegu.
> Koristi postojeći kod i aktualne consumer zahtjeve prije pitanja korisniku.
> Spremi PRODUCT-SCOPE.md i PRODUCT-FIRST-STATUS.json. Ne implementiraj H1, novi
> algoritam niti pokreći model/korpus. Jasno označi odluke koje nedostaju; provjeri
> dokumente i selektivno commitaj/pushaj s potvrdom punog remote SHA.
