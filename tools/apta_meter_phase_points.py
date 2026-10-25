# SPDX-License-Identifier: Apache-2.0
"""MC-05 opt-in public geometry diagnostic. No references, imports or I/O."""

REGISTRATION_SHA256 = 'c9104badbd1125eabe6a4140a707abd6331653878ed8260728ab05fdd22a2f9c'
Q = 1 << 32
MAX_POINTS = 3072
MAX_FRAMES = 1440000
MAX_ORDINAL = (1 << 63) - 1
METER_KEYS = ('numerator', 'denominator', 'downbeat_frame',
              'downbeat_fraction_q32', 'downbeat_ordinal')


def _shape(value, keys):
    if type(value) is not dict or set(value) != set(keys):
        raise ValueError('invalid closed record')


def _integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError('invalid exact integer')


def _window(window, total_frames):
    _shape(window, ('first_frame', 'end_frame', 'model', 'points', 'meter'))
    first, end = window['first_frame'], window['end_frame']
    _integer(first, 0, total_frames - 1)
    _integer(end, first + 1, total_frames)
    points, model, meter = window['points'], window['model'], window['meter']
    if type(points) is not list or len(points) > MAX_POINTS:
        raise ValueError('invalid point capacity')
    if model is None:
        if points or meter is not None:
            raise ValueError('model absent with published evidence')
    else:
        _shape(model, ('anchor_frame', 'anchor_ordinal', 'tempo_millibpm',
                       'period_q32', 'confidence'))
        _integer(model['anchor_frame'], 0, total_frames - 1)
        _integer(model['anchor_ordinal'], 0, MAX_ORDINAL)
        _integer(model['tempo_millibpm'], 40000, 300000)
        _integer(model['confidence'], 0, 0)
        _integer(model['period_q32'], 1, MAX_FRAMES * Q)
        step = 48000 * 60000 * Q // model['tempo_millibpm']
        if model['period_q32'] != step:
            raise ValueError('period does not match P1 geometry')
        anchor = model['anchor_frame'] * Q
        start = max(0, -(-(first * Q - anchor) // step))
        stop = (end * Q - 1 - anchor) // step + 1
        if len(points) != max(0, stop - start):
            raise ValueError('owned grid is incomplete or exceeds capacity')
        for index, point in enumerate(points, start):
            _shape(point, ('position_q32', 'ordinal'))
            _integer(point['position_q32'], first * Q, end * Q - 1)
            _integer(point['ordinal'], 0, MAX_ORDINAL)
            if (point['position_q32'] != anchor + index * step
                    or point['ordinal'] != model['anchor_ordinal'] + index):
                raise ValueError('point does not match fixed grid')
    if meter is not None:
        _shape(meter, METER_KEYS)
        _integer(meter['numerator'], 3, 4)
        _integer(meter['denominator'], 4, 4)
        _integer(meter['downbeat_frame'], first, end - 1)
        _integer(meter['downbeat_fraction_q32'], 0, Q - 1)
        _integer(meter['downbeat_ordinal'], 0, MAX_ORDINAL)
        position = meter['downbeat_frame'] * Q + meter['downbeat_fraction_q32']
        if not any(p['position_q32'] == position and p['ordinal'] == meter['downbeat_ordinal']
                   for p in points):
            raise ValueError('meter is not an exact owned witness')
    return {'first_frame': first, 'end_frame': end,
            'model': dict(model) if model is not None else None,
            'grid_points': [dict(p) for p in points],
            'phase_points': [dict(p) for p in points if meter is not None
                             and (p['ordinal'] - meter['downbeat_ordinal']) % meter['numerator'] == 0]}


def extract_source(windows, *, total_frames, sample_rate, known_gaps):
    """Validate bounded independent windows and extract K without references.

    Full geometry binds local points, not upstream replay/source provenance.
    No returned record aliases an input or another returned point/model.
    """
    _integer(total_frames, 1, MAX_FRAMES)
    _integer(sample_rate, 48000, 48000)
    if type(known_gaps) is not list or known_gaps:
        raise ValueError('explicit gap-free provenance required')
    if type(windows) is not list or not 1 <= len(windows) <= 8:
        raise ValueError('invalid window capacity')
    result, previous_end, count = [], 0, 0
    for window in windows:
        record = _window(window, total_frames)
        if record['first_frame'] < previous_end:
            raise ValueError('windows overlap or are out of order')
        previous_end = record['end_frame']
        count += len(record['grid_points'])
        if count > MAX_POINTS:
            raise ValueError('source point capacity exceeded')
        result.append(record)
    return result
