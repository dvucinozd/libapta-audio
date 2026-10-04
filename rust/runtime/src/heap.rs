// SPDX-License-Identifier: Apache-2.0
//! Independently heap-owned native snapshots. Only validated OwnedResult graphs
//! enter this boundary; empty static header slices hold no source references.
use libapta::{owned_result::OwnedResult, *};
use std::sync::{Arc, RwLock};

fn copy<T: Clone>(input: &[T]) -> Result<Vec<T>, Error> {
    let mut data = Vec::new();
    data.try_reserve_exact(input.len())
        .map_err(|_| Error::LimitExceeded)?;
    data.extend_from_slice(input);
    Ok(data)
}
#[derive(Default)]
struct GridArrays {
    coverage: Vec<FrameRange>,
    segments: Vec<GridSegment>,
    beats: Vec<Beat>,
}
impl GridArrays {
    fn copy(input: Option<NativeGrid<'_>>) -> Result<Self, Error> {
        match input {
            Some(g) => Ok(Self {
                coverage: copy(g.coverage_ranges)?,
                segments: copy(g.segments)?,
                beats: copy(g.beats)?,
            }),
            None => Ok(Self::default()),
        }
    }
    fn view<'a>(&'a self, header: NativeGrid<'static>) -> NativeGrid<'a> {
        NativeGrid {
            coverage_ranges: &self.coverage,
            segments: &self.segments,
            beats: &self.beats,
            ..header
        }
    }
}
fn grid_header(g: NativeGrid<'_>) -> NativeGrid<'static> {
    NativeGrid {
        coverage_ranges: &[],
        segments: &[],
        beats: &[],
        ..g
    }
}

/// A complete independent native graph with no caller storage or session lifetime.
/// Every payload, metadata field and provenance string is copied. Standard heap
/// allocation is explicit, fallible and bounded by the supplied native limits.
/// This is a native ownership boundary, not a C allocator/context implementation.
pub struct HeapResult {
    header: NativeResultInput<'static>,
    available: u64,
    changed: u64,
    spans: Vec<WaveformSpan>,
    columns: Vec<WaveformColumn>,
    tiles: Vec<NativeTile>,
    detail_columns: Vec<WaveformColumn>,
    tempo: Vec<TempoCandidate>,
    local: GridArrays,
    global: GridArrays,
    key: Vec<KeyCandidate>,
    meter: Vec<MeterSegment>,
    quality: Vec<QualityRecord>,
    text: [Option<Vec<u8>>; 8],
    source_id_bytes: bool,
    retained_bytes: usize,
    // Drop the accounting lease after every owned payload array is released.
    context_resource: Option<crate::context::Resource>,
}
impl HeapResult {
    pub fn copy_from(result: &OwnedResult<'_>, limits: NativeLimits) -> Result<Self, Error> {
        Self::copy_view(
            &result.view(),
            result.requirements(),
            result.available_features(),
            result.changed_features(),
            limits,
        )
    }
    /// Copy a graph obtainable only from actual native session processing.
    pub fn copy_snapshot(
        snapshot: &libapta::session_snapshot::SessionSnapshot<'_>,
        limits: NativeLimits,
    ) -> Result<Self, Error> {
        let available = snapshot.available_features(limits)?;
        Self::copy_view(
            &snapshot.view(),
            snapshot.requirements(limits)?,
            available,
            available,
            limits,
        )
    }
    /// Copy an external native graph with full external validation. This never
    /// grants the trusted session exceptions carried by `copy_from`.
    pub fn from_native(input: &NativeResultInput<'_>, limits: NativeLimits) -> Result<Self, Error> {
        let validation = libapta::native_validation::validate(input, limits)?;
        let requirements = libapta::owned_result::requirements(input, limits)?;
        Self::copy_view(
            input,
            requirements,
            validation.available_features,
            validation.available_features,
            limits,
        )
    }
    fn copy_view(
        input: &NativeResultInput<'_>,
        r: libapta::owned_result::Requirements,
        available: u64,
        changed: u64,
        limits: NativeLimits,
    ) -> Result<Self, Error> {
        // Validate supplied count/byte limits without reinterpreting trusted
        // session exceptions as external builder inputs. Public graphs are immutable.
        if r.overview_spans > limits.maximum_overview_spans
            || r.overview_columns
                .checked_add(r.detail_columns)
                .ok_or(Error::LimitExceeded)?
                > limits.maximum_waveform_columns
            || r.detail_tiles > limits.maximum_detail_tiles
            || r.tempo_candidates > limits.maximum_tempo_candidates
            || r.key_candidates > limits.maximum_key_candidates
            || r.meter_segments > limits.maximum_meter_segments
            || r.quality > limits.maximum_quality_records
            || [r.local_grid, r.global_grid].iter().any(|g| {
                g.coverage_ranges > limits.maximum_grid_coverage_ranges
                    || g.segments > limits.maximum_grid_segments
                    || g.beats > limits.maximum_grid_beats
            })
        {
            return Err(Error::LimitExceeded);
        }
        let minimum = r
            .retained_bytes
            .checked_sub(core::mem::size_of::<OwnedResult<'_>>())
            .and_then(|n| n.checked_add(core::mem::size_of::<Self>()))
            .ok_or(Error::LimitExceeded)?;
        if minimum > limits.maximum_storage_bytes {
            return Err(Error::LimitExceeded);
        }
        let metadata = input.metadata.unwrap_or_default();
        let mut text = [None, None, None, None, None, None, None, None];
        let fields = [
            metadata.producer_name.map(str::as_bytes),
            metadata.producer_version_string.map(str::as_bytes),
            metadata.backend_name.map(str::as_bytes),
            metadata.backend_version.map(str::as_bytes),
            metadata.application_source_id.map(|v| match v {
                SourceId::Text(s) => s.as_bytes(),
                SourceId::Bytes(b) => b,
            }),
            metadata.comments.map(str::as_bytes),
            Some(input.provenance.source_name.as_bytes()),
            Some(input.provenance.source_version.as_bytes()),
        ];
        for (to, from) in text.iter_mut().zip(fields) {
            *to = from.map(copy).transpose()?;
        }
        let header = NativeResultInput {
            source: input.source,
            info: input.info,
            provenance: Provenance {
                origin: input.provenance.origin,
                source_name: "",
                source_version: "",
            },
            metadata: input.metadata.map(|m| Metadata {
                creation_unix_time: m.creation_unix_time,
                ..Metadata::default()
            }),
            overview: input.overview.map(|w| NativeOverview {
                spans: &[],
                columns: &[],
                ..w
            }),
            detail: input.detail.map(|_| NativeDetail {
                tiles: &[],
                columns: &[],
            }),
            tempo: input.tempo.map(|t| TempoView {
                candidates: &[],
                ..t
            }),
            local_grid: input.local_grid.map(grid_header),
            global_grid: input.global_grid.map(grid_header),
            revision: input.revision,
            key: input.key.map(|k| Key {
                candidates: &[],
                ..k
            }),
            meter: input.meter.map(|m| Meter { segments: &[], ..m }),
            quality: &[],
        };
        let mut result = Self {
            context_resource: None,
            header,
            available,
            changed,
            spans: copy(input.overview.map_or(&[], |w| w.spans))?,
            columns: copy(input.overview.map_or(&[], |w| w.columns))?,
            tiles: copy(input.detail.map_or(&[], |d| d.tiles))?,
            detail_columns: copy(input.detail.map_or(&[], |d| d.columns))?,
            tempo: copy(input.tempo.map_or(&[], |t| t.candidates))?,
            local: GridArrays::copy(input.local_grid)?,
            global: GridArrays::copy(input.global_grid)?,
            key: copy(input.key.map_or(&[], |k| k.candidates))?,
            meter: copy(input.meter.map_or(&[], |m| m.segments))?,
            quality: copy(input.quality)?,
            text,
            source_id_bytes: matches!(metadata.application_source_id, Some(SourceId::Bytes(_))),
            retained_bytes: 0,
        };
        // Account actual Vec capacities too, including any allocator overreservation.
        let mut bytes = core::mem::size_of::<Self>();
        let sizes = [
            result.spans.capacity() * core::mem::size_of::<WaveformSpan>(),
            result.columns.capacity() * core::mem::size_of::<WaveformColumn>(),
            result.tiles.capacity() * core::mem::size_of::<NativeTile>(),
            result.detail_columns.capacity() * core::mem::size_of::<WaveformColumn>(),
            result.tempo.capacity() * core::mem::size_of::<TempoCandidate>(),
            result.local.coverage.capacity() * core::mem::size_of::<FrameRange>(),
            result.local.segments.capacity() * core::mem::size_of::<GridSegment>(),
            result.local.beats.capacity() * core::mem::size_of::<Beat>(),
            result.global.coverage.capacity() * core::mem::size_of::<FrameRange>(),
            result.global.segments.capacity() * core::mem::size_of::<GridSegment>(),
            result.global.beats.capacity() * core::mem::size_of::<Beat>(),
            result.key.capacity() * core::mem::size_of::<KeyCandidate>(),
            result.meter.capacity() * core::mem::size_of::<MeterSegment>(),
            result.quality.capacity() * core::mem::size_of::<QualityRecord>(),
        ];
        for size in sizes
            .into_iter()
            .chain(result.text.iter().flatten().map(Vec::capacity))
        {
            bytes = bytes.checked_add(size).ok_or(Error::LimitExceeded)?;
        }
        if bytes > limits.maximum_storage_bytes {
            return Err(Error::LimitExceeded);
        }
        result.retained_bytes = bytes;
        Ok(result)
    }
    pub(crate) fn attach_context(&mut self, context: &crate::RuntimeContext) -> Result<(), Error> {
        if self.context_resource.is_some() {
            return Err(Error::InvalidState);
        }
        self.context_resource = Some(context.result(self.retained_bytes)?);
        Ok(())
    }
    pub fn retained_bytes(&self) -> usize {
        self.retained_bytes
    }
    pub fn source(&self) -> SourceInfo {
        self.header.source
    }
    pub fn info(&self) -> NativeResultInfo {
        self.header.info
    }
    pub fn available_features(&self) -> u64 {
        self.available
    }
    pub fn changed_features(&self) -> u64 {
        self.changed
    }
    fn text(&self, i: usize) -> Option<&str> {
        self.text[i]
            .as_deref()
            .map(|b| core::str::from_utf8(b).expect("copied validated UTF-8"))
    }
    pub fn view(&self) -> NativeResultInput<'_> {
        NativeResultInput {
            provenance: Provenance {
                source_name: self.text(6).unwrap(),
                source_version: self.text(7).unwrap(),
                ..self.header.provenance
            },
            metadata: self.header.metadata.map(|m| Metadata {
                producer_name: self.text(0),
                producer_version_string: self.text(1),
                backend_name: self.text(2),
                backend_version: self.text(3),
                comments: self.text(5),
                application_source_id: if self.source_id_bytes {
                    self.text[4].as_deref().map(SourceId::Bytes)
                } else {
                    self.text(4).map(SourceId::Text)
                },
                ..m
            }),
            overview: self.header.overview.map(|w| NativeOverview {
                spans: &self.spans,
                columns: &self.columns,
                ..w
            }),
            detail: self.header.detail.map(|_| NativeDetail {
                tiles: &self.tiles,
                columns: &self.detail_columns,
            }),
            tempo: self.header.tempo.map(|t| TempoView {
                candidates: &self.tempo,
                ..t
            }),
            local_grid: self.header.local_grid.map(|g| self.local.view(g)),
            global_grid: self.header.global_grid.map(|g| self.global.view(g)),
            key: self.header.key.map(|k| Key {
                candidates: &self.key,
                ..k
            }),
            meter: self.header.meter.map(|m| Meter {
                segments: &self.meter,
                ..m
            }),
            quality: &self.quality,
            ..self.header
        }
    }
}

/// Concurrent immutable heap graph acquisition. Retained readers own all graph
/// memory; neither caller arrays nor core leases survive refresh. One processing
/// writer is expected; conflicting publication fails atomically.
#[derive(Clone)]
pub struct HeapResults {
    latest: Arc<RwLock<Arc<HeapResult>>>,
}
impl HeapResults {
    pub fn new(initial: HeapResult) -> Self {
        Self {
            latest: Arc::new(RwLock::new(Arc::new(initial))),
        }
    }
    pub fn acquire(&self) -> Result<Arc<HeapResult>, Error> {
        self.latest
            .read()
            .map(|v| Arc::clone(&v))
            .map_err(|_| Error::Internal)
    }
    pub fn publish(&self, result: HeapResult) -> Result<(), Error> {
        let mut latest = self.latest.write().map_err(|_| Error::Internal)?;
        let mut source = result.source();
        if latest.source().total_frames.is_none() {
            source.total_frames = None;
        }
        if source != latest.source() || result.info().generation <= latest.info().generation {
            return Err(Error::Conflict);
        }
        *latest = Arc::new(result);
        Ok(())
    }
    fn refresh(&self, result: &OwnedResult<'_>, limits: NativeLimits) -> Result<bool, Error> {
        let current = self.acquire()?;
        let mut source = result.source();
        if current.source().total_frames.is_none() {
            source.total_frames = None;
        }
        if source != current.source() {
            return Err(Error::Conflict);
        }
        if current.info().generation == result.info().generation {
            return Ok(false);
        }
        drop(current);
        self.publish(HeapResult::copy_from(result, limits)?)?;
        Ok(true)
    }
    pub fn refresh_session(
        &self,
        session: &libapta::publication::PublishedSession<'_, '_, '_>,
        limits: NativeLimits,
    ) -> Result<bool, Error> {
        let result = session.acquire_result()?;
        self.refresh(&result, limits)
    }
    pub fn refresh_sparse(
        &self,
        session: &libapta::publication::PublishedSparseSession<'_, '_, '_>,
        limits: NativeLimits,
    ) -> Result<bool, Error> {
        let result = session.acquire_result()?;
        self.refresh(&result, limits)
    }
    pub fn process(
        &self,
        session: &mut libapta::publication::PublishedSession<'_, '_, '_>,
        budget: libapta::session::WorkBudget,
        cancel: &libapta::session::CancellationToken,
        limits: NativeLimits,
    ) -> Result<libapta::session::Progress, Error> {
        let work = session.process(budget, cancel);
        self.refresh_session(session, limits)?;
        work
    }
    pub fn process_sparse(
        &self,
        session: &mut libapta::publication::PublishedSparseSession<'_, '_, '_>,
        budget: libapta::session::WorkBudget,
        cancel: &libapta::session::CancellationToken,
        limits: NativeLimits,
    ) -> Result<libapta::session::Progress, Error> {
        let work = session.process(budget, cancel);
        self.refresh_sparse(session, limits)?;
        work
    }
}
