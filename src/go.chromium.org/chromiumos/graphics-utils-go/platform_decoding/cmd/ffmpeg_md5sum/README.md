# ffmpeg\_md5sum
ffmpeg\_md5sum is a wrapper around ffmpeg that generates MD5 sums from decoding
for each frame of the input video, and processes that (annotated) output to
output one hash per line.

# Args
The wrapper accepts the following arguments:
 * video: path to video to decode
 * flags: additional flags to pass to ffmpeg. For flags whose specification
  relies on space separation, order matters, and pass each token individually,
  i.e.  pass `--flags -hwaccel --flags vaapi` to the wrapper in order to pass
  `-hwaccel vaapi` to ffmpeg.

# Details
The arguments to ffmpeg that this wrapper pre-populates are
`-hide_banner -loglevel verbose -vf format=pix_fmts=yuv420p -f framemd5 -`,
which output MD5 sums to the stdout.

# Build and deploy
``` bash
(inside) emerge-$BOARD graphics-utils-go
(inside) cros deploy $DUT graphics-utils-go
(dut) /usr/local/graphics/ffmpeg_md5sum --video=<video path> --flags=-hwaccel --flags=vaapi
(inside) tast run $DUT video.PlatformDecoding.ffmpeg*
```

# Additional notes
The wrapper is expected to output *only* MD5 sums, one per line, on success.
Logs should be printed to stderr.

The wrapper expects ffmpeg to output any number of header (hash-prefixed) lines,
followed by space-separated lines, one per frame, in which the last token is the
MD5 hash. Any modifications to the ffmpeg flags the wrapper passes should
maintain this format.
