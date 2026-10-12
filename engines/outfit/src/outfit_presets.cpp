#include "sns/outfit/outfit_presets.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>

#include "sns/core/file_io.hpp"

namespace sns::outfit {

using Json = nlohmann::json;

std::uint32_t slot_bit(int slot) {
  if (slot < 30 || slot > 61) return 0;
  return 1u << (slot - 30);
}

std::string slot_display_name(int slot) {
  switch (slot) {
    case 30: return "30 - Cabeza / Casco";
    case 31: return "31 - Cabello";
    case 32: return "32 - Cuerpo / Torso";
    case 33: return "33 - Manos / Guanteletes";
    case 34: return "34 - Antebrazos";
    case 35: return "35 - Amuleto / Collar";
    case 36: return "36 - Anillo";
    case 37: return "37 - Pies / Botas";
    case 38: return "38 - Pantorrillas";
    case 39: return "39 - Escudo";
    case 40: return "40 - Cola";
    case 41: return "41 - Cabello Largo";
    case 42: return "42 - Diadema / Tiara";
    case 43: return "43 - Orejas";
    case 44: return "44 - Rostro / Máscara";
    case 45: return "45 - Cuello";
    case 46: return "46 - Pecho Secundario";
    case 47: return "47 - Capa / Manto";
    case 48: return "48 - Mochila / Miscelánea";
    case 49: return "49 - Cinturón / Pelvis";
    case 50: return "50 - Cabeza Decapitada";
    case 51: return "51 - Cuello Decapitación";
    case 52: return "52 - Pelvis Secundaria";
    default: return std::to_string(slot) + " - Ranura Especial";
  }
}

std::string slot_category_name(int slot) {
  switch (slot) {
    case 30:
    case 42:
    case 44:
      return "Cabeza";
    case 31:
    case 41:
    case 43:
      return "Cabello";
    case 32:
    case 46:
      return "Torso";
    case 33:
    case 34:
      return "Manos";
    case 37:
    case 38:
      return "Pies";
    case 35:
    case 36:
      return "Joyería";
    case 39:
      return "Escudo";
    case 47:
      return "Capa";
    case 48:
    case 49:
    case 52:
      return "Accesorios";
    default:
      return "Otros";
  }
}

std::vector<OutfitPreset> builtin_outfit_presets() {
  return {
      OutfitPreset{
          .id = "nordic_warrior",
          .name = "Guerrera Nórdica",
          .category = "Warrior",
          .description = "Armadura pesada de acero nórdico con guanteletes y botas reforzadas.",
          .pieces = {
              Piece{.form_key = "013952:Skyrim.esm", .name = "Steel Armor", .plugin = "Skyrim.esm", .slot_mask = slot_bit(32)},
              Piece{.form_key = "013953:Skyrim.esm", .name = "Steel Nordic Gauntlets", .plugin = "Skyrim.esm", .slot_mask = slot_bit(33)},
              Piece{.form_key = "013951:Skyrim.esm", .name = "Steel Cuffed Boots", .plugin = "Skyrim.esm", .slot_mask = slot_bit(37)},
          },
      },
      OutfitPreset{
          .id = "winterhold_mage",
          .name = "Maga de Invernalia",
          .category = "Mage",
          .description = "Túnica de mago erudito con capucha encantada y botas ligeras de cuero.",
          .pieces = {
              Piece{.form_key = "010CEE4:Skyrim.esm", .name = "Novice Robes of Destruction", .plugin = "Skyrim.esm", .slot_mask = slot_bit(32)},
              Piece{.form_key = "010CEE8:Skyrim.esm", .name = "Novice Hood", .plugin = "Skyrim.esm", .slot_mask = slot_bit(30)},
              Piece{.form_key = "010CEE6:Skyrim.esm", .name = "Novice Boots", .plugin = "Skyrim.esm", .slot_mask = slot_bit(37)},
          },
      },
      OutfitPreset{
          .id = "forest_hunter",
          .name = "Cazadora de Bosques",
          .category = "Hunter",
          .description = "Vestidura ligera de cuero curtido ideal para arqueras y exploradoras.",
          .pieces = {
              Piece{.form_key = "013922:Skyrim.esm", .name = "Leather Armor", .plugin = "Skyrim.esm", .slot_mask = slot_bit(32)},
              Piece{.form_key = "013921:Skyrim.esm", .name = "Leather Bracers", .plugin = "Skyrim.esm", .slot_mask = slot_bit(33)},
              Piece{.form_key = "013920:Skyrim.esm", .name = "Leather Boots", .plugin = "Skyrim.esm", .slot_mask = slot_bit(37)},
              Piece{.form_key = "013923:Skyrim.esm", .name = "Leather Helmet", .plugin = "Skyrim.esm", .slot_mask = slot_bit(30)},
          },
      },
      OutfitPreset{
          .id = "noble_court",
          .name = "Vestimenta Noble",
          .category = "Noble",
          .description = "Ropas finas de seda y bordado con tiara de zafiro de la alta corte de Soledad.",
          .pieces = {
              Piece{.form_key = "000CEE7:Skyrim.esm", .name = "Fine Clothes", .plugin = "Skyrim.esm", .slot_mask = slot_bit(32)},
              Piece{.form_key = "000CEE8:Skyrim.esm", .name = "Fine Boots", .plugin = "Skyrim.esm", .slot_mask = slot_bit(37)},
              Piece{.form_key = "000166DF:Skyrim.esm", .name = "Silver and Sapphire Circlet", .plugin = "Skyrim.esm", .slot_mask = slot_bit(42)},
          },
      },
      OutfitPreset{
          .id = "heavy_battleplate",
          .name = "Armadura de Batalla Pesada",
          .category = "Armor",
          .description = "Conjunto completo de placas de acero para combate de vanguardia.",
          .pieces = {
              Piece{.form_key = "01395C:Skyrim.esm", .name = "Steel Plate Armor", .plugin = "Skyrim.esm", .slot_mask = slot_bit(32)},
              Piece{.form_key = "01395D:Skyrim.esm", .name = "Steel Plate Gauntlets", .plugin = "Skyrim.esm", .slot_mask = slot_bit(33)},
              Piece{.form_key = "01395B:Skyrim.esm", .name = "Steel Plate Boots", .plugin = "Skyrim.esm", .slot_mask = slot_bit(37)},
              Piece{.form_key = "01395E:Skyrim.esm", .name = "Steel Plate Helmet", .plugin = "Skyrim.esm", .slot_mask = slot_bit(30)},
          },
      },
      OutfitPreset{
          .id = "tavern_casual",
          .name = "Ropa de Descanso / Taberna",
          .category = "Casual",
          .description = "Ropa cómoda tradicional para descanso en posadas o asentamientos.",
          .pieces = {
              Piece{.form_key = "000DDF7:Skyrim.esm", .name = "Tavern Clothes", .plugin = "Skyrim.esm", .slot_mask = slot_bit(32)},
              Piece{.form_key = "000DDF8:Skyrim.esm", .name = "Clothes Boots", .plugin = "Skyrim.esm", .slot_mask = slot_bit(37)},
          },
      },
  };
}

std::vector<OutfitPreset> load_outfit_presets_from_dir(const std::filesystem::path& dir) {
  std::vector<OutfitPreset> out = builtin_outfit_presets();
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
    return out;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      try {
        const auto text = file_io::read_text_file(entry.path());
        const auto j = Json::parse(text, nullptr, false);
        if (j.is_discarded()) continue;
        OutfitPreset preset;
        preset.id = j.value("id", entry.path().stem().string());
        preset.name = j.value("name", preset.id);
        preset.category = j.value("category", "Custom");
        preset.description = j.value("description", "");
        if (j.contains("pieces") && j["pieces"].is_array()) {
          for (const auto& item : j["pieces"]) {
            Piece p;
            p.form_key = item.value("formKey", "");
            p.name = item.value("name", "");
            p.plugin = item.value("plugin", "");
            p.slot_mask = item.value("slotMask", 0u);
            if (!p.form_key.empty()) {
              preset.pieces.push_back(std::move(p));
            }
          }
        }
        if (!preset.pieces.empty()) {
          out.push_back(std::move(preset));
        }
      } catch (...) {
        // Skip unparseable preset files
      }
    }
  }
  return out;
}

bool save_outfit_preset_to_file(const std::filesystem::path& file_path, const OutfitPreset& preset) {
  try {
    Json j;
    j["id"] = preset.id;
    j["name"] = preset.name;
    j["category"] = preset.category;
    j["description"] = preset.description;
    Json pieces_arr = Json::array();
    for (const auto& p : preset.pieces) {
      Json pj = {{"formKey", p.form_key}, {"name", p.name}, {"plugin", p.plugin}, {"slotMask", p.slot_mask}};
      if (p.material) {
        Json mj = {{"name", p.material->name}, {"glossiness", p.material->glossiness}};
        if (p.material->tint_color.has_value()) {
          const auto& tc = *p.material->tint_color;
          mj["tintColor"] = {{"r", tc.r}, {"g", tc.g}, {"b", tc.b}};
        }
        pj["material"] = mj;
      }
      pieces_arr.push_back(std::move(pj));
    }
    j["pieces"] = pieces_arr;

    file_io::write_file_atomic(file_path, j.dump(2) + "\n");
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace sns::outfit
