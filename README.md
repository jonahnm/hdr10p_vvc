# hdr10p_vvc

Embed, extract and remove HDR10+ (SMPTE ST 2094-40) dynamic metadata and
Dolby Vision RPU metadata in VVC (Rec. ITU-T H.266 | ISO/IEC 23090-3)
bitstreams.

## Building

```
make
```

Requires a C11 compiler, no external dependencies.

## Usage

```
./hdr10p_vvc inject -i input.vvc -j metadata.json [-r rpu.bin] -o output.vvc
./hdr10p_vvc extract -i input.vvc -o metadata.json
./hdr10p_vvc remove  -i input.vvc -o cleaned.vvc
```

`-i` inputs must be raw VVC Annex B byte streams (the output of `vvencapp`,
or `ffmpeg -i in.mkv -map 0:v -c copy -bsf:v vvc_metadata -f vvc out.vvc`).

### inject

Interleaves metadata into every access unit. HDR10+ is written as a prefix
SEI NAL unit before the first slice of each access unit; Dolby Vision RPUs
are written as a NAL unit after the last NAL unit of each access unit.
Existing HDR10+ / RPU data in the stream is replaced.

The metadata is associated in **presentation (display) order**, like
`hdr10plus_tool inject` / `dovi_tool`: the picture order count (POC) of every
access unit is derived from the VVC picture headers (Rec. ITU-T H.266 clause
8.3.1) and the metadata for the frame being displayed is attached to the
access unit that decodes it, which matters for streams with B-frames. Use
`--no-reorder` to associate metadata in decode order instead.

Options:

* `-j metadata.json` — HDR10+ metadata in the `hdr10plus_tool` format.
* `-r rpu.bin` — Dolby Vision RPU binary (`dovi_tool extract` output).
* `--rpu-nal-type N` — VVC NAL unit type used to carry the RPU (default 27).

At least one of `-j` or `-r` is required; both can be combined.

#### HDR10+ metadata JSON

```json
{
  "SceneInfo": [
    {
      "BezierCurveData": {"Anchors": [...], "KneePointX": 3000, "KneePointY": 500},
      "LuminanceParameters": {
        "AverageRGB": 12000,
        "LuminanceDistributions": {
          "DistributionIndex": [1, 5, 10, 25, 50, 75, 90, 95, 99],
          "DistributionValues": [800, 1200, 1800, ...]
        },
        "MaxScl": [12500, 11300, 10400]
      },
      "NumberOfWindows": 1,
      "TargetedSystemDisplayMaximumLuminance": 1000
    }
  ]
}
```

One entry is required per frame. If there are fewer entries than frames the
last entry is repeated; extra entries are skipped.

#### Dolby Vision RPU

The RPU binary must be in the `dovi_tool extract` output format: a sequence
of RPU NAL units separated by `0x00000001` start codes, each RPU payload
starting with the `0x19` prefix byte. One RPU is required per frame; a
short file repeats the last RPU, extra entries are skipped.

### extract

Reads the HDR10+ metadata out of the stream and writes a JSON file in the
same `hdr10plus_tool`-compatible format.

### remove

Strips all HDR10+ SEI messages and Dolby Vision RPU NAL units from the stream.

## How it works

* **HDR10+ carriage in VVC.** Rec. ITU-T H.274 specifies the SEI messages for
  VVC. HDR10+ is carried in a *user data registered by Rec. ITU-T T.35* SEI
  message (payload type 4) carried in a prefix SEI NAL unit (type 23). The
  payload starts with the T.35 header: `country_code = 0xB5`,
  `terminal_provider_code = 0x003C`,
  `terminal_provider_oriented_code = 0x0001`, followed by the ST 2094-40
  metadata (application identifier 4, version 1). This is identical to how
  HDR10+ is carried in HEVC.
* **Dolby Vision RPU carriage in VVC.** The RPU is carried the same way
  dovi_tool carries it in HEVC (as a standalone NAL unit, one per access unit,
  placed after all other NAL units of the access unit). HEVC uses NAL type 62
  (UNSPEC62); VVC has no published RPU NAL type yet, so a *reserved non-VCL*
  NAL unit (type 27, RSV_NVCL_27) is used by default. Type 27 is not treated
  as a picture-unit boundary by FFmpeg's VVC parser, so the RPU stays grouped
  with its access unit. Override with `--rpu-nal-type` once a convention
  emerges.
* **Access unit detection.** A new access unit begins at a VCL NAL unit that is
  the first VCL NAL unit of the stream, is an IRAP/GDR NAL unit, is preceded by
  a non-VCL NAL unit (AUD, PH, SEI, parameter sets, EOS/EOB), or — when the
  stream contains no AUD or PH NAL units — is any VCL NAL unit (one slice per
  picture assumed, the VVC encoder default).
* The ST 2094-40 binary layout, the SEI message framing (0xFF-prefixed payload
  type/size) and emulation prevention byte insertion/removal follow the VVC/H.274
  specification and match the payloads produced by `hdr10plus_tool`.

## Limitations

* Only single-window HDR10+ metadata (`NumberOfWindows == 1`, profile A/B) is
  supported.
* Input and output are stored in memory; very large files need enough RAM.
* The POC-based reordering requires the SPS/PPS/picture headers to be
  parseable; if it fails, injection falls back to decode-order association
  with a warning.
* Dolby Vision RPU carriage in VVC is not standardized; the NAL type used is a
  convention that can be changed with `--rpu-nal-type`.
# hdr10p_vvc
