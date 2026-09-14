#pragma once

// A single journal serializes SD save transactions. Publish it only after the
// replacement is complete; retain the old image until the new name is durable.
// Call recover() after mounting SD and before resolving a saved image's path.
namespace GotekSave {
static const char* const journal = "/.gotek-save";

template<class FS> bool removeIfPresent(FS& fs, const String& path) {
  return !fs.exists(path) || fs.remove(path);
}

template<class FS> bool recover(FS& fs) {
  if (!fs.exists(journal)) return true;
  auto f = fs.open(journal, "r");
  if (!f || f.size() < 2 || f.size() > 512) return false;
  char path[513] = {};
  const size_t n = f.size();
  if (f.read(reinterpret_cast<uint8_t*>(path), n) != (int)n) return false;
  f.close();
  if (path[0] != '/' || strlen(path) != n) return false;
  String sav(path), backup = sav + ".gti-bak", tmp = sav + ".tmp";
  if (!fs.exists(sav)) {
    // Roll back an interrupted replacement, or finish creating the first save.
    String source = fs.exists(backup) ? backup : tmp;
    if (!fs.exists(source) || !fs.rename(source, sav)) return false;
  }
  if (!removeIfPresent(fs, backup) || !removeIfPresent(fs, tmp)) return false;
  return fs.remove(journal);
}

template<class FS, class SectorReader>
bool patch(FS& fs, const String& master, const String& sav,
           const uint8_t* map, uint32_t mapBits, SectorReader readSector) {
  if (!recover(fs) || sav.length() < 2 || sav.length() > 512 || sav[0] != '/')
    return false;
  String tmp = sav + ".tmp", backup = sav + ".gti-bak";
  if (!removeIfPresent(fs, tmp) || fs.exists(backup)) return false;
  auto in = fs.open(fs.exists(sav) ? sav : master, "r");
  if (!in) return false;
  const size_t size = in.size();
  if (!size || size % 512) return false;
  auto out = fs.open(tmp, "w");
  if (!out) return false;
  uint8_t buf[512];
  bool ok = true;
  for (size_t copied = 0; copied < size; copied += sizeof(buf)) {
    if (in.read(buf, sizeof(buf)) != sizeof(buf) ||
        out.write(buf, sizeof(buf)) != sizeof(buf)) { ok = false; break; }
  }
  in.close();
  out.flush();
  ok = ok && out.size() == size;
  out.close();
  if (!ok) { fs.remove(tmp); return false; }
  out = fs.open(tmp, "r+");
  if (!out) return false;
  uint32_t packedIndex = 0;
  for (uint32_t i = 0; i < mapBits; ++i) {
    if (!(map[i >> 3] & (1u << (i & 7)))) continue;
    if (i >= size / 512 || !readSector(i, packedIndex++, buf) ||
        !out.seek(i * 512UL) || out.write(buf, sizeof(buf)) != sizeof(buf)) {
      ok = false; break;
    }
  }
  out.flush();
  ok = ok && out.size() == size;
  out.close();
  if (!ok) { fs.remove(tmp); return false; }

  String pending = String(journal) + ".tmp";
  if (!removeIfPresent(fs, pending)) return false;
  out = fs.open(pending, "w");
  if (!out) return false;
  ok = out.write(reinterpret_cast<const uint8_t*>(sav.c_str()), sav.length()) == sav.length();
  out.flush();
  ok = ok && out.size() == sav.length();
  out.close();
  if (!ok || !fs.rename(pending, journal)) return false;
  if (fs.exists(sav) && !fs.rename(sav, backup)) return false;
  if (!fs.rename(tmp, sav)) {
    if (fs.exists(backup)) fs.rename(backup, sav);
    return false;
  }
  // Once installed the new image is valid. Leave the journal if cleanup fails;
  // recovery will finish it before the next save or library scan.
  recover(fs);
  return true;
}
} // namespace GotekSave
