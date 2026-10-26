# PF-02: ugovor grida, metra i objavljivanja

Datum: 2026-10-26. **CONTRACT_COMPLETE**, uz native generated geometrijske provjere.
Baza izvora: `2055487efbc07d865b499f24c69d8a829afaf30f`, `rust-rewrite`.
[Opseg](PRODUCT-SCOPE.md), [budžeti](PRODUCT-BUDGETS.md),
[strojni ugovor i receipt](PRODUCT-GRID-CONTRACT.json),
[javni native primjeri](../../rust/tests/product_grid_contract.rs).

Ovo određuje budući prvi produktni profil. Ne implementira detektor, admission/
progress mehanizam ili evaluator. Ne mijenja postojeći wire, proizvođače, stare
protokole ili rezultate. PF-04 mora registrirati stvarni konstruktor/pokus prije
njihove implementacije i real mjerenja. PF-01 ostaje NEEDS_MEASUREMENT.

## 1. Razredi ugovora i statusi

| Razred | Što dokazuje | Što ne slijedi automatski |
|---|---|---|
| GEOMETRY_VALID | source/Q32/range/ordinal aritmetika | procjena ritma ili potpun izvor |
| WIRE_NATIVE_VALID | postojeći writer/reader i native validator prihvaćaju podatke | consumer podržava svaki generički prikaz |
| PRODUCT_SUPPORTED | cijeli izvor i sve odredbe ovog užeg profila zadovoljene | glazbena točnost, memorija ili fizički rokovi |
| MUSICAL_PASS | registrirane metrike prolaze na dopuštenim podacima | transfer, safety, P4 ili release |

Nazivi su buduće semantičke kategorije, ne novi Rust Error ili wire enum.
UNAVAILABLE označava nedostatak dokazivog izlaza; UNSUPPORTED poznatu nepodržanu
geometriju/opseg; CAPACITY prekoračenu granicu; INVALID korupciju/integrity grešku.
UNKNOWN je stanje potpore/reference, ne tiho točan beat ili implicitni 4/4.
Partial waveform/BPM može ostati koristan, ali nije uspješan puni grid/metar.

## 2. Koordinate, rasponi i redoslijed

- Autoritet je dekodirani source frame pri izvornom sample rateu44100/48000Hz.
  Model/resampler može koristiti drugu mrežu, ali mora imati točan dokumentiran
  povrat u source koordinate. Milliseconds služe tek consumer prezentaciji.
- Source raspon je `[0,N)`, N poznat pri FINAL. Za prvi profil
  `0 < N <= 600*sample_rate`, checked arithmetic. Nema prefiksnog FINAL-a cijele
  pjesme, clampanja događaja na EOF ni zaključivanja trajanja iz analize/kataloga.
- Svaki applicability/ownership raspon je `[F,E)`. F je uključen; E isključen,
  uključujući frakcionalne Q32 točke neposredno prije E. Nije riječ o zaokruženim ms.
- Pozicija je `q=whole_frame*Q+fraction`, `Q=2^32`. Ordinal je signed i64 i
  označava kontinuitet, a ne downbeat. Zero, negativni ordinals i faza su odvojeni
  pojmovi; negative source koordinate nisu dopuštene.
- Transport/parser mora čuvati u64/i64 i Q32 bez gubitka. U JSON adapterima
  booleans nisu integers, NaN/Inf i duplicate keys nisu valjani, a veliki Q32
  integer ne smije proći kroz binary64/JavaScript Number. Koristi checked integer
  ili točan rational za geometriju; float/display pretvorba nije izvor istine.
- Objavljene točke strogo rastu po Q32 poziciji i imaju uzastopne ordinals.
  Susjedni segmenti raspon dijele bez preklapanja: granična točka pripada desnom
  segmentu. Nema rebasea vremena/ordinals na početku novog prozora.
- Model anchor je izvorno valjana matematička točka i može biti prije F.
  Objavljuju se samo owned točke. Ne pomiče se anchor da bi staro pravilo prošlo.

Za A i P kao Q32 anchor/period, početni ordinal b0:

```text
P > 0
j_first = max(0, ceil((F*Q-A)/P))
j_last  = floor((E*Q-1-A)/P)
point(j) = A+j*P; ordinal(j) = b0+j
```

Minus1 je jedan Q32 unit, ne jedan frame. Prazan skup nema izmišljeni witness.
`beat_count` mora odgovarati točno deklariranim owned točkama. Budući produktni
validator mora zasebno provjeriti potpunost; `beat_at(index)` je bounded accessor
i sam po sebi ne dokazuje da je proizvođač deklarirao sve potrebne beatove.
Checked u64/i64 i postojeća intermediate-overflow pravila moraju ostati sačuvana;
matematički u128 izračun nije dopuštenje za širenje javnog native ugovora.

Kad se period izvodi iz tempa T u millibpm, koristi se
`P=floor(sample_rate*60000*Q/T)`. Kod eksplicitnih beatova autoritet su njihove
pozicije; nominalni tempo nije zamjena za stvarne outgoing intervale. Postojeći
native tempo/period validator vrijedi uz ostalu geometriju.

Consumer pretvorba za ne-negativnu Q32 poziciju zaokružuje jednom, half-up:
`ms=floor((q*1000+(sample_rate*Q)/2)/(sample_rate*Q))`.
Provjeriti width i strogo rastuća pretvorena vremena. Ne deduplicirati različite
source točke nakon ms kolizije. Točka prije EOF smije se prikazno zaokružiti na
duration_ms; to ne daje vlasništvo točki na samom source EOF-u.

## 3. Ručno izveden primjer s context anchorom

Source48000Hz, `N=120000`, prozor `[48000,120000)`,
anchor `12000.5` frameova, anchor ordinal `-4`, tempo204800millibpm.
P je točno14062.5 frameova; `j_first=3`, `j_last=7`, pet owned točaka.
Anchor ostaje12000.5; prozor nije full-source produktni FINAL.

| j | Source pozicija | Ordinal b | Consumer ms | Faza3/4 uz d=0 | Faza4/4 uz d=0 |
|---:|---:|---:|---:|---:|---:|
| 3 | 54188 | -1 | 1129 | 2 | 3 |
| 4 | 68250.5 | 0 | 1422 | 0 | 0 |
| 5 | 82313 | 1 | 1715 | 1 | 1 |
| 6 | 96375.5 | 2 | 2008 | 2 | 2 |
| 7 | 110438 | 3 | 2301 | 0 | 3 |

Sljedeća točka124500.5 nije owned. Frakcija0.5 je `fraction_q32=2147483648`.
MTRD witness `(downbeat_frame=68250, downbeat_ordinal=0)` označava istu grid
točku68250.5; MTRD ne pohranjuje frakciju i ne pomiče grid na68250.0.
Promjena framea na68251 ili ordinala na1 mora pasti cross-validaciju.
Core signed-ordinal primjer ne proširuje frozen P1/MC Python prototipove koji
imaju vlastite uže domene; njihovi ulazi i izlazi ostaju nepromijenjeni.

## 4. Metar, faza i objavljeni witness

Prvi produktni profil podržava jedan full-source3/4 ili4/4, denominator4.
Globalni FINAL grid i metar moraju pripadati istoj source/revision/ownership
geometriji. Header MTRD jednak je prvom meter segmentu; witness je exact owned
grid point s istim whole frameom i ordinalom. Q32 frakcija ostaje u gridu.

Za beat ordinal b i meter witness d:
`phase(b)=(b-d).rem_euclid(m)`.
Nema zaključivanja faze iz `b mod m` bez poznatog witnessa. Za isti grid, metar,
revision i raspon witness `d+k*m` daje iste faze SAMO ako i on stvarno postoji
kao owned točka. Isti residue na drugom gridu ne dokazuje istu fizičku fazu.

**Odabrana buduća konvencija:** nakon konstrukcije i provjere cijelog grida i
modelove meter phase class, objaviti najraniju owned točku te downbeat klase
u cijelom izvoru. Izbor je determinističan i reference-free. Ako takve točke nema,
metar je UNAVAILABLE. Ovaj izbor ne konstruira novi detektor i ne prepisuje
stari witness, P1 tuple ili već objavljenu generaciju. Stari MC-00 retention
ugovor nije izmijenjen. Nema biranja witnessa prema poznatom rasponu anotacija.

Metar3/4 ->4/4 unutar izvora nije podržan ovim profilom, iako generički MTRD
može opisati više meter segmenata. Odvojeni window modeli nisu automatski jedan
kontinuiran source grid. Nema umetanja 4/4 kad procjena ne postoji.

## 5. Puni source i kapacitet

Produktno podržan puni grid zahtijeva:

1. Izvor bez poznatog nedostajućeg PCM raspona i valjanu source identifikaciju.
   Tišina nije PCM gap; granica reference anotacije također nije source gap.
2. Jedan autoritativni kontinuiran SEGMENTS ili EXPLICIT prikaz koji pokriva
   `[0,N)`, uz uzastopne ordinals i postojeće applied revision provjere.
3. FINAL grid/meter, bez provisional-phase ili neriješene poznate phase/tempo-family
   ambiguities. Takva dvosmislenost mora biti eksplicitna, a ne sakrivena u confidence0.
4. Najviše8 segmenata i3072 eksplicitna beata; broj svih consumer owned beatova
   mora stati u stvarni destination/bank limit. Kompaktni segment ne oslobađa
   consumer od provjere broja proširenih beatova i ukupnog10-minutnog limita.
5. Strogo rastuće source i pretvorene ms koordinate, iste frame/ordinal veze metra,
   jedan full-source meter i validan životni vijek native/consumer spremnika.

Važna postojeća native granica: generički EXPLICIT writer može prihvatiti
promjenjive intervale, ali `native_validation::grid_tempo` kod EXPLICIT ulaza
provjerava svaki interval prema jednom selected tempu. Zato generic wire
roundtrip nije dokaz da promjenjivi raw events mogu proći kroz native builder.
Prvi promjenjivi-tempo put mora koristiti valjane segmente, njihove period/tempo
veze i dynamic/revision pravila unutar kapaciteta, ili stati zbog nepodržanosti.
Ovaj ugovor ne uklanja taj validator niti registrira novu explicit reprezentaciju.

Known gap, sparse/hybrid, meter change ili nepodržan raspon vraća eksplicitnu
nepodržanost. Capacity/overflow ne vraća trunciran potpuno ispravan rezultat.
Neispravan global grid ne mijenja se tiho lokalnim. Primjer dvaju segmenata
`[0,96000)` pri120BPM i`[96000,192000)` pri180BPM ima ordinals0..9 i granicu96000
samo jednom. S gapom`[96000,120000)` nema potpune produktne pokrivenosti čak i
kada generički wire writer prihvati gapped podatke.

Za10min/300BPM na44100/48000Hz točno3000 regularnih owned beatova stane u3072;
EOF beat nije dio skupa. To je provjera geometrije, ne procjena stvarne pjesme.

## 6. Admission, progress, FINAL i cancellation

| Faza posla | Točno pravilo budućeg produkta |
|---|---|
| REFUSED/BUSY | posao nije prihvaćen; postojeća analiza ostaje, UI dobiva eksplicitan status |
| ACCEPTED | rezervirani job/source/media/load ID i storage policy; počinje monotoni clock za5s/60s |
| PROGRESS | keyed waveform preview i BPM estimate, s immutable sequence/lifetime; nije prihvaćena authoritative TrackAnalysis generacija |
| VALIDATED | cijeli izvor/EOF/identity/prolazi/close i grid/metar/capacity provjereni; referentne oznake nisu ulaz |
| DEFERRED | consumer trenutačno zauzet; banka se zadržava, provjere identiteta se ponavljaju, clock se ne pauzira |
| COMMITTED | atomarna predaja finalne generacije samo aktualnom stopped-deck owneru i uz potvrđenu predaju banke |
| CANCELLED/STALE/FAILED/EXPIRED | nema nove autoritativne objave; fence i bounded recovery/cleanup |

Clock uključuje queue/čitanje/inicijalizaciju svih prihvaćenih poslova. Prvi korisni
cilj do5s znači stvarno upotrebljiv waveform preview i podržanu BPM procjenu;
spinner, BPM0/UNKNOWN ili prihvaćen posao koji još čeka banku ne zatvaraju taj cilj.
Procjena ostaje označena kao provisional; nije obećanje pune glazbene točnosti.
Konačna predaja source-wide rezultata mora biti do60s, uključujući deferred
vrijeme i obvezne identity/close provjere; promašen rok je izmjeren neprolaz.

**Integracijska rupa koju treba implementirati kasnije:** aktualni consumer
odbija zamjenu već prihvaćene analize. Zato PROGRESS ne smije prerano zauzeti taj
slot i zatim zaobići zaštitu radi FINAL-a. Potreban je odvojen job/progress prikaz
s fenced source/media/load identitetom; njegov konkretni API pripada PF-08/09.
Ovdje nije implementiran niti se pretpostavlja da danas postoji u firmwareu.
Nova ANLZ/prihvaćena analiza koja se pojavi tijekom posla ima prednost; native
commit ponovno provjerava owner pravila i ne prepisuje je.

Playback-start/cancel/onemogućena generacija odmah zatvaraju pravo objave.
U50ms od prihvaćenog zahtjeva mora prestati analitički rad i kasna objava mora
biti onemogućena. Taj cilj uključuje rad koji je bio u backendu/I/O, ne samo
postavljanje atomic zastavice. Bank/lease recovery ima vlastit bounded cleanup
limit koji je u PF-01 još UNKNOWN; bez njega nema produktnog cancel PASS-a.
Novi posao ima novu identifikaciju; nema oživljavanja otkazanih rezultata.

## 7. Reference i metrike za buduću registraciju

Konstrukcija i izbor witnessa ne dobivaju truth, D/E maske ili stare verdictove.
Tek nakon freezea kandidata evaluation smije koristiti izričito dopuštene oznake.
Sljedeće su definicije i obvezni izvještaji, ne novi brojčani acceptance pragovi
ili registracija real pokusa. PF-04 mora odabrati točan skup, baseline, prozore,
uključene metric families, gateove i tolerancijske implementacije prije rezultata.

| Metrika | Numerator / denominator i postojeći autoritet |
|---|---|
| Geometry/delivery | broj geometrijski/product-valid rezultata / svi registrirani izvori; unknown/unsupported/failed zasebno, bez uklanjanja |
| Nominal grid | canonical `score_points` s model-period intervalima; period error<=1%, phase distance<=0.10 lokalnog referentnog beata |
| Actual grid | isti scorer, outgoing interval iz sljedeće source točke s ordinalom+1; dopuštena contiguous granica prozora; nedostajući successor=None |
| Period/phase/joint point | correct supported points / supported points s odgovarajućom truth/successor potporom; prijaviti sve predicted/scored/unknown-successor brojeve |
| Confirmed window | canonical all-points uvjet: complete reference, neprazan skup, svaka predikcija scored i svi uvjeti obitelji prolaze; joint=period AND phase |
| Complete sequence | confirmed one-to-one reference beat matching i period uvjet prema unchanged scoreru; nije isto što i visoki event F1 |
| Window stope | prikazati numerator, N_all, N_reference_complete i N_incomplete; conditional potvrđeni/N_complete odvojiti od konzervativnog potvrđeni/N_all i UNKNOWN |
| Meter M | točan numerator/denominator / meter-emitted, uz emitted/N_all; ne koristiti phase kao zamjenu za točan metar |
| Published witness D | downbeat-correct / D-supported, uz D-supported/N_all i UNKNOWN; točka mora biti unutar inclusive referentnog beat spana |
| Phase evidence E | all-supported-phase-points-correct windows / E-supported windows; zasebno točni/supported points, svaki denominator uz njegovu jedinicu |
| Event F1/CMLt | ako PF-04 uključuje ovu obitelj: isti `event_metrics`/mir_eval, trim5s, F1 threshold70ms, macro po svim registriranim izvorima, prazna sekvenca0 |
| Confidence | UNQUALIFIED u prvom profilu; nulom izbjegnute high-confidence greške nisu safety acceptance |

`score_points` i `score_point_windows(actual=...)` ostaju izvor točnih window
booleana; point-rate denominatori moraju dodatno sačuvati razliku scored points
i poznatih outgoing intervala. Lokalni referentni period je neizmijenjeni median
do pet najbližih reference gapova po početnoj source točki, sa stabilnim source-order
ties. Ne zamijeniti ga udaljenošću do jednog susjeda ili vlastitim kandidat-periodom.
Nema izmišljanja outgoing perioda na konačnom EOF-u radi complete-sequence PASS-a.

Stari helperi imaju registrirane domene (među ostalim48000Hz,30s i ponegdje
nonnegative model ordinals). Produktni44100Hz/source-wide signed ugovor ih ne
proširuje automatski. PF-04 mora registrirati potreban opt-in source-rate/domain
adapter i dokazati parity na njihovoj izvornoj domeni prije uporabe izvan nje.
Za event sekunde koristi stvarni source rate; nikada ne dijeli44100Hz frameove
hardkodiranih48000 niti ih prethodno zaokružuje u lažne48000Hz integer frameove.
Koordinatna pretvorba mora čuvati vrijeme/Q32 i imati vlastite boundary/tie testove;
originalni helperi i njihovi stari brojevi ostaju nepromijenjeni.

Za fiksni grid i metar definiraj `K={owned p : (p.ordinal-d) mod m=0}` bez
referenci. E je evaluacijski podskup K unutar inclusive reference beat spana.
Prazan E daje UNKNOWN/null conditional accuracy. Za E window correctness svi
podržani phase points moraju biti ispravni po unchanged canonical downbeat
pravilu<=0.10 lokalnog beata. Meter i phase correctness ostaju odvojeni.

Javni primjer:120BPM, source`[0,240000)`, beats0,24000,...216000,4/4.
Witness0@0 i4@96000 oba su validna i daju iste faze. Uz truth span`[48000,216000]`,
D prvog nema potporu, drugog ima; K ima tri točke0/96000/192000, E dvije.
Uz truth span`[240000,250000]` E je prazan, a ne0%/100% correct. Witness se NE
mijenja prema tim spanovima. Ovaj primjer ne mijenja stari MC-03 verdict.

Paired usporedba prvo veže isti source/window/metric family i potpuni reference
support profil. Objaviti transitions correct/incorrect/UNKNOWN/unsupported,
paired fixes/breaks i one-sided support losses zasebno. Nema usporedbe točke s
istim indeksom ili ordinalom na različitim gridovima bez dokaza zajedničke
geometrije. Skupovi različitih point denominatora ne postaju jedan zajednički point
accuracy. Stari veto uključuje točno svoju staru potporu; nema retroaktivnog
odbacivanja boundary/incomplete slučajeva. MC-08 ostaje interpretacija, ne dobitak.

Puni DJ acceptance ostaje svoj zaseban frozen protokol (uključujući cyclic
downbeat phase kriterij); gornja razvojna point/window dijagnostika ga ne zamjenjuje.

## 8. Javni slučajevi, provedene provjere i nastavak

Novi Rust suite ima12 testova: ručni Q32/context primjer;3/4 i4/4 faze;
F/E vlasništvo; posljednji Q32 unit prije EOF; promjena tempa na shared granici;
gap nasuprot generic wire validity; GGRD/MTRD whole-frame/ordinal roundtrip i
krive veze; jednaka faza uz različit D support;8/3072 capacity bez partial outputa;
10min/300BPM na oba sample ratea; signed ordinal/source arithmetic overflow;
generic EXPLICIT timing nasuprot native selected-tempo konzistenciji.
Oni provjeravaju postojeći kod i contract ilustracije. Ne implementiraju novi
produktni validator, progress owner, detector ili scorer. Changing-meter/sparse/
hybrid odbijanje prvog profila potvrđeno je pregledom actual consumer izvora;
to nije novo izvršavanje consumer testa na tom snapshotu.

Relevantni postojeći native controls: `native_coordinates`, `grid`,
`native_validation`, `builder` te feature-enabled `meter_binding`.
Konačni debug i release daju po48 prolaza (12 novih i36 postojećih), ukupno96
testnih izvršavanja, bez grešaka ili ignored slučajeva. Fmt i focused Clippy
s `-D warnings` prolaze. Raniji geometric-only primjeri i intermediate Clippy
precedence prijava ostaju u vanjskim pripremnim logovima; konačni primjer ima
točan tempo/period i zaseban native EXPLICIT konzistencijski test.
Debug/release, fmt i Clippy logovi i konačni exact source/test hashovi
vezani su u receipt JSON-u. Nema inferencea, real/private podataka ili evaluacije.
Frozen evaluatori, wire, capovi, default DSP i VERSION1.0.1 ostaju nepromijenjeni.

PF-02 zatvara ugovor, a PF-01 memory/placement/cleanup gateovi ostaju otvoreni.
PF-03 produktna route odluka još nema sve preduvjete. Sljedeće je dopuštena samo
bounded statička PF-03 inventura cijelog puta iz programskih izvora i javnih
agregata, kako dopušta PF-01 pravilo o statičkim izračunima. Ona nije završetak
PF-03 odluke niti preskakanje PF-01: nepoznate rezerve i uređaj i dalje blokiraju
produktni GO i veliki port. Bez uvoza modela, audio/label čitanja ili novog real
pokusa. H1 ostaje odgođen do odluke PF-04.
