Standalone fixed-point WMAv2 decoder from xOpenLee/wmaDecode, pinned to
e54cde0c9ba59f4752acf8643cccb1859328d3a3 (Rockbox/FFmpeg lineage).

Source: https://github.com/xOpenLee/wmaDecode/tree/e54cde0c9ba59f4752acf8643cccb1859328d3a3
License: GNU GPL, upstream notices preserved; see COPYING.

Web integration removes the standalone file reader/main, disables debug printing,
uses standard integer types, bounds bit reads/table indexes, tracks decoder allocations,
and returns decode failures to the existing PCM voice driver. The wrapper only accepts
bounded raw WMAv2 packets, with XAudio's synthesized flags (31). No ASF demuxer is linked.
