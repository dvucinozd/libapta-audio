# Create an isolated blind follow-up packet; never rewrite earlier responses.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$review = Join-Path $repo 'build/key-label-review'
$destination = Join-Path $review 'listener-2'
$zipPath = Join-Path $review 'listener-2.zip'
$mapPath = Join-Path $review 'listener-2-coordinator-private.json'
foreach ($path in @($destination, $zipPath, $mapPath)) {
    if (Test-Path -LiteralPath $path) { throw 'Second-listener output already exists; never overwrite.' }
}
function Get-Sha([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
$answers = Join-Path $review 'submitted-20260904T131743.txt'
$coordinator = Join-Path $review 'coordinator-private.json'
if ((Get-Sha $answers) -ne '4141e8b4f7a7b99ee461306158603cbd08b4ecdef1c12296eb048b03d54b6bb5' -or
    (Get-Sha $coordinator) -ne '90d2b860fea3a694c15334356d1293731a8a0a9bc1506b228bdd7e4e8a349ed3') { throw 'Frozen review inputs changed.' }
$first = Get-Content -LiteralPath $coordinator -Raw | ConvertFrom-Json
# Frozen nine disagreements from the preserved first review; not new tuning data.
$disputed = @('A01','A02','A03','A04','A05','A06','A07','A09','A11')
$seed = 'apta-independent-listener-2-20260904-v1'
$ordered = $disputed | Sort-Object { [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($seed + ':' + $_))) }
$template = Join-Path $PSScriptRoot 'apta_key_blind_review_form.html'
$html = Get-Content -LiteralPath $template -Raw
$edits = @(
    @('12 snimki', '9 snimki'),
    @('i <= 12', 'i <= 9'),
    @("const id = 'A'", "const id = 'B'"),
    @('apta-blind-review-form-v1:', 'apta-blind-review-listener2-20260904-v1:'),
    @('odgovori-tonalitet-', 'odgovori-slusatelj2-tonalitet-'),
    @('SLIJEPA PROVJERA TONALITETA', 'SLIJEPA PROVJERA TONALITETA — OBRAZAC B')
)
foreach ($edit in $edits) {
    if (-not $html.Contains($edit[0])) { throw 'Form template changed; review adaptation first.' }
    $html = $html.Replace($edit[0], $edit[1])
}
$html = $html.Replace('Nemoj tražiti naziv pjesme ili rezultate algoritama.', 'Radi samostalno: nemoj gledati tuđe odgovore, tražiti naziv pjesme ni koristiti automatsko određivanje tonaliteta. Preslušaj cijelu snimku; instrument može pomoći pri provjeri tonike.')
New-Item -ItemType Directory -Path $destination | Out-Null
$mapping = @()
for ($i = 0; $i -lt $ordered.Count; $i++) {
    $oldSample = $ordered[$i]
    $newSample = 'B{0:d2}' -f ($i + 1)
    $row = @($first.private_rows | Where-Object sample -eq $oldSample)
    if ($row.Count -ne 1) { throw 'Nonunique private sample mapping.' }
    $source = Join-Path $review ('listener/' + $oldSample + '.wav')
    if ((Get-Sha $source) -ne $row[0].sha256) { throw 'Original listener audio changed.' }
    $target = Join-Path $destination ($newSample + '.wav')
    Copy-Item -LiteralPath $source -Destination $target
    if ((Get-Sha $target) -ne $row[0].sha256) { throw 'Copied audio mismatch.' }
    $mapping += [ordered]@{ sample=$newSample; first_sample=$oldSample; sha256=$row[0].sha256 }
}
[IO.File]::WriteAllText((Join-Path $destination 'index.html'), $html, [Text.UTF8Encoding]::new($false))
$instructions = "SLIJEPA PROVJERA TONALITETA`n`nRaspakiraj cijeli ZIP u jednu mapu. Otvori index.html u Chromeu ili Edgeu.`nDevet WAV snimki mora ostati u istoj mapi uz obrazac.`nRadi samostalno, bez razgovora o odgovorima i bez automatskih key-detektora.`nZa provjeru tonike mozes koristiti instrument. Ako nisi siguran, nemoj pogadjati.`nNa kraju klikni Spremi odgovore (.txt) i vrati samo spremljenu datoteku.`nCSV nije potreban. Nista se ne salje na internet.`n"
[IO.File]::WriteAllText((Join-Path $destination 'UPUTE.txt'), $instructions, [Text.UTF8Encoding]::new($false))
$private = [ordered]@{ format='apta-second-listener-packet-1'; acceptance_claim=$false; seed=$seed; first_answers_sha256=(Get-Sha $answers); first_coordinator_sha256=(Get-Sha $coordinator); template_sha256=(Get-Sha $template); form_sha256=(Get-Sha (Join-Path $destination 'index.html')); rows=$mapping }
[IO.File]::WriteAllText($mapPath, ($private | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
$files = @(Get-ChildItem -LiteralPath $destination -File)
if ($files.Count -ne 11 -or ($html -match 'A\d{2}|track-[0-9a-f]|private_rows|baseline|essentia')) { throw 'Listener privacy/file inventory check failed.' }
Compress-Archive -LiteralPath $files.FullName -DestinationPath $zipPath -CompressionLevel Fastest
"PASS: 9 audio copies verified; 11 listener files; private mapping outside listener folder and ZIP."
