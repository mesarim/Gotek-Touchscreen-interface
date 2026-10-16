#!/bin/sh
# Skin upload / download / delete against a real GTi screen (JC3248, firmware with /api/skins).
#   usage: tools/test_skin_upload.sh <ip>        e.g. tools/test_skin_upload.sh 192.168.1.42
# Needs curl, base64 (GNU, -w0), dd, cmp, tr. The active theme is not touched (no activate call), so the
# screen keeps its look.
#
# WARNING: the script DELETES the skins named TUPLOAD, THALF, TLOWER and TBAD on the screen (it uploads, then
# deletes them). A skin of yours with one of those names is gone afterwards. Every name the script sends:
#   TUPLOAD, THALF, TBAD, tlower (= TLOWER)   written and deleted
#   ../TLOWER, ABCDEFGHIJKLMNOPQ, ""          refused before anything is written
#
# Each check prints "ok   <what>" or "FAIL <what> (<detail>)"; the last line is "N passed, M failed" and the
# exit code is the number of failures. Expected output on a correct firmware (21 checks):
#   ok   has_skins in /api/system/info
#   ok   upload bg.gts in 4096-byte chunks
#   ok   commit bg.gts
#   ok   TUPLOAD in /api/themes/list
#   ok   GET returns identical bytes
#   ok   style of TUPLOAD is the manifest
#   ok   replayed chunk -> 400 out of order
#   ok   out-of-order chunk -> 400 out of order
#   ok   in-order chunk after the refusals
#   ok   half upload: THALF not in /api/themes/list
#   ok   new off=0 starts clean, commit ok
#   ok   name ../TLOWER -> 400 or 404
#   ok   name tlower -> taken as TLOWER, no skin, not listed
#   ok   17-character name -> 400
#   ok   empty name -> 400 or 404
#   ok   commit truncated.gts -> 400 bad chunk length
#   ok   commit lying.gts -> 400 bad chunk length
#   ok   commit wide.gts -> 400 atlas is 300x192, expected 256x192
#   ok   delete TUPLOAD
#   ok   TUPLOAD gone (GET 404, not listed)
#   ok   valid.gts single chunk upload + commit + delete
#   21 passed, 0 failed
# The 400 messages are also printed (indented) so the human can read them; expected:
#   ../TLOWER      {"error":"bad skin name: A-Z, 0-9 and _ only, max 16, not starting with a digit"}
#   truncated.gts  {"error":"bad chunk length"}      (gts::errText)
#   lying.gts      {"error":"bad chunk length"}      (gts::errText)
#   wide.gts       {"error":"atlas is 300x192, expected 256x192"}
# Afterwards the serial log / /gti.log has "[skin] upload TBAD refused: ..." for each of the three.
set -u

IP="${1:?usage: $0 <ip>}"
B="http://$IP"
cd "$(dirname "$0")/.." || exit 99
SK=tools/test_skins
TMP="${TMPDIR:-/tmp}/gti_skin_up.$$"
mkdir -p "$TMP" || exit 99
trap 'rm -rf "$TMP"' EXIT
PASS=0
FAIL=0

ok()   { PASS=$((PASS + 1)); echo "ok   $1"; }
bad()  { FAIL=$((FAIL + 1)); echo "FAIL $1 ($2)"; }

# post <url> [curl args...] : body -> $TMP/body, HTTP code -> stdout
post() {
  u="$1"; shift
  curl -s --path-as-is -o "$TMP/body" -w '%{http_code}' -X POST "$@" "$u"
}
get() {
  curl -s --path-as-is -o "$TMP/body" -w '%{http_code}' "$1"
}
body() { cat "$TMP/body"; }

# chunk <name> <file> <off> <len> : sends bytes [off, off+len) of file as one chunk, prints the HTTP code
chunk() {
  dd if="$2" of="$TMP/piece" bs=1 skip="$3" count="$4" 2>/dev/null
  base64 -w0 "$TMP/piece" | tr '+/' '-_' | tr -d '=' > "$TMP/b64"
  post "$B/api/skins/$1/chunk" --data-urlencode "off=$3" --data-urlencode "data@$TMP/b64"
}

# upload <name> <file> [stop_after_bytes] : 4096-byte chunks from off=0; 0 = every chunk answered 200
upload() {
  size=$(wc -c < "$2" | tr -d ' ')
  stop="${3:-$size}"
  off=0
  while [ "$off" -lt "$stop" ]; do
    len=4096
    [ $((off + len)) -gt "$size" ] && len=$((size - off))
    code=$(chunk "$1" "$2" "$off" "$len")
    if [ "$code" != 200 ]; then echo "  chunk off=$off -> $code $(body)"; return 1; fi
    off=$((off + len))
  done
  return 0
}

listed() { get "$B/api/themes/list" >/dev/null; grep -q "\"$1\"" "$TMP/body"; }

# ── 1. capability flag
code=$(get "$B/api/system/info")
if [ "$code" = 200 ] && grep -q '"has_skins":true' "$TMP/body"; then ok "has_skins in /api/system/info"
else bad "has_skins in /api/system/info" "$code"; fi

# ── 2. full chunked upload (bg.gts is 44002 bytes = 11 chunks), commit, list, download
if upload TUPLOAD "$SK/bg.gts"; then ok "upload bg.gts in 4096-byte chunks"; else bad "upload bg.gts in 4096-byte chunks" "see above"; fi
code=$(post "$B/api/skins/TUPLOAD/commit")
if [ "$code" = 200 ]; then ok "commit bg.gts"; else bad "commit bg.gts" "$code $(body)"; fi
if listed TUPLOAD; then ok "TUPLOAD in /api/themes/list"; else bad "TUPLOAD in /api/themes/list" "$(body)"; fi
curl -s -o "$TMP/down.gts" -w '%{http_code}' "$B/api/skins/TUPLOAD" > "$TMP/code"
if [ "$(cat "$TMP/code")" = 200 ] && cmp -s "$SK/bg.gts" "$TMP/down.gts"; then ok "GET returns identical bytes"
else bad "GET returns identical bytes" "HTTP $(cat "$TMP/code")"; fi
code=$(get "$B/api/themes/TUPLOAD/style")
if [ "$code" = 200 ] && grep -q '"accent"' "$TMP/body"; then ok "style of TUPLOAD is the manifest"
else bad "style of TUPLOAD is the manifest" "$code $(body)"; fi

# ── 3. replayed and out-of-order chunks (THALF: first two chunks sent)
upload THALF "$SK/bg.gts" 8192 >/dev/null
code=$(chunk THALF "$SK/bg.gts" 4096 4096)
if [ "$code" = 400 ] && grep -q 'out of order' "$TMP/body"; then ok "replayed chunk -> 400 out of order"
else bad "replayed chunk -> 400 out of order" "$code $(body)"; fi
code=$(chunk THALF "$SK/bg.gts" 16384 4096)
if [ "$code" = 400 ] && grep -q 'out of order' "$TMP/body"; then ok "out-of-order chunk -> 400 out of order"
else bad "out-of-order chunk -> 400 out of order" "$code $(body)"; fi
# the .part is unchanged: the next in-order chunk (off=8192) is still accepted
code=$(chunk THALF "$SK/bg.gts" 8192 4096)
if [ "$code" = 200 ]; then ok "in-order chunk after the refusals"; else bad "in-order chunk after the refusals" "$code $(body)"; fi

# ── 4. stopped halfway: never listed; a new off=0 upload starts clean
if listed THALF; then bad "half upload: THALF not in /api/themes/list" "listed"; else ok "half upload: THALF not in /api/themes/list"; fi
if upload THALF "$SK/valid.gts" && [ "$(post "$B/api/skins/THALF/commit")" = 200 ]; then
  curl -s -o "$TMP/half.gts" "$B/api/skins/THALF"
  if cmp -s "$SK/valid.gts" "$TMP/half.gts"; then ok "new off=0 starts clean, commit ok"
  else bad "new off=0 starts clean, commit ok" "downloaded file differs"; fi
else bad "new off=0 starts clean, commit ok" "$(body)"; fi
post "$B/api/skins/THALF/delete" >/dev/null

# ── 5. bad names: nothing is written, nothing listed
code=$(chunk "../TLOWER" "$SK/valid.gts" 0 100)
case "$code" in 400|404) ok "name ../TLOWER -> 400 or 404"; echo "    $(body)";; *) bad "name ../TLOWER -> 400 or 404" "$code";; esac
# lower case is upper-cased first, as /api/themes/<name> does: "tlower" is the skin TLOWER (a .part only, no .gts)
code=$(chunk "tlower" "$SK/valid.gts" 0 100)
c2=$(get "$B/api/skins/TLOWER")
post "$B/api/skins/TLOWER/delete" >/dev/null
case "$code" in 200|400|404) if [ "$c2" = 404 ] && ! listed TLOWER && ! listed tlower; then ok "name tlower -> taken as TLOWER, no skin, not listed"; else bad "name tlower -> taken as TLOWER, no skin, not listed" "GET TLOWER $c2"; fi;;
  *) bad "name tlower -> taken as TLOWER, no skin, not listed" "$code";; esac
code=$(chunk "ABCDEFGHIJKLMNOPQ" "$SK/valid.gts" 0 100)
if [ "$code" = 400 ]; then ok "17-character name -> 400"; else bad "17-character name -> 400" "$code"; fi
code=$(post "$B/api/skins//chunk" --data-urlencode "off=0" --data-urlencode "data=QQ")
case "$code" in 400|404) ok "empty name -> 400 or 404";; *) bad "empty name -> 400 or 404" "$code";; esac

# ── 6. commit of bad files: 400 with the text, no skin created
for f in truncated lying wide; do
  upload TBAD "$SK/$f.gts" >/dev/null
  code=$(post "$B/api/skins/TBAD/commit")
  msg=$(body)
  c2=$(get "$B/api/skins/TBAD")
  want='"error":"bad chunk length"'   # gts::errText(BAD_LENGTH)
  [ "$f" = wide ] && want='"error":"atlas is 300x192, expected 256x192"'
  label="commit $f.gts -> 400 bad chunk length"
  [ "$f" = wide ] && label="commit wide.gts -> 400 atlas is 300x192, expected 256x192"
  if [ "$code" = 400 ] && printf '%s' "$msg" | grep -q "$want" && [ "$c2" = 404 ] && ! listed TBAD; then ok "$label"
  else bad "$label" "$code $msg, GET $c2"; fi
  echo "    $msg"
done

# ── 7. delete
code=$(post "$B/api/skins/TUPLOAD/delete")
if [ "$code" = 200 ]; then ok "delete TUPLOAD"; else bad "delete TUPLOAD" "$code $(body)"; fi
c2=$(get "$B/api/skins/TUPLOAD")
if [ "$c2" = 404 ] && ! listed TUPLOAD; then ok "TUPLOAD gone (GET 404, not listed)"; else bad "TUPLOAD gone (GET 404, not listed)" "GET $c2"; fi

# ── 8. the small file from the brief: one chunk
if upload TUPLOAD "$SK/valid.gts" && [ "$(post "$B/api/skins/TUPLOAD/commit")" = 200 ] \
   && listed TUPLOAD && [ "$(post "$B/api/skins/TUPLOAD/delete")" = 200 ]; then ok "valid.gts single chunk upload + commit + delete"
else bad "valid.gts single chunk upload + commit + delete" "$(body)"; fi

echo "$PASS passed, $FAIL failed"
exit "$FAIL"
