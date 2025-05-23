// Copyright (C) 2021, Cloudflare, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

use crate::events::EventData;
use crate::events::EventImportance;
use crate::events::EventType;
use crate::events::Eventable;
use crate::events::ExData;

/// A helper object specialized for streaming JSON-serialized qlog to a
/// [`Write`] trait.
///
/// The object is responsible for the `Qlog` object that contains the
/// provided `Trace`.
///
/// Serialization is progressively driven by method calls; once log streaming
/// is started, `event::Events` can be written using `add_event()`.
///
/// [`Write`]: https://doc.rust-lang.org/std/io/trait.Write.html
use super::*;

#[derive(PartialEq, Eq, Debug)]
pub enum StreamerState {
    Initial,
    Ready,
    Finished,
}

pub struct QlogStreamer {
    start_time: std::time::Instant,
    writer: Box<dyn std::io::Write + Send + Sync>,
    qlog: QlogSeq,
    state: StreamerState,
    log_level: EventImportance,
}

impl QlogStreamer {
    /// Creates a [QlogStreamer] object.
    ///
    /// It owns a [QlogSeq] object that contains the provided [TraceSeq]
    /// containing [Event]s.
    ///
    /// All serialization will be written to the provided [`Write`] using the
    /// JSON-SEQ format.
    ///
    /// [`Write`]: https://doc.rust-lang.org/std/io/trait.Write.html
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        qlog_version: String, title: Option<String>, description: Option<String>,
        summary: Option<String>, start_time: std::time::Instant, trace: TraceSeq,
        log_level: EventImportance,
        writer: Box<dyn std::io::Write + Send + Sync>,
    ) -> Self {
        let qlog = QlogSeq {
            qlog_version,
            qlog_format: "JSON-SEQ".to_string(),
            title,
            description,
            summary,
            trace,
        };

        QlogStreamer {
            start_time,
            writer,
            qlog,
            state: StreamerState::Initial,
            log_level,
        }
    }

    /// Starts qlog streaming serialization.
    ///
    /// This writes out the JSON-SEQ-serialized form of all initial qlog
    /// information. [Event]s are separately appended using [add_event()],
    /// [add_event_with_instant()], [add_event_now()],
    /// [add_event_data_with_instant()], or [add_event_data_now()].
    ///
    /// [add_event()]: #method.add_event
    /// [add_event_with_instant()]: #method.add_event_with_instant
    /// [add_event_now()]: #method.add_event_now
    /// [add_event_data_with_instant()]: #method.add_event_data_with_instant
    /// [add_event_data_now()]: #method.add_event_data_now
    pub fn start_log(&mut self) -> Result<()> {
        if self.state != StreamerState::Initial {
            return Err(Error::Done);
        }

        self.writer.as_mut().write_all(b"")?;
        serde_json::to_writer(self.writer.as_mut(), &self.qlog)
            .map_err(|_| Error::Done)?;
        self.writer.as_mut().write_all(b"\n")?;

        self.state = StreamerState::Ready;

        Ok(())
    }

    /// Finishes qlog streaming serialization.
    ///
    /// After this is called, no more serialization will occur.
    pub fn finish_log(&mut self) -> Result<()> {
        if self.state == StreamerState::Initial ||
            self.state == StreamerState::Finished
        {
            return Err(Error::InvalidState);
        }

        self.state = StreamerState::Finished;

        self.writer.as_mut().flush()?;

        Ok(())
    }

    /// Writes a serializable to a JSON-SEQ record using
    /// [std::time::Instant::now()].
    pub fn add_event_now<E: Serialize + Eventable>(
        &mut self, event: E,
    ) -> Result<()> {
        let now = std::time::Instant::now();

        self.add_event_with_instant(event, now)
    }

    /// Writes a serializable to a pretty-printed JSON-SEQ record using
    /// [std::time::Instant::now()].
    pub fn add_event_now_pretty<E: Serialize + Eventable>(
        &mut self, event: E,
    ) -> Result<()> {
        let now = std::time::Instant::now();

        self.add_event_with_instant_pretty(event, now)
    }

    /// Writes a serializable to a JSON-SEQ record using the provided
    /// [std::time::Instant].
    pub fn add_event_with_instant<E: Serialize + Eventable>(
        &mut self, event: E, now: std::time::Instant,
    ) -> Result<()> {
        self.event_with_instant(event, now, false)
    }

    /// Writes a serializable to a pretty-printed JSON-SEQ record using the
    /// provided [std::time::Instant].
    pub fn add_event_with_instant_pretty<E: Serialize + Eventable>(
        &mut self, event: E, now: std::time::Instant,
    ) -> Result<()> {
        self.event_with_instant(event, now, true)
    }

    fn event_with_instant<E: Serialize + Eventable>(
        &mut self, mut event: E, now: std::time::Instant, pretty: bool,
    ) -> Result<()> {
        if self.state != StreamerState::Ready {
            return Err(Error::InvalidState);
        }

        if !event.importance().is_contained_in(&self.log_level) {
            return Err(Error::Done);
        }

        let dur = if cfg!(test) {
            std::time::Duration::from_secs(0)
        } else {
            now.duration_since(self.start_time)
        };

        let rel_time = dur.as_secs_f32() * 1000.0;
        event.set_time(rel_time);

        if pretty {
            self.add_event_pretty(event)
        } else {
            self.add_event(event)
        }
    }

    /// Writes an [Event] based on the provided [EventData] to a JSON-SEQ record
    /// at time [std::time::Instant::now()].
    pub fn add_event_data_now(&mut self, event_data: EventData) -> Result<()> {
        self.add_event_data_ex_now(event_data, Default::default())
    }

    /// Writes an [Event] based on the provided [EventData] to a pretty-printed
    /// JSON-SEQ record at time [std::time::Instant::now()].
    pub fn add_event_data_now_pretty(
        &mut self, event_data: EventData,
    ) -> Result<()> {
        self.add_event_data_ex_now_pretty(event_data, Default::default())
    }

    /// Writes an [Event] based on the provided [EventData] and [ExData] to a
    /// JSON-SEQ record at time [std::time::Instant::now()].
    pub fn add_event_data_ex_now(
        &mut self, event_data: EventData, ex_data: ExData,
    ) -> Result<()> {
        let now = std::time::Instant::now();

        self.add_event_data_ex_with_instant(event_data, ex_data, now)
    }

    /// Writes an [Event] based on the provided [EventData] and [ExData] to a
    /// pretty-printed JSON-SEQ record at time [std::time::Instant::now()].
    pub fn add_event_data_ex_now_pretty(
        &mut self, event_data: EventData, ex_data: ExData,
    ) -> Result<()> {
        let now = std::time::Instant::now();

        self.add_event_data_ex_with_instant_pretty(event_data, ex_data, now)
    }

    /// Writes an [Event] based on the provided [EventData] and
    /// [std::time::Instant] to a JSON-SEQ record.
    pub fn add_event_data_with_instant(
        &mut self, event_data: EventData, now: std::time::Instant,
    ) -> Result<()> {
        self.add_event_data_ex_with_instant(event_data, Default::default(), now)
    }

    /// Writes an [Event] based on the provided [EventData] and
    /// [std::time::Instant] to a pretty-printed JSON-SEQ record.
    pub fn add_event_data_with_instant_pretty(
        &mut self, event_data: EventData, now: std::time::Instant,
    ) -> Result<()> {
        self.add_event_data_ex_with_instant_pretty(
            event_data,
            Default::default(),
            now,
        )
    }

    /// Writes an [Event] based on the provided [EventData], [ExData], and
    /// [std::time::Instant] to a JSON-SEQ record.
    pub fn add_event_data_ex_with_instant(
        &mut self, event_data: EventData, ex_data: ExData,
        now: std::time::Instant,
    ) -> Result<()> {
        self.event_data_ex_with_instant(event_data, ex_data, now, false)
    }

    // Writes an [Event] based on the provided [EventData], [ExData], and
    /// [std::time::Instant] to a pretty-printed JSON-SEQ record.
    pub fn add_event_data_ex_with_instant_pretty(
        &mut self, event_data: EventData, ex_data: ExData,
        now: std::time::Instant,
    ) -> Result<()> {
        self.event_data_ex_with_instant(event_data, ex_data, now, true)
    }

    fn event_data_ex_with_instant(
        &mut self, event_data: EventData, ex_data: ExData,
        now: std::time::Instant, pretty: bool,
    ) -> Result<()> {
        if self.state != StreamerState::Ready {
            return Err(Error::InvalidState);
        }

        let ty = EventType::from(&event_data);
        if !EventImportance::from(ty).is_contained_in(&self.log_level) {
            return Err(Error::Done);
        }

        let dur = if cfg!(test) {
            std::time::Duration::from_secs(0)
        } else {
            now.duration_since(self.start_time)
        };

        let rel_time = dur.as_secs_f32() * 1000.0;
        let event = Event::with_time_ex(rel_time, event_data, ex_data);

        if pretty {
            self.add_event_pretty(event)
        } else {
            self.add_event(event)
        }
    }

    /// Writes a JSON-SEQ-serialized [Event] using the provided [Event].
    pub fn add_event<E: Serialize + Eventable>(
        &mut self, event: E,
    ) -> Result<()> {
        self.write_event(event, false)
    }

    /// Writes a pretty-printed JSON-SEQ-serialized [Event] using the provided
    /// [Event].
    pub fn add_event_pretty<E: Serialize + Eventable>(
        &mut self, event: E,
    ) -> Result<()> {
        self.write_event(event, true)
    }

    /// Writes a JSON-SEQ-serialized [Event] using the provided [Event].
    fn write_event<E: Serialize + Eventable>(
        &mut self, event: E, pretty: bool,
    ) -> Result<()> {
        if self.state != StreamerState::Ready {
            return Err(Error::InvalidState);
        }

        if !event.importance().is_contained_in(&self.log_level) {
            return Err(Error::Done);
        }

        self.writer.as_mut().write_all(b"")?;
        if pretty {
            serde_json::to_writer_pretty(self.writer.as_mut(), &event)
                .map_err(|_| Error::Done)?;
        } else {
            serde_json::to_writer(self.writer.as_mut(), &event)
                .map_err(|_| Error::Done)?;
        }
        self.writer.as_mut().write_all(b"\n")?;

        Ok(())
    }

    /// Returns the writer.
    #[allow(clippy::borrowed_box)]
    pub fn writer(&self) -> &Box<dyn std::io::Write + Send + Sync> {
        &self.writer
    }

    pub fn start_time(&self) -> std::time::Instant {
        self.start_time
    }
}

impl Drop for QlogStreamer {
    fn drop(&mut self) {
        let _ = self.finish_log();
    }
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeMap;

    use super::*;
    use crate::events::quic;
    use crate::events::quic::QuicFrame;
    use crate::events::RawInfo;
    use smallvec::smallvec;
    use testing::*;

    use serde_json::json;

    #[test]
    fn serialization_states() {
        let v: Vec<u8> = Vec::new();
        let buff = std::io::Cursor::new(v);
        let writer = Box::new(buff);

        let trace = make_trace_seq();
        let pkt_hdr = make_pkt_hdr(quic::PacketType::Handshake);
        let raw = Some(RawInfo {
            length: Some(1251),
            payload_length: Some(1224),
            data: None,
        });

        let frame1 = QuicFrame::Stream {
            stream_id: 40,
            offset: 40,
            length: 400,
            fin: Some(true),
            raw: None,
        };

        let event_data1 = EventData::PacketSent(quic::PacketSent {
            header: pkt_hdr.clone(),
            frames: Some(smallvec![frame1]),
            raw: raw.clone(),
            ..Default::default()
        });

        let ev1 = Event::with_time(0.0, event_data1);

        let frame2 = QuicFrame::Stream {
            stream_id: 0,
            offset: 0,
            length: 100,
            fin: Some(true),
            raw: None,
        };

        let frame3 = QuicFrame::Stream {
            stream_id: 0,
            offset: 0,
            length: 100,
            fin: Some(true),
            raw: None,
        };

        let event_data2 = EventData::PacketSent(quic::PacketSent {
            header: pkt_hdr.clone(),
            frames: Some(smallvec![frame2]),
            raw: raw.clone(),
            ..Default::default()
        });

        let ev2 = Event::with_time(0.0, event_data2);

        let event_data3 = EventData::PacketSent(quic::PacketSent {
            header: pkt_hdr,
            frames: Some(smallvec![frame3]),
            stateless_reset_token: Some("reset_token".to_string()),
            raw,
            ..Default::default()
        });

        let ev3 = Event::with_time(0.0, event_data3);

        let mut s = streamer::QlogStreamer::new(
            "version".to_string(),
            Some("title".to_string()),
            Some("description".to_string()),
            None,
            std::time::Instant::now(),
            trace,
            EventImportance::Base,
            writer,
        );

        // Before the log is started all other operations should fail.
        assert!(matches!(s.add_event(ev2.clone()), Err(Error::InvalidState)));
        assert!(matches!(s.finish_log(), Err(Error::InvalidState)));

        // Start log and add a simple event.
        assert!(matches!(s.start_log(), Ok(())));
        assert!(matches!(s.add_event(ev1), Ok(())));

        // Add some more events.
        assert!(matches!(s.add_event(ev2), Ok(())));
        assert!(matches!(s.add_event(ev3.clone()), Ok(())));

        // Adding an event with an external time should work too.
        // For tests, it will resolve to 0 but we care about proving the API
        // here, not timing specifics.
        let now = std::time::Instant::now();

        assert!(matches!(s.add_event_with_instant(ev3, now), Ok(())));

        assert!(matches!(s.finish_log(), Ok(())));

        let r = s.writer();
        #[allow(clippy::borrowed_box)]
        let w: &Box<std::io::Cursor<Vec<u8>>> = unsafe { std::mem::transmute(r) };

        let log_string = r#"{"qlog_version":"version","qlog_format":"JSON-SEQ","title":"title","description":"description","trace":{"vantage_point":{"type":"server"},"title":"Quiche qlog trace","description":"Quiche qlog trace description","configuration":{"time_offset":0.0}}}
{"time":0.0,"name":"transport:packet_sent","data":{"header":{"packet_type":"handshake","packet_number":0,"version":"1","scil":8,"dcil":8,"scid":"7e37e4dcc6682da8","dcid":"36ce104eee50101c"},"raw":{"length":1251,"payload_length":1224},"frames":[{"frame_type":"stream","stream_id":40,"offset":40,"length":400,"fin":true}]}}
{"time":0.0,"name":"transport:packet_sent","data":{"header":{"packet_type":"handshake","packet_number":0,"version":"1","scil":8,"dcil":8,"scid":"7e37e4dcc6682da8","dcid":"36ce104eee50101c"},"raw":{"length":1251,"payload_length":1224},"frames":[{"frame_type":"stream","stream_id":0,"offset":0,"length":100,"fin":true}]}}
{"time":0.0,"name":"transport:packet_sent","data":{"header":{"packet_type":"handshake","packet_number":0,"version":"1","scil":8,"dcil":8,"scid":"7e37e4dcc6682da8","dcid":"36ce104eee50101c"},"stateless_reset_token":"reset_token","raw":{"length":1251,"payload_length":1224},"frames":[{"frame_type":"stream","stream_id":0,"offset":0,"length":100,"fin":true}]}}
{"time":0.0,"name":"transport:packet_sent","data":{"header":{"packet_type":"handshake","packet_number":0,"version":"1","scil":8,"dcil":8,"scid":"7e37e4dcc6682da8","dcid":"36ce104eee50101c"},"stateless_reset_token":"reset_token","raw":{"length":1251,"payload_length":1224},"frames":[{"frame_type":"stream","stream_id":0,"offset":0,"length":100,"fin":true}]}}
"#;

        let written_string = std::str::from_utf8(w.as_ref().get_ref()).unwrap();

        assert_eq!(log_string, written_string);
    }

    #[test]
    fn stream_json_event() {
        let data = json!({"foo": "Bar", "hello": 123});
        let ev = events::JsonEvent {
            time: 0.0,
            importance: events::EventImportance::Core,
            name: "jsonevent:sample".into(),
            data,
        };

        let v: Vec<u8> = Vec::new();
        let buff = std::io::Cursor::new(v);
        let writer = Box::new(buff);

        let trace = make_trace_seq();

        let mut s = streamer::QlogStreamer::new(
            "version".to_string(),
            Some("title".to_string()),
            Some("description".to_string()),
            None,
            std::time::Instant::now(),
            trace,
            EventImportance::Base,
            writer,
        );

        assert!(matches!(s.start_log(), Ok(())));
        assert!(matches!(s.add_event(ev), Ok(())));
        assert!(matches!(s.finish_log(), Ok(())));

        let r = s.writer();
        #[allow(clippy::borrowed_box)]
        let w: &Box<std::io::Cursor<Vec<u8>>> = unsafe { std::mem::transmute(r) };

        let log_string = r#"{"qlog_version":"version","qlog_format":"JSON-SEQ","title":"title","description":"description","trace":{"vantage_point":{"type":"server"},"title":"Quiche qlog trace","description":"Quiche qlog trace description","configuration":{"time_offset":0.0}}}
{"time":0.0,"name":"jsonevent:sample","data":{"foo":"Bar","hello":123}}
"#;

        let written_string = std::str::from_utf8(w.as_ref().get_ref()).unwrap();

        assert_eq!(log_string, written_string);
    }

    #[test]
    fn stream_data_ex() {
        let v: Vec<u8> = Vec::new();
        let buff = std::io::Cursor::new(v);
        let writer = Box::new(buff);

        let trace = make_trace_seq();
        let pkt_hdr = make_pkt_hdr(quic::PacketType::Handshake);
        let raw = Some(RawInfo {
            length: Some(1251),
            payload_length: Some(1224),
            data: None,
        });

        let frame1 = QuicFrame::Stream {
            stream_id: 40,
            offset: 40,
            length: 400,
            fin: Some(true),
            raw: None,
        };

        let event_data1 = EventData::PacketSent(quic::PacketSent {
            header: pkt_hdr.clone(),
            frames: Some(smallvec![frame1]),
            raw: raw.clone(),
            ..Default::default()
        });
        let j1 = json!({"foo": "Bar", "hello": 123});
        let j2 = json!({"baz": [1,2,3,4]});
        let mut ex_data = BTreeMap::new();
        ex_data.insert("first".to_string(), j1);
        ex_data.insert("second".to_string(), j2);

        let ev1 = Event::with_time_ex(0.0, event_data1, ex_data);

        let frame2 = QuicFrame::Stream {
            stream_id: 1,
            offset: 0,
            length: 100,
            fin: Some(true),
            raw: None,
        };

        let event_data2 = EventData::PacketSent(quic::PacketSent {
            header: pkt_hdr.clone(),
            frames: Some(smallvec![frame2]),
            raw: raw.clone(),
            ..Default::default()
        });

        let ev2 = Event::with_time(0.0, event_data2);

        let mut s = streamer::QlogStreamer::new(
            "version".to_string(),
            Some("title".to_string()),
            Some("description".to_string()),
            None,
            std::time::Instant::now(),
            trace,
            EventImportance::Base,
            writer,
        );

        assert!(matches!(s.start_log(), Ok(())));
        assert!(matches!(s.add_event(ev1), Ok(())));
        assert!(matches!(s.add_event(ev2), Ok(())));
        assert!(matches!(s.finish_log(), Ok(())));

        let r = s.writer();
        #[allow(clippy::borrowed_box)]
        let w: &Box<std::io::Cursor<Vec<u8>>> = unsafe { std::mem::transmute(r) };

        let log_string = r#"{"qlog_version":"version","qlog_format":"JSON-SEQ","title":"title","description":"description","trace":{"vantage_point":{"type":"server"},"title":"Quiche qlog trace","description":"Quiche qlog trace description","configuration":{"time_offset":0.0}}}
{"time":0.0,"name":"transport:packet_sent","data":{"header":{"packet_type":"handshake","packet_number":0,"version":"1","scil":8,"dcil":8,"scid":"7e37e4dcc6682da8","dcid":"36ce104eee50101c"},"raw":{"length":1251,"payload_length":1224},"frames":[{"frame_type":"stream","stream_id":40,"offset":40,"length":400,"fin":true}]},"first":{"foo":"Bar","hello":123},"second":{"baz":[1,2,3,4]}}
{"time":0.0,"name":"transport:packet_sent","data":{"header":{"packet_type":"handshake","packet_number":0,"version":"1","scil":8,"dcil":8,"scid":"7e37e4dcc6682da8","dcid":"36ce104eee50101c"},"raw":{"length":1251,"payload_length":1224},"frames":[{"frame_type":"stream","stream_id":1,"offset":0,"length":100,"fin":true}]}}
"#;

        let written_string = std::str::from_utf8(w.as_ref().get_ref()).unwrap();

        assert_eq!(log_string, written_string);
    }
}
