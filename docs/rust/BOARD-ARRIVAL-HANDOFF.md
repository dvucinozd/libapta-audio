# Nastavak nakon dolaska ploče

Datum usklađenja: **2026-10-27**. Grana: **rust-rewrite**.
Stanje: **PAUSED_BY_OWNER_WAITING_FOR_BOARD**.
Korisnik traži usklađenje dokumentacije, commit/push i nastavak razvoja kad
stigne ploča. Do tada nema novog implementacijskog ili eksperimentalnog koraka.

Aktualni napredak vodi [PRODUCT-FIRST-STATUS.json](PRODUCT-FIRST-STATUS.json).
[Indeks](README.md) razdvaja aktivne dokumente od zamrznutih zapisa;
[MIGRATION.md](MIGRATION.md) čuva tehničku povijest. Ovaj handoff ne mijenja
protokole, budžete, numeričke tolerancije ili rezultate.

## Gdje smo stali

| Korak | Dovršeno | Preostala granica |
|---|---|---|
| PF-00 / PF-02 | Produktni opseg i neutralni grid/publication ugovor | Nema isporučive pune native analize |
| PF-01 / RC-03 | Statička inventura, owner/lifetime analiza i priprema fizičkog protokola | Fizički resursi i dopuštene rezerve nisu izmjereni; odluka WAIT_EXTERNAL |
| HS01–HS06 / RC-02 | Sintetičke host komponente; HS06 čist četveroslučajni rezultat i audit | Nisu glazbena evaluacija ni potpuni trenirani graf |
| RC-04A-1 | Telemetry V1, offline parser i 19 kontrola integriteta | Nema live transporta ili fizičke kvalifikacije |
| RC-04A-2 | Fiksni no_std copy/math probe core, javne reference i quota/cancel provjere | Nema fizičkog izvođenja ili mjerenja |
| RC-04A-3 | Guarded adapter i pet čistih ELF/map/disassembly paketa; audit prolazi | BUILD_ONLY_LINKCHECK; execution_ready=false; NOT_QUALIFIED |
| RC-04B | Pripremljeni zahtjevi za točan uređaj i fizičke pokuse | NOT_RUN; nedostaju ploča i kvalificirane runtime mogućnosti |
| RC-05–RC-10 | Zadržan redoslijed nastavka | Nisu izvršeni; nema GO za puni model ili aktivaciju |

**RC-04A priprema je dovršena; roditeljski RC-04 nije dovršen.**
Izgrađeni HAL boot samo inicijalizira sustav i miruje. Zadržana probe funkcija
nije pozvana i ima jednokratnu statičku pohranu. Ne postoji potpuni fizički
warm-up/trial runner. Dolazak ploče sam po sebi ne čini te imageove spremnima
za registrirana mjerenja.

## Točne baze i sačuvani artefakti

- Posljednji objavljeni checkpoint prije ovog usklađenja dokumentacije:
  `8c32fdc47b4afcaa1b4752349423d5df904ae427`.
- Čist i pushan izvor svih pet target varijanti:
  `489e5af0beac1cef53c1276a3aad65d7687255b8`.
- [Rezultat paketa](PRODUCT-TARGET-ADAPTER-PACKAGE.md) i
  [strojni receipt](PRODUCT-TARGET-ADAPTER-PACKAGE.json) vežu izvor, alate,
  datoteke i odvojeni audit. Završni paket ima **49 datoteka / 39.411.271 B**.
- SHA-256 `package.json`:
  `9c460c062c54a2622185a2b464d25d2f98963f5b24aa845c4288e6cf56752a95`.
- Zabilježena kontrola: 14 izvornih datoteka, 24.174 registry source datoteke
  i 395 ranijih zamrznutih veza. To su provjere pripreme, bez uređaja.

```text
Repo: D:/AI/LIBAPTA/libapta-audio
Čisti paket: D:/AI/LIBAPTA/outputs/rc04a3-package-clean-489e5af-20261027/
Odvojeni audit: D:/AI/LIBAPTA/outputs/rc04a3-package-audit-489e5af-20261027/audit.json
Host provjere: D:/AI/LIBAPTA/outputs/rc04a3-final-checks-20261027-01/
```

Pet varijanti su copy, projection32, projection64, activation i logs. Ne zbrajati
njihove odvojene BSS rezervacije u rezidentni proizvod. ELF veličina s debug
podacima nije programirana flash veličina. Host vrijeme kompilacije i RSS nisu
P4 latencija ili zauzeće memorije. Raniji development paketi ostaju odvojeni od
završnog čistog paketa.

Sirovi paketi i logovi ostaju izvan Gita. Git čuva njihove receipte i hashove;
sam clone ne donosi vanjske artefakte. Sačuvati navedene direktorije i provjeriti
njihovu dostupnost pri nastavku. Novi firmware ili novi pokus trebaju vlastiti
izvorni SHA, image i novi izlazni direktorij; ne prepisivati stari dokaz.

## Što nedostaje

Cilj je **Guition JC1060P470C_I_W_Y**. Posljednja potvrda vlasnika kaže da ploča
nije dostupna. Revizija 3.2 je pretpostavka; očitana revizija, unit ID i servisni
port su UNKNOWN. Policy minimum 302 nije očitanje čipa. Dokazi s JC8012 ili
drugog ESP32-P4 uređaja ne kvalificiraju ovu ploču.

Prije fizičkog pokusa trebaju biti zatvoreni sljedeći uvjeti:

1. Točan uređaj, dostupnost, očitani chip/PCB/panel identiteti, servisni transport,
   sačuvan tvornički image, provjerljiv recovery i točan programming layout.
2. Kvalificirani BSP i jedinstveno vlasništvo memorije uz stvarne rezidentne
   audio/USB/UI ownere. Postojeći integration-check ne pokreće te ownere;
   64 KiB boot scratch nije allocator ni dokaz slobodnog produktnog budžeta.
3. Namjerno operator arming/control sučelje i ograničen producer/collector
   transport, vezan uz točan image, protokol, jedinicu i plan opažanja.
4. Ponovljiv potpuni raspored warm-upova i trialova s ponovnom uporabom buffera,
   bez odabira najbržeg rezultata ili tihog skraćivanja kohorte.
5. Provjera stvarnih numeričkih izlaza i digestova prema zamrznutim javnim
   referencama. Linkani simboli i host prolaz nisu target numerički PASS.
6. Stvarni clock/placement, allocator po memorijskim klasama, largest-block,
   stack/future/error metričke mogućnosti te neovisni asynchronous cancel
   requester. Nedostajuće vrijednosti ostaju null; ne zamjenjivati ih nulama.

Capture templateovi iz build paketa imaju null unit/revision/schedule/arming.
Nisu izvršni manifesti za ingest i ne popunjavaju se pretpostavkama.
Detalje i ograničenja pokusa određuje
[pripremljeni fizički protokol](PRODUCT-TARGET-PROBE-PROTOCOL.md) uz
[adapter registraciju](PRODUCT-TARGET-ADAPTER-PROTOCOL.md).

## Redoslijed kad ploča stigne

1. Pročitati ovaj handoff, vrh MIGRATION.md, live status i lokalni AGENTS.md.
   Provjeriti radno stablo, worktreeove i remote na `rust-rewrite`; pregledati
   nove commitove prije nastavka. Sačuvati paralelni rad i vanjske artefakte.
2. Zabilježiti stvarno dostupnu jedinicu i očitane identitete, pa zatvoriti
   backup/recovery/layout uvjete prije programiranja. Ne pretpostaviti COM port,
   flash offsete, slobodnu PSRAM ili reviziju iz policy minimuma.
3. Pregledati aktualni consumer izvor i prikladan izolirani checkout za RC-04B.
   Dosadašnja snimka consumer origin/main je
   `4a8c660183910b0950ae7ddabce9e8b395a2f10d`; to je povijesna baza koju treba
   osvježiti, ne tvrdnja o budućem remote HEAD-u. Sačuvati nepovezane lokalne
   datoteke/worktreeove. Consumer pin/provider i Guition zaštite ostaju važeći.
4. Registrirati točne runtime/BSP/transport/metric promjene i sposobnosti te
   kvalificirati nedostajući runner/verifier/cancel put. Ne uklanjati guard kao
   zamjenu za BSP. Za ovu promjenu trebaju zasebni testovi, source freeze,
   commit/push i provjeren puni remote SHA prije stvarnog mjerenja.
5. Tek kad su svi uvjeti spremni, zamrznuti izvršni image/manifest/schedule i
   provesti TP-00–TP-06 po fizičkom protokolu, uz sve predviđene varijante,
   warm-upove, trialove, operativne limite i sirovi dokaz. Rezultate klasificirati
   kao PASS/FAIL/INVALID/NOT_RUN po opsegu; nema automatskog retryja nakon pada.
6. Iz fizičkog dokaza zatvoriti opravdane PF-01 rezerve i RC-04B, pa u RC-05
   donijeti cijelu resource/rights odluku. Ako dokazi nedostaju, odluka ostaje
   WAIT. RC-06/puni port ne počinje bez potrebnih GO uvjeta i zasebnog odobrenog
   opsega; uspješan izolirani probe nije produktni GO.

Ne ponavljati nepromijenjene audite i ne stvarati novi mali sintetički pokus kao
zamjenu za nedostajući uređaj. Ovaj dokument završava pripremu za čekanje;
ne započinje novu implementaciju niti kontaktiranje autora modela.

## Granice isporuke ostaju iste

Prvi cilj je waveform, BPM, beatgrid i metar/downbeat za izvor do **600 s**,
44,1/48 kHz mono/stereo. Prvi korisni waveform/BPM do **5 s**, konačni grid/metar
do **60 s** i prekid rada/blokada objave do **50 ms** su odobreni produktni
ciljevi, još nepotvrđeni na P4. Key i kalibrirani confidence dolaze kasnije.

Svih devet fizičkih resource kategorija, exact-model prava/provenijencija,
neovisna glazbena evaluacija, puna native integracija i produktna aktivacija
ostaju otvoreni prema svojim ugovorima. Nije odobreno novo treniranje, izbor
drugog modela, kvantizacija ili offload. C/default/wire i **VERSION 1.0.1** ostaju
neizmijenjeni. Povijesni odbijeni/spent rezultati i zamrznute banke ostaju dokazi
sa svojim izvornim značenjem.

## Provjera ovog usklađenja

Ovo je dokumentacijska promjena. Provjerene su lokalne poveznice i Markdown
sidra u promijenjenim dokumentima, JSON sintaksa, veza handoff/status i
neizmijenjeni hashovi izvora i sačuvanog paketa. Status zadržava ranije rezultate,
hardverske UNKNOWN vrijednosti i sve otvorene produktne uvjete. Brojevi testova
u povezanim receiptima odnose se na njihove izvorne checkpointove; testovi DSP-a,
target build i pokusi nisu ponovno pokretani radi ove dokumentacijske promjene.
