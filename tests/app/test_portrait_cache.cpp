#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QImage>

#include "portrait_cache.hpp"
#include "temp_dir.hpp"

using namespace sns::app;

namespace {

QString qpath(const std::filesystem::path& p) { return QString::fromStdWString(p.wstring()); }

// The profile as a portrait sees it: data path -> resource as it is now.
struct Profile {
  QHash<QString, PortraitResource> resources;
  [[nodiscard]] PortraitResource current(const QString& path) const {
    return resources.value(path, PortraitResource{path, QStringLiteral("missing"), -1, -1});
  }
};

PortraitEntry entry_for(const QString& form_key, const Profile& profile) {
  PortraitEntry e;
  e.form_key = form_key;
  e.renderer_version = QStringLiteral("portrait-v1");
  e.snapshot_digest = QStringLiteral("snapshot-A");
  for (const auto& r : profile.resources) e.resources.push_back(r);
  e.width = 256;
  e.height = 256;
  e.render_ms = 120;
  e.created_at = QStringLiteral("2026-10-18T10:00:00.000-03:00");
  return e;
}

const QByteArray kPng("\x89PNG\r\n\x1a\nportrait-bytes", 22);

Profile lydia_profile() {
  Profile p;
  p.resources.insert("meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\000a2c8e.nif",
                     {"meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\000a2c8e.nif", "loose:F:/Mods/Helen/x.nif", 3726729, 1700000000000});
  p.resources.insert("textures\\actors\\character\\facegendata\\facetint\\skyrim.esm\\000a2c8e.dds",
                     {"textures\\actors\\character\\facegendata\\facetint\\skyrim.esm\\000a2c8e.dds", "archive:Skyrim - Textures1.bsa@<Game Data>", -1, -1});
  return p;
}

}  // namespace

TEST_CASE("Portrait cache: one entry per NPC whatever the FormKey spelling", "[portrait]") {
  CHECK(PortraitCache::entry_name("0A2C8E:Skyrim.esm") == PortraitCache::entry_name("000A2C8E:skyrim.esm"));
  CHECK(PortraitCache::entry_name("0A2C8E:Skyrim.esm") == "000a2c8e_skyrim.esm");
  CHECK(PortraitCache::entry_name("0A2C8E:Skyrim.esm") != PortraitCache::entry_name("0A2C8E:Dawnguard.esm"));
  CHECK(PortraitCache::entry_name("0370FB:alternate start - live another life.esp") == "000370fb_alternate_start_-_live_another_life.esp");
}

TEST_CASE("Portrait cache: an entry survives a save and reopen; bad entries are refused", "[portrait]") {
  const auto e = entry_for("0A2C8E:Skyrim.esm", lydia_profile());
  auto copy = e;
  copy.image_file = "000a2c8e_skyrim.esm-abc.png";
  const auto back = portrait_entry_from_json(portrait_entry_to_json(copy), nullptr);
  REQUIRE(back);
  CHECK(back->form_key == e.form_key);
  CHECK(back->resources.size() == 2);
  CHECK(back->resources == copy.resources);
  CHECK(back->source == "studio-render");
  QString error;
  CHECK_FALSE(portrait_entry_from_json("not json", &error));
  CHECK_FALSE(portrait_entry_from_json(R"({"schema": 9, "formKey": "1:A.esp", "imageFile": "a.png"})", &error));
  CHECK_FALSE(portrait_entry_from_json(R"({"schema": 1, "formKey": "1:A.esp", "imageFile": "..\\outside.png"})", &error));
}

TEST_CASE("Portrait cache: a stored portrait is found again while nothing it used changed", "[portrait]") {
  sns::test::TempDir dir;
  const auto profile = lydia_profile();
  PortraitCache cache(qpath(dir.path() / "portraits"));
  const auto current = [&](const QString& path) { return profile.current(path); };

  CHECK(cache.lookup("0A2C8E:Skyrim.esm", "portrait-v1", "snapshot-A", current).state == PortraitCacheState::Missing);
  QString error;
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), kPng, &error));

  // A new cache object over the same folder (a second run of the studio) finds it.
  const PortraitCache reopened(qpath(dir.path() / "portraits"));
  const auto found = reopened.lookup("000A2C8E:skyrim.esm", "portrait-v1", "snapshot-A", current);
  REQUIRE(found.state == PortraitCacheState::Valid);
  REQUIRE(found.entry);
  CHECK(found.entry->form_key == "0A2C8E:Skyrim.esm");
  CHECK(QFile::exists(found.image_path));
  QFile image(found.image_path);
  REQUIRE(image.open(QIODevice::ReadOnly));
  CHECK(image.readAll() == kPng);
}

TEST_CASE("Portrait cache: a changed resource, profile or renderer makes the portrait stale", "[portrait]") {
  sns::test::TempDir dir;
  auto profile = lydia_profile();
  PortraitCache cache(qpath(dir.path() / "portraits"));
  QString error;
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), kPng, &error));
  const auto lookup = [&](const QString& renderer, const QString& snapshot) {
    return cache.lookup("0A2C8E:Skyrim.esm", renderer, snapshot, [&](const QString& path) { return profile.current(path); });
  };
  REQUIRE(lookup("portrait-v1", "snapshot-A").state == PortraitCacheState::Valid);

  SECTION("the FaceGen file was replaced (another size and date)") {
    auto& nif = profile.resources["meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\000a2c8e.nif"];
    nif.size += 44;
    nif.modified_ms += 1000;
    const auto stale = lookup("portrait-v1", "snapshot-A");
    CHECK(stale.state == PortraitCacheState::Stale);
    CHECK(stale.reason.contains("facegeom"));
  }
  SECTION("another mod now provides the tint") {
    profile.resources["textures\\actors\\character\\facegendata\\facetint\\skyrim.esm\\000a2c8e.dds"].location = "loose:F:/Mods/Other/x.dds";
    CHECK(lookup("portrait-v1", "snapshot-A").state == PortraitCacheState::Stale);
  }
  SECTION("a resource disappeared") {
    profile.resources.remove("textures\\actors\\character\\facegendata\\facetint\\skyrim.esm\\000a2c8e.dds");
    CHECK(lookup("portrait-v1", "snapshot-A").state == PortraitCacheState::Stale);
  }
  SECTION("the MO2 profile or load order changed") {
    CHECK(lookup("portrait-v1", "snapshot-B").state == PortraitCacheState::Stale);
  }
  SECTION("the renderer changed (framing, lights or materials)") {
    CHECK(lookup("portrait-v2", "snapshot-A").state == PortraitCacheState::Stale);
  }
  SECTION("the image was altered on disk") {
    const auto found = lookup("portrait-v1", "snapshot-A");
    QFile image(found.image_path);
    REQUIRE(image.open(QIODevice::WriteOnly | QIODevice::Truncate));
    image.write("tampered");
    image.close();
    const auto stale = lookup("portrait-v1", "snapshot-A");
    CHECK(stale.state == PortraitCacheState::Stale);
    CHECK(stale.reason.contains("hash"));
  }
}

TEST_CASE("Portrait cache: a new render replaces the previous image of the same NPC", "[portrait]") {
  sns::test::TempDir dir;
  const auto profile = lydia_profile();
  PortraitCache cache(qpath(dir.path() / "portraits"));
  QString error;
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), kPng, &error));
  const auto first = cache.lookup("0A2C8E:Skyrim.esm", "portrait-v1", "snapshot-A", [&](const QString& p) { return profile.current(p); });
  REQUIRE(first.state == PortraitCacheState::Valid);
  const QByteArray other("\x89PNG\r\n\x1a\nanother-render", 22);
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), other, &error));
  const auto second = cache.lookup("0A2C8E:Skyrim.esm", "portrait-v1", "snapshot-A", [&](const QString& p) { return profile.current(p); });
  REQUIRE(second.state == PortraitCacheState::Valid);
  CHECK(second.image_path != first.image_path);
  // The replaced render is kept as the previous version, with its own entry; a third render drops it.
  const auto kept = cache.previous("0A2C8E:Skyrim.esm");
  REQUIRE(kept);
  CHECK(kept->image_path == first.image_path);
  CHECK(QDir(qpath(dir.path() / "portraits")).entryList({"*.png"}).size() == 2);
  const QByteArray third("\x89PNG\r\n\x1a\nthird-render", 20);
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), third, &error));
  CHECK_FALSE(QFile::exists(first.image_path));
  REQUIRE(cache.previous("0A2C8E:Skyrim.esm"));
  CHECK(cache.previous("0A2C8E:Skyrim.esm")->image_path == second.image_path);
  CHECK(QDir(qpath(dir.path() / "portraits")).entryList({"*.png"}).size() == 2);
  // Storing the same image again keeps the previous version as it was.
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), third, &error));
  REQUIRE(cache.previous("0A2C8E:Skyrim.esm"));
  CHECK(cache.previous("0A2C8E:Skyrim.esm")->image_path == second.image_path);
  // Another NPC keeps its own entry.
  REQUIRE(cache.store(entry_for("002B6C:Dawnguard.esm", profile), kPng, &error));
  CHECK(QDir(qpath(dir.path() / "portraits")).entryList({"*.json"}).size() == 3);
}

TEST_CASE("Portrait cache: two renders are compared by the resources they recorded", "[portrait]") {
  sns::test::TempDir dir;
  auto profile = lydia_profile();
  PortraitCache cache(qpath(dir.path() / "portraits"));
  QString error;
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), kPng, &error));
  CHECK_FALSE(cache.previous("0A2C8E:Skyrim.esm"));
  // A new FaceGen mesh wins: the portrait goes stale, is rendered again, and the comparison names the file.
  const QString nif("meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\000a2c8e.nif");
  profile.resources[nif] = {nif, "loose:F:/Mods/Other/x.nif", 4000000, 1800000000000};
  const auto stale = cache.lookup("0A2C8E:Skyrim.esm", "portrait-v1", "snapshot-A", [&](const QString& p) { return profile.current(p); });
  REQUIRE(stale.state == PortraitCacheState::Stale);
  const QByteArray other("\x89PNG\r\n\x1a\nanother-render", 22);
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), other, &error));
  const auto before = cache.previous("0A2C8E:Skyrim.esm");
  const auto after = cache.current("0A2C8E:Skyrim.esm");
  REQUIRE(before);
  REQUIRE(after);
  CHECK(PortraitCache::changed_resources(before->entry, after->entry) == QStringList{nif});
  CHECK(PortraitCache::changed_resources(after->entry, after->entry).isEmpty());
  // The same pixels made from other resources still keep the render they replace: the change did not show.
  const QString tint("textures\\actors\\character\\facegendata\\facetint\\skyrim.esm\\000a2c8e.dds");
  profile.resources[tint] = {tint, "loose:F:/Mods/Tint/x.dds", 1024, 1800000000001};
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), other, &error));
  const auto same_pixels = cache.previous("0A2C8E:Skyrim.esm");
  REQUIRE(same_pixels);
  CHECK(same_pixels->image_path == cache.current("0A2C8E:Skyrim.esm")->image_path);
  CHECK(PortraitCache::changed_resources(same_pixels->entry, cache.current("0A2C8E:Skyrim.esm")->entry) == QStringList{tint});
  CHECK(QFile::exists(before->image_path) == false);  // the first render is two versions old now
  // A previous image altered on disk is no evidence: it is not offered.
  QFile image(same_pixels->image_path);
  REQUIRE(image.open(QIODevice::WriteOnly | QIODevice::Truncate));
  image.write("tampered");
  image.close();
  QString reason;
  CHECK_FALSE(cache.previous("0A2C8E:Skyrim.esm", &reason));
  CHECK(reason.contains("hash"));
}

TEST_CASE("Portrait cache: a Skyrim capture is kept apart and never taken for a studio render", "[portrait]") {
  sns::test::TempDir dir;
  const auto profile = lydia_profile();
  PortraitCache cache(qpath(dir.path() / "portraits"));
  QString error;
  const auto shot = qpath(dir.path() / "ScreenShot12.png");
  QImage picture(64, 48, QImage::Format_RGB32);
  picture.fill(Qt::darkGreen);
  REQUIRE(picture.save(shot, "PNG"));
  REQUIRE(cache.store_game_capture("0A2C8E:Skyrim.esm", shot, "2026-10-18T12:00:00.000-03:00", &error));
  const auto capture = cache.game_capture("0A2C8E:Skyrim.esm");
  REQUIRE(capture);
  CHECK(capture->entry.source == PortraitCache::kSkyrimCapture);
  CHECK(capture->entry.width == 64);
  CHECK(capture->entry.height == 48);
  REQUIRE(capture->entry.resources.size() == 1);
  CHECK(capture->entry.resources[0].location.startsWith("user-file:"));
  // It is not a portrait of the card: no studio entry exists, and the lookup still asks for a render.
  CHECK_FALSE(cache.current("0A2C8E:Skyrim.esm"));
  CHECK(cache.lookup("0A2C8E:Skyrim.esm", "portrait-v1", "snapshot-A", [&](const QString& p) { return profile.current(p); }).state ==
        PortraitCacheState::Missing);
  // A studio render next to it leaves the capture alone.
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), kPng, &error));
  CHECK(cache.game_capture("0A2C8E:Skyrim.esm"));
  CHECK(cache.current("0A2C8E:Skyrim.esm")->entry.source == PortraitCache::kStudioRender);
  // A file that is not an image is refused.
  const auto text = qpath(dir.path() / "notes.png");
  QFile notes(text);
  REQUIRE(notes.open(QIODevice::WriteOnly));
  notes.write("not an image");
  notes.close();
  CHECK_FALSE(cache.store_game_capture("0A2C8E:Skyrim.esm", text, "2026-10-18T12:01:00.000-03:00", &error));
  CHECK(cache.game_capture("0A2C8E:Skyrim.esm"));  // the one attached before is still there
  CHECK(cache.remove_game_capture("0A2C8E:Skyrim.esm"));
  CHECK_FALSE(cache.game_capture("0A2C8E:Skyrim.esm"));
  CHECK(QFile::exists(cache.current("0A2C8E:Skyrim.esm")->image_path));
}

TEST_CASE("Portrait cache: the stored renders are indexed for instant display, intact ones only", "[portrait]") {
  sns::test::TempDir dir;
  const auto profile = lydia_profile();
  PortraitCache cache(qpath(dir.path() / "portraits"));
  QString error;
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), kPng, &error));
  REQUIRE(cache.store(entry_for("002B6C:Dawnguard.esm", profile), kPng, &error));
  const QByteArray other("\x89PNG\r\n\x1a\nanother-image", 21);
  REQUIRE(cache.store(entry_for("002B6C:Dawnguard.esm", profile), other, &error));  // leaves a previous version
  auto index = cache.scan_current();
  CHECK(index.size() == 2);  // previous versions are not current renders
  REQUIRE(index.contains(PortraitCache::entry_name("0A2C8E:Skyrim.esm")));
  CHECK(index.value(PortraitCache::entry_name("0A2C8E:Skyrim.esm")).entry.form_key == "0A2C8E:Skyrim.esm");
  // Indexed whatever the profile now says (it is only shown as provisional): an outdated digest is still listed.
  auto outdated = entry_for("013BB9:Skyrim.esm", profile);
  outdated.snapshot_digest = QStringLiteral("snapshot-OLD");
  REQUIRE(cache.store(outdated, kPng, &error));
  CHECK(cache.scan_current().size() == 3);
  // An image altered on disk is never shown, not even as provisional.
  const auto tampered = cache.current("0A2C8E:Skyrim.esm");
  REQUIRE(tampered);
  QFile image(tampered->image_path);
  REQUIRE(image.open(QIODevice::WriteOnly | QIODevice::Truncate));
  image.write("altered");
  image.close();
  index = cache.scan_current();
  CHECK(index.size() == 2);
  CHECK_FALSE(index.contains(PortraitCache::entry_name("0A2C8E:Skyrim.esm")));
  // A capture of Skyrim is never indexed as a render.
  const auto shot = qpath(dir.path() / "shot.png");
  QImage picture(32, 32, QImage::Format_RGB32);
  picture.fill(Qt::red);
  REQUIRE(picture.save(shot, "PNG"));
  REQUIRE(cache.store_game_capture("000A2C8E:skyrim.esm", shot, "2026-10-19T09:00:00.000-03:00", &error));
  CHECK(cache.scan_current().size() == 2);
}

TEST_CASE("Portrait cache: clearing removes the studio renders and keeps the Skyrim captures", "[portrait]") {
  sns::test::TempDir dir;
  const auto profile = lydia_profile();
  const auto folder = qpath(dir.path() / "portraits");
  PortraitCache cache(folder);
  QString error;
  CHECK(cache.clear_renders(&error) == 0);  // no folder yet: nothing to do
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), kPng, &error));
  const QByteArray other("\x89PNG\r\n\x1a\nanother-image", 21);
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), other, &error));
  const auto shot = qpath(dir.path() / "shot.png");
  QImage picture(32, 32, QImage::Format_RGB32);
  picture.fill(Qt::blue);
  REQUIRE(picture.save(shot, "PNG"));
  REQUIRE(cache.store_game_capture("0A2C8E:Skyrim.esm", shot, "2026-10-19T09:00:00.000-03:00", &error));
  // A file of the user next to the cache folder is not inside it and is never touched.
  const auto outside = qpath(dir.path() / "keep-me.png");
  REQUIRE(picture.save(outside, "PNG"));
  const auto before = cache.usage();
  CHECK(before.renders == 1);
  CHECK(before.captures == 1);
  CHECK(before.files == 6);  // entry + image, previous entry + image, capture entry + image
  CHECK(before.bytes > 0);
  CHECK(cache.clear_renders(&error) == 4);
  const auto after = cache.usage();
  CHECK(after.renders == 0);
  CHECK(after.captures == 1);
  CHECK(after.files == 2);
  CHECK(cache.game_capture("0A2C8E:Skyrim.esm"));
  CHECK_FALSE(cache.current("0A2C8E:Skyrim.esm"));
  CHECK_FALSE(cache.previous("0A2C8E:Skyrim.esm"));
  CHECK(QFile::exists(outside));
  CHECK(QFile::exists(shot));
}

namespace {

const QString kMesh = QStringLiteral("meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\000a2c8e.nif");

PortraitCache::InspectRow row_of(const QList<PortraitCache::InspectRow>& rows, const QString& form_key, const QString& kind = QStringLiteral("render")) {
  for (const auto& r : rows) {
    if (r.kind == kind && PortraitCache::entry_name(r.form_key) == PortraitCache::entry_name(form_key)) return r;
  }
  return {};
}

}  // namespace

TEST_CASE("Portrait inspector: a render is valid, or stale with the reason lookup() gives", "[portrait][inspector]") {
  sns::test::TempDir dir;
  auto profile = lydia_profile();
  PortraitCache cache(qpath(dir.path() / "portraits"));
  QString error;
  REQUIRE(cache.store(entry_for("0A2C8E:Skyrim.esm", profile), kPng, &error));
  const auto now = [&](const QString& p) { return profile.current(p); };
  const auto lookup_state = [&](const QString& renderer, const QString& digest) { return cache.lookup("0A2C8E:Skyrim.esm", renderer, digest, now).state; };

  auto rows = cache.inspect("portrait-v1", "snapshot-A", now);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].kind == "render");
  CHECK(rows[0].state == "valid");
  CHECK(rows[0].code == "ok");
  CHECK(rows[0].files.size() == 2);  // its entry and its image
  CHECK(rows[0].bytes > static_cast<qint64>(kPng.size()));
  CHECK(rows[0].form_key == "0A2C8E:Skyrim.esm");
  CHECK_FALSE(PortraitCache::is_unusable(rows[0]));
  CHECK(lookup_state("portrait-v1", "snapshot-A") == PortraitCacheState::Valid);

  rows = cache.inspect("portrait-v2", "snapshot-A", now);
  CHECK(rows[0].state == "stale");
  CHECK(rows[0].code == "renderer-changed");
  CHECK(rows[0].detail.contains("portrait-v1"));
  CHECK(PortraitCache::is_unusable(rows[0]));
  CHECK(lookup_state("portrait-v2", "snapshot-A") == PortraitCacheState::Stale);

  rows = cache.inspect("portrait-v1", "snapshot-B", now);
  CHECK(rows[0].state == "stale");
  CHECK(rows[0].code == "profile-changed");
  CHECK(lookup_state("portrait-v1", "snapshot-B") == PortraitCacheState::Stale);

  profile.resources[kMesh].size += 1;  // the head's mesh is another file now
  rows = cache.inspect("portrait-v1", "snapshot-A", now);
  CHECK(rows[0].state == "stale");
  CHECK(rows[0].code == "resource-changed");
  CHECK(rows[0].changed_resources == 1);
  CHECK(rows[0].detail == kMesh);
  CHECK(lookup_state("portrait-v1", "snapshot-A") == PortraitCacheState::Stale);
}

TEST_CASE("Portrait inspector: damaged files are named, never mistaken for valid ones", "[portrait][inspector]") {
  sns::test::TempDir dir;
  const auto profile = lydia_profile();
  const auto folder = qpath(dir.path() / "portraits");
  PortraitCache cache(folder);
  QString error;
  REQUIRE(cache.store(entry_for("000001:Skyrim.esm", profile), kPng, &error));
  REQUIRE(cache.store(entry_for("000002:Skyrim.esm", profile), kPng + "2", &error));
  REQUIRE(cache.store(entry_for("000003:Skyrim.esm", profile), kPng + "3", &error));
  REQUIRE(QFile::remove(cache.current("000001:Skyrim.esm")->image_path));  // image gone
  {
    QFile f(cache.current("000002:Skyrim.esm")->image_path);  // image replaced by other bytes
    REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("not the portrait");
  }
  {
    QFile f(QDir(folder).filePath("00000004_skyrim.esm.json"));  // an entry that is not one
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("{ this is not json");
  }
  const auto now = [&](const QString& p) { return profile.current(p); };
  const auto rows = cache.inspect("portrait-v1", "snapshot-A", now);
  CHECK(row_of(rows, "000001:Skyrim.esm").code == "image-missing");
  CHECK(row_of(rows, "000002:Skyrim.esm").code == "image-hash");
  CHECK(row_of(rows, "000003:Skyrim.esm").state == "valid");
  int invalid = 0;
  for (const auto& r : rows) invalid += r.code == "entry-invalid" ? 1 : 0;
  CHECK(invalid == 1);
  for (const auto& r : rows) {
    if (r.code == "image-missing" || r.code == "image-hash" || r.code == "entry-invalid") {
      CHECK(r.state == "damaged");
      CHECK(PortraitCache::is_unusable(r));
    }
  }
  // The worst come first.
  CHECK(rows.last().state == "valid");
  // lookup() agrees on what can be used.
  CHECK(cache.lookup("000001:Skyrim.esm", "portrait-v1", "snapshot-A", now).state == PortraitCacheState::Stale);
  CHECK(cache.lookup("000002:Skyrim.esm", "portrait-v1", "snapshot-A", now).state == PortraitCacheState::Stale);
  CHECK(cache.lookup("000003:Skyrim.esm", "portrait-v1", "snapshot-A", now).state == PortraitCacheState::Valid);
}

TEST_CASE("Portrait inspector: removing the unusable keeps what is in use, the Skyrim captures and anything unknown", "[portrait][inspector]") {
  sns::test::TempDir dir;
  const auto current_profile = lydia_profile();
  auto old_profile = lydia_profile();
  old_profile.resources[kMesh].size = 5;  // what a stale render was made from
  const auto folder = qpath(dir.path() / "portraits");
  PortraitCache cache(folder);
  QString error;
  REQUIRE(cache.store(entry_for("000001:Skyrim.esm", current_profile), kPng, &error));        // valid
  REQUIRE(cache.store(entry_for("000002:Skyrim.esm", old_profile), kPng + "2", &error));      // stale
  REQUIRE(cache.store(entry_for("000003:Skyrim.esm", current_profile), kPng + "3", &error));  // valid, with a render it replaced
  REQUIRE(cache.store(entry_for("000003:Skyrim.esm", current_profile), kPng + "3b", &error));
  const auto shot = qpath(dir.path() / "shot.png");
  QImage picture(16, 16, QImage::Format_RGB32);
  picture.fill(Qt::green);
  REQUIRE(picture.save(shot, "PNG"));
  REQUIRE(cache.store_game_capture("000002:Skyrim.esm", shot, "2026-10-19T09:00:00.000-03:00", &error));
  const QDir folder_dir(folder);
  {
    QFile f(folder_dir.filePath("0000000d_skyrim.esm-aaaaaaaaaaaa.png"));  // an image no entry names
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("orphan");
  }
  {
    QFile f(folder_dir.filePath("notes.txt"));  // not ours
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("keep");
  }
  const auto outside = qpath(dir.path() / "keep-me.png");
  REQUIRE(picture.save(outside, "PNG"));
  const auto now = [&](const QString& p) { return current_profile.current(p); };

  const auto rows = cache.inspect("portrait-v1", "snapshot-A", now);
  CHECK(row_of(rows, "000002:Skyrim.esm").state == "stale");
  CHECK(row_of(rows, "000003:Skyrim.esm", "previous").state == "kept");
  CHECK_FALSE(PortraitCache::is_unusable(row_of(rows, "000003:Skyrim.esm", "previous")));  // for comparison, expected to differ
  CHECK(row_of(rows, "000002:Skyrim.esm", "capture").state == "kept");
  int orphans = 0, unknown = 0;
  for (const auto& r : rows) {
    orphans += r.code == "orphan-image" ? 1 : 0;
    unknown += r.code == "unknown-file" ? 1 : 0;
  }
  CHECK(orphans == 1);
  CHECK(unknown == 1);

  const auto stale_image = cache.current("000002:Skyrim.esm")->image_path;
  const auto result = cache.remove_unusable("portrait-v1", "snapshot-A", now, &error);
  CHECK(error.isEmpty());
  CHECK(result.rows == 2);   // the stale render and the orphan image
  CHECK(result.files == 3);  // entry + image + orphan
  CHECK(result.bytes > 0);
  CHECK(result.form_keys == QStringList{"000002:Skyrim.esm"});
  CHECK_FALSE(QFile::exists(stale_image));
  CHECK_FALSE(cache.current("000002:Skyrim.esm"));
  CHECK(cache.current("000001:Skyrim.esm"));
  CHECK(cache.current("000003:Skyrim.esm"));
  CHECK(cache.previous("000003:Skyrim.esm"));
  CHECK(cache.game_capture("000002:Skyrim.esm"));
  CHECK(QFile::exists(folder_dir.filePath("notes.txt")));
  CHECK(QFile::exists(outside));
  CHECK_FALSE(QFile::exists(folder_dir.filePath("0000000d_skyrim.esm-aaaaaaaaaaaa.png")));

  // Nothing left to remove, and removing again is harmless.
  const auto again = cache.remove_unusable("portrait-v1", "snapshot-A", now, &error);
  CHECK(again.rows == 0);
  CHECK(again.files == 0);
}

TEST_CASE("Portrait inspector: a cache of a thousand portraits is inspected and cleaned in a bounded time", "[portrait][inspector]") {
  sns::test::TempDir dir;
  const auto current_profile = lydia_profile();
  auto old_profile = lydia_profile();
  old_profile.resources[kMesh].size = 5;
  PortraitCache cache(qpath(dir.path() / "portraits"));
  QString error;
  constexpr int kPortraits = 1000;
  for (int i = 0; i < kPortraits; ++i) {
    const auto form_key = QStringLiteral("%1:Skyrim.esm").arg(0x100 + i, 6, 16, QLatin1Char('0'));
    // Every fourth portrait is stale; each image is different (its hash is its name).
    REQUIRE(cache.store(entry_for(form_key, i % 4 == 0 ? old_profile : current_profile), kPng + QByteArray::number(i), &error));
  }
  const auto now = [&](const QString& p) { return current_profile.current(p); };
  QElapsedTimer clock;
  clock.start();
  const auto rows = cache.inspect("portrait-v1", "snapshot-A", now);
  const auto inspect_ms = clock.elapsed();
  int stale = 0, valid = 0;
  for (const auto& r : rows) {
    stale += r.state == "stale" ? 1 : 0;
    valid += r.state == "valid" ? 1 : 0;
  }
  CHECK(rows.size() == kPortraits);
  CHECK(stale == kPortraits / 4);
  CHECK(valid == kPortraits - kPortraits / 4);
  clock.restart();
  const auto result = cache.remove_unusable("portrait-v1", "snapshot-A", now, &error);
  const auto remove_ms = clock.elapsed();
  CHECK(result.rows == kPortraits / 4);
  CHECK(result.files == 2 * (kPortraits / 4));
  CHECK(cache.usage().renders == kPortraits - kPortraits / 4);
  WARN("inspect of " << kPortraits << " portraits: " << inspect_ms << " ms; inspect + removal of " << result.rows << ": " << remove_ms << " ms");
  CHECK(inspect_ms < 20000);  // a very loose bound: the measured time is the evidence, this only catches something quadratic
  CHECK(remove_ms < 20000);
}

TEST_CASE("Portrait inspector: an image a kept row still names survives the removal of the stale render that shared it", "[portrait][inspector]") {
  sns::test::TempDir dir;
  const auto profile = lydia_profile();
  PortraitCache cache(qpath(dir.path() / "portraits"));
  QString error;
  auto first = entry_for("0A2C8E:Skyrim.esm", profile);
  REQUIRE(cache.store(first, kPng, &error));
  auto second = entry_for("0A2C8E:Skyrim.esm", profile);
  second.snapshot_digest = "snapshot-B";  // the same pixels, made under another profile: the first one is kept as "previous"
  REQUIRE(cache.store(second, kPng, &error));
  const auto shared = cache.current("0A2C8E:Skyrim.esm")->image_path;
  REQUIRE(cache.previous("0A2C8E:Skyrim.esm"));
  REQUIRE(cache.previous("0A2C8E:Skyrim.esm")->image_path == shared);
  const auto now = [&](const QString& p) { return profile.current(p); };

  // Under a third profile the current render is stale, the previous one is only kept for comparison.
  const auto result = cache.remove_unusable("portrait-v1", "snapshot-C", now, &error);
  CHECK(result.rows == 1);
  CHECK(QFile::exists(shared));
  CHECK_FALSE(cache.current("0A2C8E:Skyrim.esm"));
  CHECK(cache.previous("0A2C8E:Skyrim.esm"));
}
