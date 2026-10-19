#include <catch2/catch_test_macros.hpp>

#include <QDir>
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
