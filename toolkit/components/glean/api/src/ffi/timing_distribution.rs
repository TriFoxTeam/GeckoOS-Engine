// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#![cfg(feature = "with_gecko")]

use nsstring::nsACString;
use std::time::Duration;
use thin_vec::ThinVec;

#[no_mangle]
pub extern "C" fn fog_timing_distribution_start(id: u32) -> u64 {
    with_metric!(TIMING_DISTRIBUTION_MAP, id, metric, metric.start().id)
}

#[no_mangle]
pub extern "C" fn fog_timing_distribution_stop_and_accumulate(id: u32, timing_id: u64) {
    with_metric!(
        TIMING_DISTRIBUTION_MAP,
        id,
        metric,
        metric.stop_and_accumulate(timing_id.into())
    );
}

#[no_mangle]
pub extern "C" fn fog_timing_distribution_accumulate_raw_nanos(id: u32, sample: u64) {
    with_metric!(
        TIMING_DISTRIBUTION_MAP,
        id,
        metric,
        metric.accumulate_raw_duration(Duration::from_nanos(sample))
    );
}

#[no_mangle]
pub extern "C" fn fog_timing_distribution_cancel(id: u32, timing_id: u64) {
    with_metric!(
        TIMING_DISTRIBUTION_MAP,
        id,
        metric,
        metric.cancel(timing_id.into())
    );
}

#[no_mangle]
pub extern "C" fn fog_timing_distribution_accumulate_samples(id: u32, samples: &ThinVec<i64>) {
    // N.B.: Avoid reallocation here by making the underlying type take a slice.
    let samples = samples.to_vec();
    with_metric!(
        TIMING_DISTRIBUTION_MAP,
        id,
        metric,
        metric.accumulate_samples(samples)
    );
}

#[no_mangle]
pub extern "C" fn fog_timing_distribution_accumulate_single_sample(id: u32, sample: i64) {
    with_metric!(
        TIMING_DISTRIBUTION_MAP,
        id,
        metric,
        metric.accumulate_single_sample(sample)
    );
}

#[no_mangle]
pub extern "C" fn fog_timing_distribution_test_has_value(id: u32, ping_name: &nsACString) -> bool {
    with_metric!(
        TIMING_DISTRIBUTION_MAP,
        id,
        metric,
        test_has!(metric, ping_name)
    )
}

#[no_mangle]
pub extern "C" fn fog_timing_distribution_test_get_value(
    id: u32,
    ping_name: &nsACString,
    sum: &mut u64,
    count: &mut u64,
    buckets: &mut ThinVec<u64>,
    counts: &mut ThinVec<u64>,
) {
    let val = with_metric!(
        TIMING_DISTRIBUTION_MAP,
        id,
        metric,
        test_get!(metric, ping_name)
    );
    *sum = val.sum as _;
    *count = val.count as _;
    for (&bucket, &count) in val.values.iter() {
        buckets.push(bucket as _);
        counts.push(count as _);
    }
}

#[no_mangle]
pub extern "C" fn fog_timing_distribution_test_get_error(
    id: u32,

    error_str: &mut nsACString,
) -> bool {
    let err = with_metric!(
        TIMING_DISTRIBUTION_MAP,
        id,
        metric,
        test_get_errors!(metric)
    );
    err.map(|err_str| error_str.assign(&err_str)).is_some()
}
