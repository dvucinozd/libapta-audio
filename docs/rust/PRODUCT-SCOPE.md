# PF-00: prvi isporučivi opseg native Rust analize

Datum: 2026-10-26. **PF-00 SCOPE COMPLETE**; produktno izvršavanje **NOT_READY**.
Baza pregleda: `0f19ec768ffcdcd30e579ef1265fb1809ad807c4`, `rust-rewrite`.
[Plan](PRODUCT-FIRST-IMPLEMENTATION-PLAN-20261026.md),
[aktualno stanje faza](PRODUCT-FIRST-STATUS.json).
Ovaj checkpoint zaključuje opseg i inventuru; ne implementira novi DSP ili provider.

## Prvi cilj i odluke

Prvi cilj je native Rust analiza na M1/ESP32-P4, prije reprodukcije, od dekodiranog
PCM-a do nepromjenjivog neutralnog `TrackAnalysis` rezultata. Desktop služi za
razvoj, referencu i testove; obvezna desktop predanaliza nije dio ove rute.
To slijedi prihvaćeni product-first smjer i postojeću M1 arhitekturu. C API/ABI,
CLI i paketna zamjena ostaju zaseban cilj B, izvan prvog isporučivog opsega.

| Odluka / zahtjev | Vrijednost | Autoritet |
|---|---|---|
| Obvezni izlazi | waveform, BPM, beatgrid i metar/downbeat faza | izričit odgovor korisnika 2026-10-26 |
| Key i kalibrirani confidence | odgođeni za kasniji korak | izričit odgovor korisnika 2026-10-26 |
| Najdulje trajanje izvora | 600 sekundi / 10 minuta | izričit odabir korisnika 2026-10-26 |
| Način rada | standalone M1/P4, stopped-deck analiza | prihvaćeni smjer i postojeći consumer ugovor |
| Playback | prekinuti analizu pri početku reprodukcije | consumer architecture |
| Postojeća prihvaćena analiza | zadržati, ne zamijeniti native rezultatom | consumer architecture/adapter |
| Predanalizirani podaci | postojeći Rekordbox/cache put ostaje; ne traži se nova desktop priprema | postojeći provider ugovor |
| Produkcijska aktivacija APTA | ovisi o upstream embedded/release gateovima | M1R-P1-001 |

Odabir 10 minuta je produktni limit, a ne izmjereno vrijeme analize ili dokaz
točnosti/memorijske izvedivosti. PF-01 tek određuje te numeričke budžete.

## Ulazi i ponašanje na granicama

- Produktni izvor: MP3, WAV ili FLAC, prema M1R-P0-011. LIBAPTA core prima PCM;
  dekoder, filesystem/media lease i bounded read pripadaju consumeru.
- Početni engineering profil kvalifikacije: mono/stereo, 44100 i 48000 Hz,
  s očuvanim source-frame koordinatama i stvarnim dekodiranim trajanjem.
  To je početni izbor testne matrice, ne tvrdnja o svim sample rateovima koje
  generički WAV parser ili playback mogu prihvatiti.
- Prvi implementacijski vertikalni test koristi postojeći WAV/PCM put. MP3/FLAC
  decoded-PCM bridge ostaje obvezan prije tvrdnje o cijelom navedenom produktnom
  ulaznom opsegu; trenutačni APTA worker ga još ne zatvara.
- Prazan, korumpiran, nepodržan ili predug izvor ima eksplicitan status bez
  objave lažnog potpunog rezultata. Pjesma dulja od 600 s ne skraćuje se na prvih
  600 s i takav se prefiks ne predstavlja kao analiza cijele pjesme.
- Za poznat ili naknadno utvrđen broj frameova `N`, trajanje se provjerava točno
  uvjetom `N <= 600 * source_sample_rate` uz checked arithmetic. Izvor nepoznate
  duljine provjerava se i tijekom rada; catalog length nije autoritet za EOF.
- Prvi grid/meter profil koristi postojeći raspon 40..300 BPM i jedan full-source
  metar 3/4 ili 4/4. Ovo je engineering suženje kompatibilno s prvom APTA ciljnom
  metrikom; generički adapter podržava širi brojnik, ali owned bridge dopušta faze 0..3.
- Promjenjivi tempo dopušten je samo ako prihvaćeni algoritam i postojeći model
  mogu opisati kontinuiran puni izvor unutar kapaciteta. Nema automatskog OLS
  izravnavanja promjena tempa radi prolaska adaptera.
- Promjene metra, sparse/hybrid grid, poznati gap ili izvan-opsežni metar nisu
  potpun podržan beatgrid ovog prvog profila. Nema umetanja lažnog 4/4 ili tihog
  prebacivanja s nevaljanog globalnog na lokalni grid.
- Nedostatak potpune grid/meter evidencije ostaje unavailable/unsupported ili
  postojeći jasno djelomični waveform/tempo-only rezultat; takav rezultat nije
  uspješno ispunjenje obveznog beatgrid/meter izlaza.

Drugim riječima, trajanje do 10 minuta jest cilj potpune putanje; podržana glazbena
točnost i svi formati moraju se još dokazati, a ne zaključiti iz admission pravila.

## Kapaciteti i razlog odabira trajanja

Postojeći `rust/src/grid.rs` i `global_analysis.rs` ograničavaju eksplicitne beatove
na 3072, a segmente na 8. `tempo.rs` i segment validator koriste 40..300 BPM.
Za idealni četvrtinski raster na gornjoj granici:

`600 s * 300 beats/min / 60 s/min = 3000 beats`.

To je izvedeni planski račun, ne mjerenje detektora. Ostavlja 72 zapisa razlike
prema postojećem eksplicitnom kapacitetu; ne daje pravo dodavanja proizvoljnih
dodatnih događaja. Brojnik metra broji faze, ne dodatne beatove između četvrtinki.
Q32 rounding, owned range i više segmenata zahtijevaju točne PF-02 granične testove;
proizvoljni eksplicitni događaji nisu automatski omeđeni nominalnim BPM-om.
Stvarni broj događaja/segmenata mora se provjeriti i overflow eksplicitno prijaviti.

Pri 48000 Hz maksimum iznosi 28800000 source frameova; pri 44100 Hz 26460000.
Nijedan broj ne određuje storage cijelog PCM-a: bounded streaming i peak lifetimes
moraju se planirati u PF-01/03. Limit se ne podiže niti se izlaz truncira radi testa.

## Što prvi rezultat mora omogućiti

1. Cijeli podržani izvor daje waveform i BPM; podržani potpuni grid ima metar i
   downbeat fazu na istim source/ordinal koordinatama, za neutralne Sync/Beat Jump
   i prikaz potrošače. Detaljni representation ugovor određuje PF-02.
2. Izlaz je nepromjenjiv, povezan sa source/media/load generacijom i životnim
   vijekom spremnika. Stale completion ne može zamijeniti novi load.
3. Obrada i pretvorba rade izvan audio deadline callbacka. Dvije banke,
   pending/retained kopije, recycle i cancellation ulaze u budžet, ne samo DSP scratch.
4. Početak playbacka otkazuje analizu; cancel/recovery i ponovni početak moraju
   proći lifecycle testove prije produktne aktivacije.
5. Key i kalibrirani confidence nisu obvezni izlazi ovog profila. Njihova odsutnost
   je izričita; interna vrijednost 0/null ili wire polje nije confidence-safety PASS.
   Ne mijenja se današnje default Rust/C ponašanje da bi se dokument uskladio.
6. Reprezentacija i native parity, glazbena kvaliteta, consumer end-to-end te
   fizički budžeti dobivaju zasebne dokaze. Užim opsegom se ne zatvara puni
   APTA 1.1 DJ prihvat koji uključuje key i confidence.

## Inventura početka: kod nasuprot kvalifikaciji

| Područje | Implementirano ili pronađeno u kodu | Što ostaje otvoreno |
|---|---|---|
| Core PCM i waveform | caller-owned sequential/pull, WAV parser, waveform i detail | odabrana produktna matrica i uređaj |
| Rezultati i wire | native graph/builder, reader/writer, Q32 grid i meter validacija | prihvaćeni puni grid novog algoritma |
| Ownership/runtime | nepromjenjive generacije, storage planning, native/std owners | ukupni produktni peak, target placement i latency |
| Default glazbeni DSP | tempo/local/global grid, key/meter i quality plumbing | prihvatljiva točnost i relevantne portable numeričke granice |
| Eksperimentalni algoritmi | host konstrukcija i brojni generated testovi postoje | DD-05 je odbijen; nema prihvaćenog novog Rust dense dekodera |
| Consumer adapter | convert/cache/waveform, full-source provjere i lifecycle owners | firmware task/bank/provider wiring nije aktiviran; compressed analysis bridge otvoren |
| Key/confidence consumer | TrackAnalysis nema novu key/confidence putanju; cache adapter postavlja key=None i quality=[] | kasniji produktni i safety korak |
| Platforma | portable core je no_std/unsafe-free; postoje compile i host dokazi | točan ciljni backend, cijeli inference put i fizički M1/P4 dokaz |

Ovo je pregled izvornog koda i postojećih izvještaja. Nisu ponovno pokrenuti stari
host/C/Rust testovi, corpus evaluacija ili inference, pa se njihovi stari rezultati
ne predstavljaju kao testovi ovog checkpointa.

## Točni izvori i otvorene odluke

Consumer je pregledan na fetched `origin/main`
`4a8c660183910b0950ae7ddabce9e8b395a2f10d` putem `git show <sha>:<path>`:

- `docs/M1_RUST_PRODUCT_REQUIREMENTS.md`: M1R-P0-011/012, 050..053, 070 i P1-001;
- `docs/M1_RUST_ARCHITECTURE.md`: Media and analysis;
- `firmware-rust/crates/ruzina-apta-adapter/README.md` i `src/lib.rs`;
- `firmware-rust/crates/ruzina-track-analysis/src/lib.rs`.

Lokalni consumer checkout, njegova postojeća dva untracked unosa, pin i firmware
nisu mijenjani. Hashovi izabranih izvora i plana su u statusnom JSON-u.

PF-00 nema preostalu blokirajuću odluku o prvom opsegu. **Sljedeći je PF-01**:
internal RAM/PSRAM/storage/stog/lifetimes, dopušteno prvo/final čekanje, vrijeme
po minuti audija i cancel deadline. Te brojke ostaju UNKNOWN; 10 minuta je
trajanje ulaza, a ne dopušteno čekanje korisnika. Točan uređaj/revizija i mjerenja
također ostaju otvoreni. PF-02 može napredovati neovisno nakon ovog opsega.

H1 ostaje odgođen do PF-04; nema nove registracije algoritma, runtime pripreme,
real replaya, consumer aktivacije, promjene VERSION 1.0.1 ili release tvrdnje.
