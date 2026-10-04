// SPDX-License-Identifier: Apache-2.0
//! Borrowed, trusted overview/musical graph from an actual sequential session.
//! Metadata and eager detail require their separate explicit copy interfaces.
use crate::{
    owned_result::{self, OwnedResult, Requirements, Storage},
    *,
};

/// Constructed only by Session. The view borrows working arrays, so processing
/// cannot run until it is released. Copying preserves trusted native exceptions;
/// passing `view()` to an external builder retains that builder's strict rules.
pub struct SessionSnapshot<'a> {
    pub(crate) header: NativeResultInput<'a>,
    pub(crate) span: [WaveformSpan; 1],
    pub(crate) local: Option<LocalGrid>,
    pub(crate) local_coverage: [FrameRange; 1],
    pub(crate) local_segment: Option<[GridSegment; 1]>,
    pub(crate) global: Option<GlobalGrid<'a>>,
    pub(crate) global_coverage: [FrameRange; 1],
    pub(crate) quality: Option<[QualityRecord; 1]>,
}
impl SessionSnapshot<'_> {
    pub fn view(&self) -> NativeResultInput<'_> {
        NativeResultInput {
            overview: self.header.overview.map(|w| NativeOverview {
                spans: &self.span,
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
        }
    }
    pub fn requirements(&self, limits: NativeLimits) -> Result<Requirements, Error> {
        crate::native_validation::validate_session(&self.view(), limits, 0)?;
        owned_result::counts(&self.view(), limits)
    }
    pub fn available_features(&self, limits: NativeLimits) -> Result<u64, Error> {
        Ok(crate::native_validation::validate_session(&self.view(), limits, 0)?.available_features)
    }
    /// Caller chooses its native generation/change identity. This does not claim
    /// the C wrapper's intermediate stage publication schedule.
    pub fn copy_to<'b>(
        &self,
        storage: Storage<'b>,
        limits: NativeLimits,
        changed: u64,
    ) -> Result<OwnedResult<'b>, Error> {
        owned_result::copy_session(&self.view(), storage, limits, changed)
    }
}
