#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Classify the frozen DD-05 error tail; no new candidate or scoring rule."""
import argparse
from collections import Counter
from fractions import Fraction
import json
from pathlib import Path

import apta_1_1_dense_decoder_diagnosis as audit

PRIOR = audit.ROOT/"docs/rust/DENSE-DECODER-DIAGNOSIS-20261023.json"
PRIOR_SHA256 = "901faeca3290ae9ef7a688960542119d126180eb89caf6d798a47830382816f8"
FAMILIES = ((1,3),(1,2),(2,3),(3,2),(2,1),(3,1))


def family(delta, target):
    """Relative 1% band around each fixed rational interval/target ratio."""
    if delta <= 0 or target <= 0:
        raise ValueError("positive interval/target required")
    ratio = Fraction(delta)/Fraction(target)
    for numerator, denominator in FAMILIES:
        value = Fraction(numerator, denominator)
        if abs(ratio-value) <= value/100:
            return str(numerator)+"_over_"+str(denominator)
    return "off_family"


def tail_point(frame, next_frame, row_step, beats):
    nearest = min(range(len(beats)), key=lambda j: abs(beats[j]-frame))
    local = audit.representation.ref.annotated.canonical._local_period(beats,beats[nearest])
    delta = next_frame-frame
    error = abs(delta-local)/local
    result = Counter(known_intervals=1, period_errors=int(abs(delta-local)>local*.01))
    if error <= .05:
        return result
    result["large_tail_intervals"] = 1
    ratio_name = family(delta,local)
    result["family_"+ratio_name] = 1
    result["row_term_also_exceeds_five_percent"] = int(abs(row_step*960-local)>local*.05)
    result["target_below_9600"] = int(local<9600)
    result["target_above_72000"] = int(local>72000)
    if not beats[0] <= next_frame <= beats[-1]:
        result["next_outside_reference"] = 1
        return result
    right = min(range(len(beats)),key=lambda j:abs(beats[j]-next_frame))
    right_local = audit.representation.ref.annotated.canonical._local_period(beats,beats[right])
    left_ok = abs(frame-beats[nearest]) <= local*.10
    right_ok = abs(next_frame-beats[right]) <= right_local*.10
    both = left_ok and right_ok
    step = right-nearest
    if step < 0:
        raise ValueError("nearest reference index must be monotone")
    result["both_endpoints_phase_correct"] = int(both)
    result["reference_step_"+(str(step) if step<3 else "ge3")] = 1
    result["phase_supported_skipped_reference_events"] = int(both and step>=2)
    result["phase_supported_adjacent_reference_events"] = int(both and step==1)
    result["family_"+ratio_name+("_both_phase_correct" if both else "_phase_not_both_correct")] = 1
    return result


class TailAudit:
    def __init__(self):
        self.groups = {k:Counter() for k in ("all", "actual_joint_break", "nominal_correct_actual_failed")}
        self.windows = {k:Counter() for k in ("actual_joint_break", "nominal_correct_actual_failed")}
        self.private = []

    def observe(self, track, candidate, saved, truth):
        frames, rows = candidate["events"]["beat_frames"], candidate["events"]["beat_rows"]
        points = {}
        for index in range(len(frames)-1):
            if truth["beat_frames"][0] <= frames[index] <= truth["beat_frames"][-1]:
                points[index] = tail_point(frames[index], frames[index+1], rows[index+1]-rows[index],truth["beat_frames"])
                self.groups["all"].update(points[index])
        details = []
        for index, window in enumerate(candidate["windows"]):
            groups = []
            if saved["ols_actual"][index]["joint_windows"] and not saved["candidate_actual"][index]["joint_windows"]:
                groups.append("actual_joint_break")
            if saved["candidate_nominal"][index]["joint_windows"] and not saved["candidate_actual"][index]["joint_windows"]:
                groups.append("nominal_correct_actual_failed")
            counts = Counter()
            for ordinal in window["beat_ordinals"]:
                counts.update(points.get(ordinal,{}))
            for group in groups:
                self.groups[group].update(counts)
                self.windows[group].update(windows=1,
                    with_large_tail=int(counts["large_tail_intervals"]>0),
                    period_errors_all_le_five_percent=int(counts["period_errors"]>0 and counts["large_tail_intervals"]==0),
                    with_half_ratio=int(counts["family_1_over_2"]>0),
                    with_double_ratio=int(counts["family_2_over_1"]>0))
            details.append({"window":index,"groups":groups,"counts":dict(counts)})
        self.private.append({"track":track,"windows":details})

    def result(self):
        return {"groups":{k:dict(v) for k,v in self.groups.items()},
                "window_groups":{k:dict(v) for k,v in self.windows.items()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("candidates","evaluation","prepared","output"):
        parser.add_argument("--"+name,required=True,type=Path)
    args = parser.parse_args()
    try:
        prior = audit.io.read_bound(PRIOR,PRIOR_SHA256,{})
        observer = TailAudit()
        base = audit.diagnose(args.candidates,args.evaluation,args.prepared,args.output,observer.observe)
        current = observer.groups["all"]
        if (current["known_intervals"] != prior["interval_counts"]["known_intervals"]
                or current["large_tail_intervals"] != prior["interval_counts"]["gt_5pct"]
                or current["period_errors"] != prior["interval_counts"]["actual_period_failures"]
                or observer.windows["actual_joint_break"]["windows"] != 95
                or observer.windows["nominal_correct_actual_failed"]["windows"] != 181):
            raise ValueError("tail scope differs from prior frozen audit")
        audit.io.bind(PRIOR,PRIOR_SHA256,{})
        value = {"format":"libapta-dense-decoder-tail-audit-1", "source_revision":base["source_revision"],
                 "original_measurement_revision":base["original_measurement_revision"],
                 "prior_diagnosis_sha256":PRIOR_SHA256,
                 "replayed_audit_sha256":audit.io.sha(args.output/"aggregate.json"),
                 "sources":100,"windows":600,"scorer_replay_exact":True,
                 "classification":{"large_error_exclusive_percent":5,"family_relative_tolerance_percent":1,
                     "interval_over_reference_period_families":[list(pair) for pair in FAMILIES],
                     "endpoint_phase_tolerance_percent":10},
                 **observer.result(), "original_verdict":"REJECTED", "candidate_redecoded":False,
                 "alternative_candidate_scored":False, "new_inference":False,
                 "posthoc_on_observed_development_labels":True}
        audit.io.write_new(args.output/"tail-details.private.json",{"sources":observer.private})
        audit.io.write_new(args.output/"tail-aggregate.json",value)
        print(json.dumps(value,sort_keys=True))
        return 0
    except (ValueError,KeyError,TypeError,OSError) as exc:
        import sys
        print(json.dumps({"status":"INVALID","error":str(exc)}),file=sys.stderr)
        return 2


if __name__ == "__main__":
    import sys
    sys.exit(main())
