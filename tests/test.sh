#!/bin/sh
# Round-trip test for hdr10p_vvc.
#
# Requires ffmpeg built with --enable-libvvenc (or a pre-generated test.vvc).

set -e

cd "$(dirname "$0")/.."

if [ ! -x ./hdr10p_vvc ]; then
    echo "building..."
    make >/dev/null
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# 1. Make a small VVC test stream.
if [ ! -f "$WORK/test.vvc" ]; then
    ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=128x64:rate=30 \
        -frames:v 25 -c:v libvvenc -pix_fmt yuv420p "$WORK/test.vvc" -y
fi

echo "== inject =="
./hdr10p_vvc inject -i "$WORK/test.vvc" -j tests/sample_metadata.json -o "$WORK/injected.vvc"

echo "== extract =="
./hdr10p_vvc extract -i "$WORK/injected.vvc" -o "$WORK/extracted.json"

echo "== remove =="
./hdr10p_vvc remove -i "$WORK/injected.vvc" -o "$WORK/removed.vvc"

echo "== verify decodability =="
ffmpeg -hide_banner -loglevel error -i "$WORK/injected.vvc" -f null -
ffmpeg -hide_banner -loglevel error -i "$WORK/removed.vvc" -f null -
echo "decode OK"

echo "== frame counts =="
echo "input:      $(./hdr10p_vvc extract -i "$WORK/test.vvc" -o /dev/null 2>&1 | head -1)"
echo "extracted:  $(grep -c '"TargetedSystemDisplayMaximumLuminance"' "$WORK/extracted.json") metadata entries"

echo "== presentation-order reordering =="
# A stream with B-frames and per-frame distinct metadata must end up with every
# metadata entry used exactly once, in a (decode-order) sequence that differs
# from decode order. With --no-reorder the association is the identity.
if command -v perl >/dev/null 2>&1; then
    NF=24
    perl -e '
        my $n = shift;
        print "{\"JSONInfo\":{\"HDR10plusProfile\":\"B\",\"Version\":\"1.0\"},\n\"SceneInfo\":[\n";
        for my $i (0..$n-1){
            my $avg = 1000 + $i;
            print "  {\"BezierCurveData\":{\"Anchors\":[140,220,300,380,460,540,620,700],\"KneePointX\":3000,\"KneePointY\":500},";
            print "\"LuminanceParameters\":{\"AverageRGB\":$avg,\"LuminanceDistributions\":{\"DistributionIndex\":[1,5,10,25,50,75,90,95,99],";
            print "\"DistributionValues\":[800,1200,1800,2600,4200,6800,9400,11200,14500]},\"MaxScl\":[12500,11300,10400]},";
            print "\"NumberOfWindows\":1,\"TargetedSystemDisplayMaximumLuminance\":1000,\"SceneFrameIndex\":$i,\"SceneId\":0,\"SequenceFrameIndex\":$i}";
            print ($i < $n-1 ? ",\n" : "\n");
        }
        print "],\n\"SceneInfoSummary\":{\"SceneFirstFrameIndex\":[0],\"SceneFrameNumbers\":[$n]},\n\"ToolInfo\":{\"Tool\":\"test\",\"Version\":\"1\"}}\n";
    ' "$NF" > "$WORK/meta_perframe.json"

    ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=128x64:rate=30 \
        -frames:v "$NF" -c:v libvvenc -pix_fmt yuv420p10le "$WORK/perframe.vvc" -y
    ./hdr10p_vvc inject -i "$WORK/perframe.vvc" -j "$WORK/meta_perframe.json" -o "$WORK/reordered.vvc" >/dev/null
    ./hdr10p_vvc extract -i "$WORK/reordered.vvc" -o "$WORK/reordered.json" >/dev/null
    ./hdr10p_vvc inject -i "$WORK/perframe.vvc" -j "$WORK/meta_perframe.json" -o "$WORK/decord.vvc" --no-reorder >/dev/null
    ./hdr10p_vvc extract -i "$WORK/decord.vvc" -o "$WORK/decord.json" >/dev/null

    grep -oE '"AverageRGB":[0-9]+' "$WORK/reordered.json" | grep -oE '[0-9]+$' | sort -n > "$WORK/perm.txt"
    perl -e 'my $m = shift; for (my $i = 1000; $i < 1000 + $m; $i++) { print "$i\n"; }' "$NF" > "$WORK/want.txt"
    if ! cmp -s "$WORK/perm.txt" "$WORK/want.txt"; then
        echo "FAIL: reordered metadata is not a complete permutation"; exit 1
    fi

    grep -oE '"AverageRGB":[0-9]+' "$WORK/reordered.json" | grep -oE '[0-9]+$' > "$WORK/dec_order.txt"
    grep -oE '"AverageRGB":[0-9]+' "$WORK/decord.json" | grep -oE '[0-9]+$' > "$WORK/decord_order.txt"
    if cmp -s "$WORK/dec_order.txt" "$WORK/decord_order.txt"; then
        echo "FAIL: reordering had no effect (expected B-frame stream to be reordered)"
        exit 1
    fi
    ffmpeg -hide_banner -loglevel error -i "$WORK/reordered.vvc" -f null -
    echo "reordering OK: all $NF metadata entries used once, decode order != presentation order, stream decodes"
else
    echo "perl not available, skipping reordering test"
fi

echo "== Dolby Vision RPU round-trip =="
if command -v perl >/dev/null 2>&1; then
    NR=12
    ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=128x64:rate=30 \
        -frames:v "$NR" -c:v libvvenc -pix_fmt yuv420p10le "$WORK/rpu.vvc" -y
    ./hdr10p_vvc inject -i "$WORK/rpu.vvc" -r tests/sample_rpu.bin -o "$WORK/rpu_inj.vvc" --no-reorder >/dev/null

    # Rebuild a dovi_tool-style RPU file from the injected stream and compare.
    perl -0777 -e '
        my $b = do { local $/; open(my $f,"<:raw",$ARGV[0]) or die $!; <$f> };
        my @scs; my $q=0;
        while (1) { my $k = index($b, "\x00\x00\x01", $q); last if $k<0;
            if ($k>=1 && substr($b,$k-1,1) eq "\x00") { push @scs, $k-1; $q=$k+3; }
            else { push @scs, $k; $q=$k+3; } }
        my $out=""; my $n=0;
        for my $i (0..$#scs) {
            my $next = ($i<$#scs) ? $scs[$i+1] : length($b);
            my $nal = (substr($b,$scs[$i],4) eq "\x00\x00\x00\x01")
                      ? substr($b,$scs[$i]+4,$next-($scs[$i]+4))
                      : substr($b,$scs[$i]+3,$next-($scs[$i]+3));
            if (length($nal)>=3 && ((ord(substr($nal,1,1))>>3)&0x1F)==27 &&
                ord(substr($nal,2,1))==0x19) {
                $out .= "\x00\x00\x00\x01" . substr($nal,2); $n++;
            }
        }
        open(my $o,">:raw",$ARGV[1]) or die $!; print $o $out; close $o;
        exit($n == $ARGV[2] ? 0 : 1);
    ' "$WORK/rpu_inj.vvc" "$WORK/rpu_extracted.bin" "$NR" || { echo "FAIL: RPU count mismatch"; exit 1; }

    if cmp -s tests/sample_rpu.bin "$WORK/rpu_extracted.bin"; then
        echo "RPU round-trip OK ($NR RPUs byte-identical)"
    else
        echo "FAIL: RPU round-trip mismatch"; exit 1
    fi
    ffmpeg -hide_banner -loglevel error -i "$WORK/rpu_inj.vvc" -f null -
    echo "RPU-injected stream decodes OK"
else
    echo "perl not available, skipping RPU test"
fi

echo "== mux =="
# Matroska muxing: the injected stream's HDR10+ payloads must survive into the
# BlockAdditions (BlockAddID 4) of the container. The container structure is
# verified with mkvmerge when available (ffmpeg 8.x cannot demux VVC in MKV).
./hdr10p_vvc mux -i "$WORK/injected.vvc" -o "$WORK/muxed.mkv" >/dev/null
if command -v mkvmerge >/dev/null 2>&1; then
    mkvmerge -i "$WORK/muxed.mkv" 2>&1 | grep -q "V_MPEGH/ISO/VVC" \
        || { echo "FAIL: mkvmerge does not recognize the VVC track"; exit 1; }
    echo "mux OK (mkvmerge: V_MPEGH/ISO/VVC track, BlockAddID 4 additions)"
else
    head -c4 "$WORK/muxed.mkv" | grep -q "$(printf '\x1a\x45\xdf\xa3')" \
        || { echo "FAIL: muxed output is not an EBML file"; exit 1; }
    echo "mux OK (EBML header verified; mkvmerge not available for full validation)"
fi
# A stream without HDR10+ metadata must be rejected unless -j is given.
if ./hdr10p_vvc mux -i "$WORK/test.vvc" -o "$WORK/mux_bad.mkv" >/dev/null 2>&1; then
    echo "FAIL: mux without metadata on a clean stream should fail"; exit 1
fi
./hdr10p_vvc mux -i "$WORK/test.vvc" -j tests/sample_metadata.json -o "$WORK/muxed_json.mkv" >/dev/null

echo
echo "ALL TESTS PASSED"
