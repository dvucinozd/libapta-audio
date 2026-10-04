// SPDX-License-Identifier: Apache-2.0
//! Default-profile musical desktop path using the actual native publication graph.
use libapta::{
    analysis, global_analysis,
    owned_result::{GridStorage, Storage},
    publication::{plan_features, PublishedSession, ResultPool},
    result,
    session::{CancellationToken, SessionConfig, SessionState, WorkBudget},
    wav::Wav,
    waveform::{NormalizedSample, PcmView},
    *,
};

struct Slot {
    spans: Vec<WaveformSpan>,
    columns: Vec<WaveformColumn>,
    tempo: [TempoCandidate; 3],
    local_coverage: [FrameRange; 1],
    local: [GridSegment; 1],
    global_coverage: [FrameRange; 1],
    global: [GridSegment; 8],
    beats: Vec<Beat>,
    key: [KeyCandidate; 3],
    meter: [MeterSegment; 1],
    quality: [QualityRecord; 1],
}
impl Slot {
    fn new(columns: usize) -> Self {
        let segment = GridSegment {
            applicability_range: FrameRange::default(),
            anchor_position: FractionalFrame::default(),
            anchor_ordinal: 0,
            frames_per_beat: FramePeriod::default(),
            beat_count: 0,
            nominal_tempo_millibpm: 0,
            confidence: 0,
            state: FeatureState::Provisional,
            flags: 0,
            segment_id: 0,
            revision: 0,
        };
        Self {
            spans: vec![WaveformSpan::default(); 1],
            columns: vec![WaveformColumn::default(); columns],
            tempo: [TempoCandidate::default(); 3],
            local_coverage: [FrameRange::default(); 1],
            local: [segment],
            global_coverage: [FrameRange::default(); 1],
            global: [segment; 8],
            beats: vec![Beat::default(); global_analysis::MAX_BEATS],
            key: [KeyCandidate::default(); 3],
            meter: [MeterSegment {
                first_frame: 0,
                end_frame: 0,
                downbeat_frame: 0,
                downbeat_ordinal: 0,
                numerator: 4,
                denominator: 4,
                state: FeatureState::Provisional,
                confidence: 0,
                segment_id: 0,
            }],
            quality: [QualityRecord {
                feature: result::BPM,
                calibration_model_id: 0,
                evidence_coverage_permille: 0,
                confidence: 0,
                state: FeatureState::Provisional,
                flags: 0,
            }],
        }
    }
    fn storage(&mut self) -> Storage<'_> {
        Storage {
            overview_spans: &mut self.spans,
            overview_columns: &mut self.columns,
            tempo_candidates: &mut self.tempo,
            local_grid: GridStorage {
                coverage_ranges: &mut self.local_coverage,
                segments: &mut self.local,
                beats: &mut [],
            },
            global_grid: GridStorage {
                coverage_ranges: &mut self.global_coverage,
                segments: &mut self.global,
                beats: &mut self.beats,
            },
            key_candidates: &mut self.key,
            meter_segments: &mut self.meter,
            quality: &mut self.quality,
            ..Storage::default()
        }
    }
}

pub fn analyze(wav: &Wav<'_>) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    let config = SessionConfig {
        sample_rate: wav.source().sample_rate,
        channel_count: wav.source().channel_count,
        total_frames: wav.frame_count(),
        frames_per_column: 32768,
    };
    let features = result::WAVEFORM_OVERVIEW
        | result::BPM
        | result::LOCAL_BEATGRID
        | result::GLOBAL_BEATGRID
        | result::DYNAMIC_TEMPO
        | result::CONFIDENCE
        | result::GRID_LOCKING
        | result::MUSICAL_KEY
        | result::METER_DOWNBEAT
        | result::CALIBRATED_QUALITY;
    let limits = NativeLimits::default();
    let plan = plan_features(config, features, limits)?;
    let mut a = Slot::new(plan.overview.columns_per_slot);
    let mut b = Slot::new(plan.overview.columns_per_slot);
    let pool = ResultPool::new_with_requested_features(
        wav.source(),
        [a.storage(), b.storage()],
        limits,
        features,
    )?;
    let mut queue = vec![NormalizedSample::default(); 4096];
    let mut columns = vec![WaveformColumn::default(); plan.overview.working_columns];
    let mut bins = vec![analysis::OnsetBin::default(); plan.tempo_bins];
    let mut flux = vec![0.0; plan.tempo_bins];
    let mut global_bins = vec![analysis::OnsetBin::default(); plan.global_bins];
    let mut global_flux = vec![0.0; plan.global_bins];
    let mut beats = vec![Beat::default(); plan.global_beats];
    let mut session = PublishedSession::new(config, &mut queue, &mut columns, &pool)?;
    session.enable_tempo(&mut bins, &mut flux)?;
    session.enable_global_grid(true, &mut global_bins, &mut global_flux, &mut beats)?;
    session.enable_key()?;
    session.enable_meter()?;
    session.enable_calibrated_quality()?;
    let token = CancellationToken::new();
    let budget = WorkBudget::default();
    let channels = usize::from(config.channel_count);
    let mut scratch = vec![0.0; 4096 * channels];
    let mut first = 0;
    while first < wav.frame_count() {
        let count = wav.read_frames(first, &mut scratch)?;
        let accepted = session.push_pcm(PcmView::F32Interleaved(&scratch[..count * channels]))?;
        if accepted == 0 {
            return Err("unexpected PCM backpressure".into());
        }
        first += accepted as u64;
        session.process(budget, &token)?;
    }
    session.finish_input()?;
    while session.session().state() != SessionState::Complete {
        session.process(budget, &token)?;
    }
    let owned = pool.acquire()?;
    let mut tiles = [];
    let wire = result::from_session_result(&owned, &mut tiles, limits)?;
    let mut bytes = vec![0; result::serialized_size(&wire)?];
    result::write(&wire, &mut bytes, result::Limits::default())?;
    result::parse(&bytes, result::Limits::default())?;
    Ok(bytes)
}
