# CS-01 binding: finite vrijednosti i javni provenance pregled

Datum: 2026-10-26. Baza: `0a4fa2d39b09368923fbb1a9256bf87c42630215`,
grana `rust-rewrite`. **FINITE_VALUES_PASS; RIGHTS_AND_PROVENANCE_UNRESOLVED.**
[Receipt](PRODUCT-COMPACT-BINDING.json), [dizajn](PRODUCT-COMPACT-CANDIDATE-SPEC.md),
[CS-02 tablice](PRODUCT-COMPACT-RESOURCE-ORACLES.md), [PF-01](PRODUCT-BUDGETS.md).

Korisnik potvrđuje da JC1060P470C_I_W_Y još nije dostupna. Ne pokušavati fizičko
mjerenje ili zamijeniti uređaj drugim COM portom. PF-01 ostaje NEEDS_MEASUREMENT;
silicon v3.2 i dalje je pretpostavka, ne očitan identitet. Nastavljen je samo
neovisni statički binding. Nema inferencije, treninga, quantizationa, novog
modela/corpusa, target executiona ili promjene source/provider pina.

## Što je zatvoreno

Provjeren je isti `generic_main_weights.pt`:
SHA-256 `5bbae630b4112f3c1193654e3dbc946b850caa06a2a82e10e8de9e48bb673519`,
3072890 B, Git blob `7aefd5e56ef65482be6f671c3cca8de457dad102`.
Nema ponovnog downloadanja ni odabira druge grane. CS-01 prethodno je vezao svih22
key/shape/stride/offset zapisa; sada je zatvoren i njihov raw numerički sadržaj:

| Provjera | Rezultat |
|---|---|
| Jedinstveni ZIP storage zapisi |7, little-endian float32 |
| Logički tenzori |22; contiguous views bez rupa ili preklapanja |
| Parametarske vrijednosti |767125 |
| NaN/Inf vrijednosti |0 |
| Hash i Git blob |podudaraju se s prethodnim frozen identitetima |
| Framework tensor/model konstrukcija |NOT_RUN |
| Glazbena točnost, native parity, P4 brzina/memorija |NOT_RUN / NOT_MEASURED |

Stdlib `zipfile` provjerava CRC i čita samo vezane storage zapise. `struct` dekodira
raw f32 skalare i provjerava konačnost. Zaseban verifier provjerava IEEE754 exponent
bits kao u32, bez float decodea; oba daju iste counts. Ovo jest čitanje numeričkog
weight payloada, za razliku od prethodnog metadata-only koraka. Nisu konstruirani
torch/NumPy tenzori, nije korišten pickle load niti izvršen callable/model kod.
Ne izvještavati finite PASS kao strict runtime load ili model-quality PASS.

## Prava nisu zatvorena

Pregledano je cijelo netruncirano Git stablo odabranog upstream commita
`bb90eb0a9065b101a4b4c4cb2b2061950266cb4b`, pinned README/config i javna GitHub
repo metadata. Nema imenovanog LICENSE/LICENCE/COPYING/NOTICE/model-card zapisa,
README nema license odjeljak, a GitHub metadata vraća `license=null`.
To opisuje pregledane dokaze; nije tvrdnja da dopuštenje ne može postojati drugdje.

Za ovaj checkpoint ostaje **LICENSE_UNRESOLVED** za BeatNet+ kod i weights.
Ne naslijediti licence iz BeatNet, madmom, NumPy ili članka na ovaj artefakt.
Nema redistribucije/ugradnje ili automatskog nastavka u produktni port.
Nije poslana poruka autoru ili drugoj osobi.

Potrebni dokaz treba izričito pokriti repository/revision, exact checkpoint digest,
uvjete upotrebe/izmjena i distribucije weights/koda u planiranom firmwareu.
Javna dostupnost datoteke i postojeći Python install primjer nisu taj dokaz.

## Provenance: poznata je Git linija, ne stvarni trening manifest

Weight-path Git povijest vraća upload commit
[`0001fb7e0d3deff862af67a516192225ccc2eb6a`](https://github.com/mjhydri/BeatNet-Plus/commit/0001fb7e0d3deff862af67a516192225ccc2eb6a),
author timestamp2026-04-13. Isti Git blob nalazi se u tom commitu i odabranom
upstream commitu. Datum Git unosa nije dokaz datuma ili postupka treniranja.

[Pinned README](https://github.com/mjhydri/BeatNet-Plus/blob/bb90eb0a9065b101a4b4c4cb2b2061950266cb4b/README.md)
i [generic config](https://github.com/mjhydri/BeatNet-Plus/blob/bb90eb0a9065b101a4b4c4cb2b2061950266cb4b/src/BeatNetPlus/configs/generic.yaml)
navode trening na Ballroom, Hainsworth, MUSDB18, Rock Corpus, RWC Pop/Jazz/
Royalty-Free i URSing, uz GTZAN samo za test. Config navodi seed42 i main mix/
aux drumless grane. To su deklaracije/config izvora, ne potvrđen training manifest
baš ovih weight bytes. Arhitektura i konačnost to ne mogu dokazati.

Nedostaju checkpoint-specific training-run/config identitet, stvarni membership/
split i recording/performance duplicate provjere. Ostaje
**INDEPENDENCE_UNRESOLVED**. GTZAN je u našem projektu već izložen i ne postaje
fresh acceptance ni ako autor navodi da nije u treningu. Formalni holdouti,
oznake i glazba nisu otvarani. Potrebni privatni manifesti/mappings ostaju izvan
Gita; javni report veže samo dopuštene aggregate/hash dokaze.

## Sljedeći uvjeti

CS-01 metadata/shape/finite i CS-02 matematički asset dio sada su zatvoreni.
**Prije PF-04 ostaju prava, checkpoint provenance i PF-01 fizički budžeti.**
Bez ploče ne popunjavati memorijske vrijednosti host brojkama ili deklariranim
kapacitetom PSRAM-a. Bez provenance dokaza ne proglašavati transfer neovisnim.
Ako prava/provenance za ovaj kandidat nije moguće potkrijepiti, odluka o drugoj
ruti pripada vlasniku; ovaj pregled ne odabire zamjenski model niti trening.
Nema novog package/runtime audita kao zamjene za te nedostajuće dokaze.

Vanjski stdlib inspector i neovisni rezultat su pod
`D:/AI/LIBAPTA/outputs/product-compact-binding-20261026/`, hash-bound u JSON-u.
Inspector prima samo prethodno vezanu ZIP datoteku/metadata, odbija identity drift,
provjerava storage coverage i ne prepisuje postojeći receipt. VERSION ostaje1.0.1.
