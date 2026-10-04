// SPDX-License-Identifier: Apache-2.0
//! Native default-profile decimated Goertzel musical-key analysis.
use crate::*;
const FREQUENCIES: [f32; 36] = [
    130.8128, 138.5913, 146.8324, 155.5635, 164.8138, 174.6141, 184.9972, 195.9977, 207.6523,
    220.0000, 233.0819, 246.9417, 261.6256, 277.1826, 293.6648, 311.127, 329.6276, 349.2282,
    369.9944, 391.9954, 415.3047, 440.0000, 466.1638, 493.8833, 523.2511, 554.3653, 587.3295,
    622.254, 659.2551, 698.4565, 739.9888, 783.9909, 830.6094, 880.0000, 932.3275, 987.7666,
];
const MAJOR: [f32; 12] = [
    0.748, 0.060, 0.488, 0.082, 0.674, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400,
];
const MINOR: [f32; 12] = [
    0.712, 0.084, 0.455, 0.270, 0.360, 0.320, 0.082, 0.600, 0.059, 0.291, 0.092, 0.260,
];
/// Explicit numerical backend. Default uses portable libm. A std host can
/// select its platform f32 operations to compare against that platform's C libm.
/// Callbacks must be deterministic finite math functions on valid inputs, and
/// must return promptly. Backend identity stays fixed for the session lifetime.
#[derive(Clone, Copy)]
pub struct KeyMath {
    pub cos: fn(f32) -> f32,
    pub log: fn(f32) -> f32,
    pub sqrt: fn(f32) -> f32,
}
impl Default for KeyMath {
    fn default() -> Self {
        Self {
            cos: libm::cosf,
            log: libm::logf,
            sqrt: libm::sqrtf,
        }
    }
}
/// Fixed-size state; no source samples or dynamically allocated storage survive.
pub struct KeyAnalysis {
    math: KeyMath,
    coefficients: [f32; 36],
    q1: [f32; 36],
    q2: [f32; 36],
    chroma: [f32; 12],
    sum: f32,
    decimation: u32,
    samples: u32,
    target: u32,
    windows: u32,
    selected_windows: u32,
    next: Option<u64>,
    first: u64,
    end: u64,
    selected: Option<(u8, u8, u8, FeatureState, u64, u64)>,
    candidates: [KeyCandidate; 3],
    serial: u64,
}
impl KeyAnalysis {
    pub fn new(rate: u32) -> Result<Self, Error> {
        Self::new_with_math(rate, KeyMath::default())
    }
    pub fn new_with_math(rate: u32, math: KeyMath) -> Result<Self, Error> {
        if rate == 0 || rate > 768000 {
            return Err(Error::InvalidArgument);
        }
        let decimated = rate as f32 / 4.0;
        let mut coefficients = [0.0; 36];
        let target = if decimated > 2.0 * FREQUENCIES[35] {
            for (c, f) in coefficients.iter_mut().zip(FREQUENCIES) {
                *c = 2.0 * (math.cos)(core::f32::consts::TAU * f / decimated);
            }
            rate / 4
        } else {
            0
        };
        Ok(Self {
            math,
            coefficients,
            q1: [0.0; 36],
            q2: [0.0; 36],
            chroma: [0.0; 12],
            sum: 0.0,
            decimation: 0,
            samples: 0,
            target,
            windows: 0,
            selected_windows: 0,
            next: None,
            first: 0,
            end: 0,
            selected: None,
            candidates: [KeyCandidate::default(); 3],
            serial: 0,
        })
    }
    fn reset_window(&mut self) {
        self.q1 = [0.0; 36];
        self.q2 = [0.0; 36];
        self.sum = 0.0;
        self.decimation = 0;
        self.samples = 0;
    }
    pub(crate) fn push(&mut self, frame: u64, sample: f32) {
        if self.target == 0 {
            return;
        }
        if self.next.is_some_and(|next| next != frame) {
            self.reset_window();
        }
        if self.windows == 0 && self.samples == 0 && self.decimation == 0 {
            self.first = frame;
        }
        self.next = Some(frame + 1);
        self.end = frame + 1;
        self.sum += sample;
        self.decimation += 1;
        if self.decimation < 4 {
            return;
        }
        let sample = self.sum / 4.0;
        self.sum = 0.0;
        self.decimation = 0;
        for i in 0..36 {
            let q = sample + self.coefficients[i] * self.q1[i] - self.q2[i];
            self.q2[i] = self.q1[i];
            self.q1[i] = q;
        }
        self.samples += 1;
        if self.samples >= self.target {
            for i in 0..36 {
                let a = self.q1[i];
                let b = self.q2[i];
                let energy = a * a + b * b - self.coefficients[i] * a * b;
                let energy = if !energy.is_finite() || energy < 0.0 {
                    0.0
                } else {
                    energy
                };
                self.chroma[i % 12] += (self.math.log)(1.0 + energy);
            }
            self.windows += 1;
            self.reset_window();
        }
    }
    pub(crate) fn pending(&self, eof: bool) -> bool {
        self.windows != self.selected_windows
            && (self.selected_windows == 0 || self.windows >= self.selected_windows + 4 || eof)
    }
    pub fn key(&self) -> Option<Key<'_>> {
        self.selected.map(
            |(tonic, mode, confidence, state, first_frame, end_frame)| Key {
                state,
                confidence,
                tonic,
                mode,
                tuning_offset_cents: 0,
                first_frame,
                end_frame,
                candidates: &self.candidates,
            },
        )
    }
    pub fn mutation_serial(&self) -> u64 {
        self.serial
    }
    pub(crate) fn refresh(&mut self, steps: u32, eof: bool) -> Result<u32, Error> {
        if steps == 0 || !self.pending(eof) {
            return Ok(0);
        }
        self.selected_windows = self.windows;
        if self.windows == 0 || self.chroma.iter().sum::<f32>() <= 1e-12 {
            return Ok(1);
        }
        let mut scores = [-1.0; 3];
        let mut candidates = [KeyCandidate::default(); 3];
        for tonic in 0..12 {
            for (mode, profile) in [(1, &MAJOR), (2, &MINOR)] {
                let score = profile_score(&self.chroma, tonic, profile, self.math.sqrt);
                if let Some(position) = scores.iter().position(|s| score > *s) {
                    for i in (position + 1..3).rev() {
                        scores[i] = scores[i - 1];
                        candidates[i] = candidates[i - 1];
                    }
                    scores[position] = score;
                    candidates[position] = KeyCandidate {
                        tonic: tonic as u8,
                        mode,
                        tuning_offset_cents: 0,
                        score: (score * 65535.0 + 0.5).clamp(0.0, 65535.0) as u16,
                        confidence: 0,
                    };
                }
            }
        }
        for i in 1..3 {
            if candidates[i].score >= candidates[i - 1].score {
                candidates[i].score = candidates[i - 1].score.saturating_sub(1);
            }
        }
        let separation = (scores[0] - scores[1]).max(0.0);
        let confidence = (25
            + (self.windows * 5).min(40)
            + (separation * 350.0 + 0.5).min(35.0) as u32)
            .min(100) as u8;
        for (i, c) in candidates.iter_mut().enumerate() {
            c.confidence = confidence.saturating_sub(i as u8 * 10);
        }
        let state = if self.windows >= 4 {
            FeatureState::Stable
        } else {
            FeatureState::Provisional
        };
        let next = (
            candidates[0].tonic,
            candidates[0].mode,
            confidence,
            state,
            self.first,
            self.end,
        );
        let changed = self.selected.map_or(true, |old| {
            old.0 != next.0 || old.1 != next.1 || old.2 != next.2 || old.3 != next.3
        }) || self.candidates[0].score != candidates[0].score;
        self.selected = Some(next);
        self.candidates = candidates;
        if changed {
            self.serial = self.serial.checked_add(1).ok_or(Error::LimitExceeded)?;
        }
        Ok(1)
    }
}
fn profile_score(
    chroma: &[f32; 12],
    tonic: usize,
    profile: &[f32; 12],
    sqrt: fn(f32) -> f32,
) -> f32 {
    let mut dot = 0.0;
    let mut a = 0.0;
    let mut b = 0.0;
    for i in 0..12 {
        let p = profile[(i + 12 - tonic) % 12];
        dot += chroma[i] * p;
        a += chroma[i] * chroma[i];
        b += p * p;
    }
    if a <= 1e-20 || b <= 1e-20 {
        0.0
    } else {
        dot / sqrt(a * b)
    }
}

#[cfg(test)]
mod numerical_tests {
    use super::*;
    extern crate std;
    #[test]
    #[ignore = "requires APTA_C_KEY_MATH_ORACLE"]
    fn platform_key_front_end_arithmetic_matches_compiled_c() {
        let output =
            std::process::Command::new(std::env::var_os("APTA_C_KEY_MATH_ORACLE").unwrap())
                .output()
                .unwrap();
        assert!(output.status.success());
        let mut k = KeyAnalysis::new_with_math(
            8000,
            KeyMath {
                cos: f32::cos,
                log: f32::ln,
                sqrt: f32::sqrt,
            },
        )
        .unwrap();
        for i in 0..320000u64 {
            let phase = i % 4000;
            k.push(
                i,
                if phase < 64 {
                    (64 - phase) as f32 / 64.0 * 0.75
                } else {
                    0.0
                },
            );
        }
        let values: std::vec::Vec<_> = output.stdout[..192]
            .chunks_exact(4)
            .map(|b| f32::from_ne_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(values.len(), 48);
        assert_eq!(k.coefficients.as_slice(), &values[..36], "coefficients");
        assert_eq!(k.chroma.as_slice(), &values[36..], "chroma");
        k.refresh(1, true).unwrap();
        let c_scores: std::vec::Vec<_> = output.stdout[192..]
            .chunks_exact(2)
            .map(|b| u16::from_ne_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(
            k.candidates.map(|c| c.score).as_slice(),
            c_scores.as_slice()
        );
        if std::env::var_os("APTA_KEY_PORTABLE_AUDIT").is_some() {
            let mut portable = KeyAnalysis::new(8000).unwrap();
            for i in 0..320000u64 {
                let phase = i % 4000;
                portable.push(
                    i,
                    if phase < 64 {
                        (64 - phase) as f32 / 64.0 * 0.75
                    } else {
                        0.0
                    },
                );
            }
            portable.refresh(1, true).unwrap();
            std::println!("portable coefficient mismatches: {}, chroma mismatches: {}, portable scores: {:?}, C scores: {:?}", portable.coefficients.iter().zip(&values[..36]).filter(|(a,b)| a.to_bits()!=b.to_bits()).count(), portable.chroma.iter().zip(&values[36..]).filter(|(a,b)| a.to_bits()!=b.to_bits()).count(), portable.candidates.map(|c| c.score), c_scores);
        }
    }
}
