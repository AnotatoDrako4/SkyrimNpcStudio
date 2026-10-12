#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <NifFile.hpp>
#include <lz4frame.h>
#include <cstring>
#include <sstream>
#include "temp_dir.hpp"
#include "sns/assets/bsa_archive.hpp"
#include "sns/core/error.hpp"
#include "sns/nif/mesh.hpp"
#include "sns/rendering/scene.hpp"
#include "sns/textures/dds.hpp"

namespace {
void put(std::string& b, std::size_t offset, std::uint32_t v) { std::memcpy(b.data()+offset, &v, 4); }
std::string dds() {
  std::string b(136, '\0'); b.replace(0,4,"DDS ");
  put(b,4,124); put(b,12,4); put(b,16,4); put(b,76,32); put(b,80,4); b.replace(84,4,"DXT1");
  return b;
}
std::string bsa(std::string data, bool compress, bool embedded = false) {
  const std::string folder("meshes\0",7), filename("test.nif\0",9);
  std::string payload;
  if (compress) {
    payload.resize(4 + LZ4F_compressFrameBound(data.size(),nullptr)); put(payload,0,static_cast<std::uint32_t>(data.size()));
    const auto n=LZ4F_compressFrame(payload.data()+4,payload.size()-4,data.data(),data.size(),nullptr);
    payload.resize(n+4);
  } else payload=data;
  if (embedded) payload=std::string(1,static_cast<char>(8))+"test.nif"+payload;
  const auto offset=36+24+1+folder.size()+16+filename.size();
  std::string b(offset,'\0'); b.replace(0,4,std::string("BSA\0",4));
  put(b,4,105); put(b,8,36); put(b,12,3u | (compress?4u:0u) | (embedded?0x100u:0u));
  put(b,16,1); put(b,20,1); put(b,24,static_cast<std::uint32_t>(folder.size())); put(b,28,static_cast<std::uint32_t>(filename.size()));
  put(b,44,1); put(b,52,static_cast<std::uint32_t>(60+filename.size()));
  b[60]=static_cast<char>(folder.size()); b.replace(61,folder.size(),folder);
  const auto record=61+folder.size(); put(b,record+8,static_cast<std::uint32_t>(payload.size())); put(b,record+12,static_cast<std::uint32_t>(offset));
  b.replace(record+16,filename.size(),filename); b+=payload; return b;
}
}
TEST_CASE("BSA 105 reads LZ4, plain and embedded files", "[assets]") {
  sns::test::TempDir dir;
  for (bool compressed : {false,true}) for (bool embedded : {false,true}) {
    const auto file=dir.write("test.bsa", bsa("real fixture bytes",compressed,embedded));
    auto archive=sns::assets::BsaArchive::open(file);
    CHECK(archive->file_count()==1);
    CHECK(archive->read("MESHES/test.nif")=="real fixture bytes");
    CHECK_FALSE(archive->read("missing.nif").has_value());
  }
}
TEST_CASE("BSA rejects corrupt headers and compressed frames", "[assets]") {
  sns::test::TempDir dir;
  CHECK_THROWS_AS(sns::assets::BsaArchive::open(dir.write("bad.bsa", std::string(36,'\0'))), sns::Error);
  auto bad=bsa("fixture",true); bad.back()^=0x55;
  auto archive=sns::assets::BsaArchive::open(dir.write("bad-frame.bsa",bad));
  CHECK_THROWS_AS(archive->read("meshes/test.nif"),sns::Error);
}
TEST_CASE("DDS block sizes and malformed data", "[textures]") {
  using namespace sns::textures;
  auto bytes=dds(); auto image=parse_dds(bytes);
  CHECK(image.width==4); CHECK(image.height==4); CHECK(image.format==TextureFormat::BC1); CHECK(image.data.size()==8);
  const auto rgba=decode_rgba(image); CHECK(rgba.format==TextureFormat::RGBA8); CHECK(rgba.data.size()==64);
  CHECK(static_cast<unsigned char>(rgba.data[3])==255);
  CHECK(level_size(TextureFormat::BC7,5,7)==64);
  CHECK_THROWS_AS(parse_dds(bytes.substr(0,130)),sns::Error);
  put(bytes,16,0); CHECK_THROWS_AS(parse_dds(bytes),sns::Error);
  image.data.clear(); CHECK_THROWS_AS(decode_rgba(image),sns::Error);
}
TEST_CASE("nifly roundtrip extracts actual geometry and transforms", "[nif]") {
  nifly::NifFile file; file.Create(nifly::NiVersion::getSSE());
  std::vector<nifly::Vector3> verts{{0,0,0},{10,0,0},{0,10,0}};
  std::vector<nifly::Triangle> tris{{0,1,2}};
  std::vector<nifly::Vector2> uv{{0,0},{1,0},{0,1}};
  auto* shape=file.CreateShapeFromData("Fixture",&verts,&tris,&uv);
  REQUIRE(shape!=nullptr);
  std::ostringstream out(std::ios::binary); REQUIRE(file.Save(out)==0);
  const auto parsed=sns::nif::load_mesh(out.str(),"synthetic.nif");
  REQUIRE(parsed.shapes.size()==1); CHECK(parsed.shapes[0].positions.size()==3); CHECK(parsed.shapes[0].indices.size()==3);
  CHECK(parsed.shapes[0].uvs[1].u==Catch::Approx(1));
  CHECK_THROWS_AS(sns::nif::load_mesh("not a nif","invalid"),sns::Error);
}
TEST_CASE("Skinning applies bone pose and reports unresolved bones", "[scene]") {
  sns::nif::Shape shape; shape.skinned=true; shape.positions={{0,0,1}}; shape.normals={{0,0,1}};
  shape.bones={"bone"}; shape.skin_to_bone.resize(1); shape.influences.resize(1); shape.influences[0].weight[0]=1;
  sns::nif::Skeleton skeleton; sns::nif::Bone bone; bone.name="bone"; bone.to_global.translation={0,0,10}; skeleton.bones.push_back(bone);
  CHECK(sns::rendering::apply_skinning(shape,skeleton).empty()); CHECK(shape.positions[0].z==Catch::Approx(11));
  skeleton.bones.clear(); CHECK(sns::rendering::apply_skinning(shape,skeleton).size()==1);
}
TEST_CASE("Weight morph rejects mismatched topology and preserves endpoints", "[scene]") {
  sns::nif::Shape heavy,thin; heavy.positions={{0,0,10}}; thin.positions={{0,0,0}};
  CHECK(sns::rendering::blend_by_weight(heavy,thin,50)); CHECK(heavy.positions[0].z==Catch::Approx(5));
  heavy.indices={0,0,0}; CHECK_FALSE(sns::rendering::blend_by_weight(heavy,thin,50));
  CHECK(sns::rendering::weight_variant("Meshes/body_1.nif",0)=="Meshes/body_0.nif");
  auto v=sns::rendering::to_view_space({1,2,3}); CHECK(v.x==-1); CHECK(v.y==3); CHECK(v.z==2);
}
