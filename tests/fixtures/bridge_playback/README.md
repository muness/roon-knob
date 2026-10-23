These are synthetic controller HTTP responses, not captures from the tester.

The zone IDs, names, track and artwork key come from the Roon event excerpt attached to #291. The stopped MacBook zone has no now-playing or volume control; the playing Gustard also has no volume control. The HTTP shape and null/default behavior follow [`src/knobs/routes.rs` at UHC `v4.0.0-beta.7`](https://github.com/open-horizon-labs/unified-hifi-control/blob/v4.0.0-beta.7/src/knobs/routes.rs) (`ZoneInfo`, `NowPlayingResponse`, and `knob_now_playing_handler`). The bridge prefixes Roon IDs and projects media into top-level fields. Its zone summaries do not contain `now_playing`.

The fixtures test the empty-zone hypothesis without pretending raw Roon CONTINUE events are the firmware API. They cannot establish the cause of the tester's intermittent failure without failing HTTP/serial evidence and exact-artifact hardware validation.
