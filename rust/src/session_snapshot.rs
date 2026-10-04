// SPDX-License-Identifier: Apache-2.0
//! Trusted overview/detail/musical graph from an actual sequential session.
//! Detail uses fixed inline scratch; metadata retains its separate interfaces.
use crate::{
    owned_result::{self, OwnedResult, Requirements, Storage},
    *,
};

/// Constructed only by Session. The view borrows working arrays, so processing
/// cannot run until it is released. Copying preserves trusted native exceptions;
/// passing `view()` to an external builder retains that builder's strict rules.
pub struct SessionSnapshot<'a> {
    pub(crate) initially_unknown: bool,
    pub(crate) requested_features: Option<u64>,
    pub(crate) header: NativeResultInput<'a>,
    pub(crate) detail_tiles: [NativeTile; crate::detail_analysis::TILE_COUNT],
    pub(crate) detail_columns: [WaveformColumn;
        crate::detail_analysis::TILE_COUNT * crate::detail_analysis::COLUMNS_PER_TILE],
    pub(crate) detail_counts: Option<(usize, usize)>,
    pub(crate) span: Option<[WaveformSpan; 1]>,
    pub(crate) local: Option<LocalGrid>,
    pub(crate) local_coverage: [FrameRange; 1],
    pub(crate) local_segment: Option<[GridSegment; 1]>,
    pub(crate) global: Option<GlobalGrid<'a>>,
    pub(crate) global_coverage: [FrameRange; 1],
    pub(crate) quality: Option<[QualityRecord; 1]>,
}
impl SessionSnapshot<'_> {
    /// Project actual session data using the explicit C requested-capability
    /// rules, preserving trusted exceptions. This does not attach analysis stages.
    pub fn with_requested_features(mut self, requested: u64) -> Result<Self, Error> {
        crate::publication::validate_requested_features(requested)?;
        if requested & crate::result::WAVEFORM_OVERVIEW == 0 {
            return Err(Error::Unsupported);
        }
        self.requested_features = Some(requested);
        Ok(self)
    }
    pub fn view(&self) -> NativeResultInput<'_> {
        let mut input = NativeResultInput {
            detail: self.header.detail.map(|_| {
                let (tiles, columns) = self.detail_counts.unwrap();
                NativeDetail {
                    tiles: &self.detail_tiles[..tiles],
                    columns: &self.detail_columns[..columns],
                }
            }),
            overview: self.header.overview.map(|w| NativeOverview {
                spans: self.span.as_ref().map_or(w.spans, |s| s.as_slice()),
                ..w
            }),
            local_grid: self.local.map(|g| NativeGrid {
                state: g.state,
                confidence: g.confidence,
                flags: g.flags,
                representation: GridRepresentation::Segments,
                requested_range: g.requested_range,
                evidence_range: g.evidence_range,
                applicability_range: g.applicability_range,
                coverage_ranges: &self.local_coverage,
                segments: self.local_segment.as_ref().unwrap(),
                beats: &[],
            }),
            global_grid: self.global.map(|g| NativeGrid {
                state: g.state,
                confidence: g.confidence,
                flags: g.flags,
                representation: g.representation,
                requested_range: g.requested_range,
                evidence_range: g.evidence_range,
                applicability_range: g.applicability_range,
                coverage_ranges: &self.global_coverage,
                segments: g.segments,
                beats: g.beats,
            }),
            quality: self.quality.as_ref().map_or(&[], |q| q.as_slice()),
            ..self.header
        };
        if let Some(requested) = self.requested_features {
            crate::publication::project_session_payload(
                &mut input,
                requested,
                self.initially_unknown,
            );
        }
        input
    }
    pub fn requirements(&self, limits: NativeLimits) -> Result<Requirements, Error> {
        crate::native_validation::validate_session(&self.view(), limits, 0)?;
        owned_result::counts(&self.view(), limits)
    }
    pub fn available_features(&self, limits: NativeLimits) -> Result<u64, Error> {
        let available =
            crate::native_validation::validate_session(&self.view(), limits, 0)?.available_features;
        Ok(self.requested_features.map_or(available, |r| {
            crate::publication::session_capabilities(available, r)
        }))
    }
    /// Caller chooses its native generation/change identity. This does not claim
    /// the C wrapper's intermediate stage publication schedule.
    pub fn copy_to<'b>(
        &self,
        storage: Storage<'b>,
        limits: NativeLimits,
        changed: u64,
    ) -> Result<OwnedResult<'b>, Error> {
        let mut result = owned_result::copy_session(&self.view(), storage, limits, changed)?;
        if let Some(requested) = self.requested_features {
            result.apply_session_capabilities(requested);
        }
        Ok(result)
    }
}
