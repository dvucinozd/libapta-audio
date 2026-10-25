# PF-01: produktni ciljevi i statička inventura troškova

Datum: 2026-10-26. **STATIC_INVENTORY_COMPLETE; NEEDS_MEASUREMENT.**
Produktni memory/placement i fizički prolaz ostaju otvoreni; real execution NOT_READY.
Baza izvora `9ae4927d55a476d1f590e42fd486494ed13660aa`, `rust-rewrite`, uz
točno hash-bound novi [dijagnostički primjer](../../rust/examples/product_budget_probe.rs).
[Strojni budžeti i dokazi](PRODUCT-BUDGETS.json), [opseg](PRODUCT-SCOPE.md).

## Potvrđene odluke i uređaj

Korisnik 2026-10-26 odabire ove ciljeve za izvor do 600 s:

| Stavka | Cilj | Što znači |
|---|---:|---|
| Prvi korisni waveform/BPM | <=5000 ms | od prihvaćanja posla, uključujući čekanje, I/O i inicijalizaciju unutar prihvaćenog posla |
| Konačni puni grid/metar | <=60000 ms | uključuje potrebne source passove, provjeru identiteta i pripremu rezultata |
| Cancel | <=50 ms | od prihvaćenog cancel/playback-start zahtjeva do prestanka analitičkog rada i onemogućene objave otkazanog posla |
| Efektivni trošak na maksimalnom izvoru | <=6 s/min audija | izvedeno iz 60 s / 10 min, s uključenim setupom; nije dodatni proporcionalni rok za kratke izvore |

To su APPROVED_TARGET vrijednosti, ne izmjereni rezultati. Djelomični prvi rezultat
nije authoritative full-track grid. Unaprijed odbijen/busy posao ne smije se
prikazati kao prihvaćen pa izuzeti njegovo čekanje iz obećanog roka.
Cleanup i povrat banke/leaseova moraju imati zaseban omeđen trošak; taj je limit
još UNKNOWN i ne smije sakriti blokiranje playbacka ili neograničen cancel.
Detaljno objavljivanje i clock/admission ugovor zatvara PF-02.

Ciljna ploča je **JC1060P470C_I_W_Y**, izričito odabrana od korisnika.
Korisnik pretpostavlja P4 v3.2, ali stvarna revizija još nije potvrđena. Zadržava se
postojeći consumer minimum `ESP_HAL_CONFIG_MIN_CHIP_REVISION=302`; pretpostavka
nije očitana identifikacija. Točan uređaj, image i fizičko mjerenje nisu dostupni
kao dokaz ovog checkpointa. Nema flashanja ni otvaranja serijske sesije.

Board dokumenti navode 16 MiB flash i 32 MiB PSRAM. Oni ne određuju slobodan
model storage, slobodnu PSRAM ili internal RAM. Postojeći memory probe ispituje
64 KiB CPU-cached scratcha na 80 MHz i ne registrira allocator. Integration-check
ne pokreće produktne ownere ili heap. Boot/probe nije mjerenje rezidualne memorije
uz audio/USB/UI. Flash s firmwareom/OTA/assetima nije model allocation.

## Izvedeni postojeći native profil

Primjer koristi neizmijenjeni `publication::plan_features`, masku1081
(overview/BPM/local/global/meter; bez key/confidence/detail/3-band), dvije native
generacije i dijagnostički queue17. Queue17 i obje FPC rezolucije su usporedni
parametri, ne odobrene produkcijske alokacije. Ni jedan profil ne dodaje neuralni model.

| Hz | Frames/column | Working columns | Native accounting RV32 | Native + consumer overlap accounting |
|---:|---:|---:|---:|---:|
| 44100 | 256 | 103360 | 3882320 B | 4793168 B |
| 44100 | 32768 | 808 | 805760 B | 896192 B |
| 48000 | 256 | 112500 | 4156520 B | 5140488 B |
| 48000 | 32768 | 879 | 807890 B | 898890 B |

Native subtotal je working overview + queue + optional working arrays + canonical
retained graph accounting za dvije generacije. Optional arrays iznose532480 B:
4096 S4 i16384 S6 onset/flux elemenata po20 B, plus3072 native beata po40 B.
Za 48000/FPC32768 working overview je8790 B, queue136 B, dvije retained generacije
266484 B. Točno su odvojene promjenjive veličine od fiksnih ringova.

Overlap scenarij dodaje dvije consumer beat banke (73728 B), dvije rendered
waveform banke (n*8 B), source scratch6144 B i broker4096 B. To je deklarirani
scenarij rezidentnosti, ne izmjereni vršni allocator usage. `ServiceStorage::plan`
je alternativni **local-tempo-only** profil; njegov cijeli subtotal ne dodaje se
native subtotalu jer bi ponovio queue/onset/overview podatke. Za 48000/FPC32768
ta alternativa iznosi181846 B, a ne cijenu cjelovitog global/neuralnog puta.
Izvedena formula reprodukcijom zatvara i stari dijagnostički total117868 B.

Compiler za `riscv32imafc-unknown-none-elf` daje OwnedResult header704 B,
Session control3536 B i ResultPool control1752 B; na hostu su984/3648/2544 B.
Kontrole se izvještavaju odvojeno: canonical retained accounting već uključuje
logički header, a stvarni pool ima ugrađene strukture. Ne dodavati pune kontrole
na subtotal bez stvarnog allocation/lifetime mapiranja. To bi moglo dvaput
brojiti iste metapodatke. ABI/layout račun nije C ABI ni fizičko mjerenje.

## Ostali živi vlasnici i dvostruko brojanje

| Resurs / vlasnik | Pronađeni ili izvedeni payload | Lifetime i ograničenje dokaza |
|---|---:|---|
| Dva audio FX prostora | 1540704 B | uvjetno `render-graph-bringup`; heap payload ostaje živ nakon inicijalizacije, allocator/graph overhead dodatno |
| Dva canonical source history buffera | 131072 B | BSS pod odgovarajućim audio ownerom; nisu analiza/queue scratch |
| JC1060 RGB565 single/double framebuffer | 1228800 / 2457600 B | scenariji iz1024x600, ne alocirani/reservirani produktni brojevi; broj buffera i DMA/cache placement još otvoreni |
| Jedna owned beat/preview kopija | 38464 B | 3072 consumer beata +400 preview stupaca; Vec/header/allocator/cues i broj retained/pending kopija dodatno |
| Stream worker i scan kontrole | izvorni ceilings4864 /768 B | granice generičkih objecta s jednostavnim handleom, nisu nested future/stack/backends budget |
| Codec cache, USB/audio rings, Slint/UI, channels/executor/stog | UNKNOWN | kvalificirati točan feature/image; ne oduzimati iz32 MiB na temelju nepotpunog zbroja |
| Novi model/frontend/activations/decoder trace | UNKNOWN | nije odabran prihvaćen cijeli put; nema učitavanja težina ili inferencea |

Peak je maksimum zbroja istodobno živih ALLOCATIONS, razdvojen po internal/PSRAM/DMA
klasi. Radni/native/consumer izlazi mogu se preklapati s retained/pending kopijama
i zaustavljenim audio ownerima. Nema dokaza da playback pause oslobađa FX/UI/USB
memoriju. Nije opravdano zbrojiti sve scenarije kao stvarni peak, niti pretpostaviti
da su svi ostali vlasnici ugašeni pa preostalu PSRAM odobriti analizi.

Za stereo PCM16 na48000Hz cijelih600s zahtijeva115200000 B bez WAV headera, više
od dokumentirane32MiB PSRAM. F32 stereo zahtijeva230400000 B. Zato se cijeli izvor
ne zadržava u memoriji za ovu rutu. Postojeći `streaming.rs` već ima bounded
dvoprolazni WAV put bez zadržavanja cijelog objekta, sa source identitetom i
obveznim dovršenjem closea; njega ponovno koristiti. `source.rs` ima i stariji
whole-object put, koji nije rješenje za puni desetominutni izvor.
MP3/FLAC decoded-PCM streaming bridge, trošak svih prolaza i cancellation ostaju
otvoreni. Postojeći `StreamLimits::UNLIMITED` je compatibility opcija, ne produktni budget.

## Što treba izmjeriti za zatvaranje PF-01

1. Očitati stvarni board/chip revision i memory mapping na točnoj ploči/slici;
   prije flashanja potvrditi identitet, recovery i konkretan port prema runbooku.
2. Za odabrani produktni feature set izvući linker map i sve statičke owner/rezerve.
   Zasebno imenovati internal, PSRAM i DMA/uncached zahtjeve. Nema novih implicitnih
   alokacija modela ili providera radi dobivanja tog izvještaja.
3. Izmjeriti used/free/min-free i largest-contiguous block, stog/future/executor
   high-water te allocator overhead uz oba decka, UI i USB owner životne cikluse.
   Ponoviti stopped-analysis/playback-transition stanje; ne izmišljati marginu.
4. Nakon lifetime i fragmentacijskog izračuna odrediti analitičku rezervu i
   obrazloženu sigurnosnu marginu. Odabrati waveform rezoluciju prema UI potrebi,
   a ne automatski zadržati najskuplji full-source detail profil.
5. Prije product porta usporediti kompletan odabrani inference put i stvarne
   latencije s odobrenim ciljevima. Uvijek uključiti I/O/source passove i bank
   retention; već postojeća dijagnostika nema taj fizički dokaz.

Ovaj checkpoint završava statičku inventuru i bilježi ciljeve. PF-01 ostaje
**NEEDS_MEASUREMENT**, a veliki port i fizički PASS ostaju blokirani. **Sljedeći
spreman nezavisan korak je PF-02**, ugovor grida/metra i objavljivanja. PF-03
ne dobiva produktni prolaz iz nepotpunog memory inventara. H1 ostaje odgođen.

## Reprodukcija i provjere

Rust1.95.0. Novi primjer ne mijenja core, feature maskove ili default ponašanje.
Pokrenuto: fmt, focused Clippy s `-D warnings`, host probe, RV32 no-default library
build i LLVM-IR layout compile. Zaseban provjeravatelj ponovno računa svaki host
subtotal iz canonical formula i RV32 subtotal iz compiler type sizes. Četiri
profila se zatvaraju, bez alociranja punog PCM-a ili analitičkih nizova.
To nisu DSP accuracy testovi ni target execution.

```text
cargo fmt --all -- --check
cargo clippy -p libapta --locked --example product_budget_probe --target-dir BUILD -- -D warnings
cargo run -p libapta --locked --example product_budget_probe --target-dir BUILD
cargo build -p libapta --locked --lib --no-default-features --target riscv32imafc-unknown-none-elf --target-dir BUILD
rustc --crate-type lib --edition 2021 --emit=llvm-ir --target riscv32imafc-unknown-none-elf -L dependency=BUILD/riscv32imafc-unknown-none-elf/debug/deps --extern libapta=BUILD/riscv32imafc-unknown-none-elf/debug/liblibapta.rlib rust/examples/product_budget_probe.rs -o layout-riscv32.ll
```

`BUILD` je novi vanjski direktorij, ne source tree. Točni stdout/stderr, target IR
i `static-costs.json` nalaze se izvan Gita u WSL
`product-pf01-20261026/` pod postojećim libapta rust-rewrite evidence rootom.
Receipt veže bazu izvora, hash primjera, compiler i artefakte. Consumer izvori
su pročitani s`4a8c660183910b0950ae7ddabce9e8b395a2f10d`; checkout/pin nisu mijenjani.
Nema starog corpus replaya, treninga, promjene protokola ili VERSION1.0.1.
