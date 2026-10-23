#include <catch2/catch.hpp>

#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/WipeTower.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Per-filament options that libslic3r reads by filament id. The CLI keeps an option's single-element default when a
// BBL 3MF lacks the key (filament_shrink exists since 2024-06, filament_adhesiveness_category since 2025-02,
// filament_change_length since 2025-01, override_process_overhang_speed since 2025-07), and nothing resizes it to the
// filament count. A short vector must apply its first value to every filament (ConfigOptionVector::get_at) and never
// be read past its end.
//
// Each case shortens one option to a single value and requires exactly the output of that value given once per
// filament, and checks that the fixture consumes the option (a different per-filament value changes the output, so
// equality is not vacuous). Every other per-filament option stays at full length, so a revert of one read can fail
// only its own case. The red side depends on undefined behaviour in Release builds (the value read past the end);
// with -D_GLIBCXX_ASSERTIONS the unfixed reads abort in std::vector::operator[].

namespace {

// Two filaments, one nozzle, prime tower on; every per-filament option at full length.
DynamicPrintConfig two_filament_config()
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    for (const std::string &key : Preset::filament_options()) {
        auto *opt = dynamic_cast<ConfigOptionVectorBase *>(config.option(key));
        if (opt != nullptr && opt->size() == 1)
            opt->resize(2);
    }
    config.set_num_filaments(2);
    config.set_key_value("filament_self_index", new ConfigOptionInts{1, 2});
    config.set_key_value("enable_prime_tower", new ConfigOptionBool(true));
    config.set_key_value("single_extruder_multi_material", new ConfigOptionBool(true));
    config.set_key_value("filament_diameter", new ConfigOptionFloats{1.75, 1.75});
    config.set_key_value("filament_colour", new ConfigOptionStrings{"#FF0000", "#00FF00"});
    config.set_key_value("filament_type", new ConfigOptionStrings{"PLA", "PLA"});
    config.set_key_value("filament_map", new ConfigOptionInts{1, 1});
    config.set_key_value("flush_volumes_matrix", new ConfigOptionFloats{0., 280., 280., 0.});
    config.set_key_value("wipe_tower_x", new ConfigOptionFloats{15.});
    config.set_key_value("wipe_tower_y", new ConfigOptionFloats{150.});
    // full_print_config() clones FullPrintConfig::defaults(), whose generic enum vectors (cooling_slowdown_logic, ...)
    // do not carry their definition's keys_map; the G-code export serializes the whole config and would dereference
    // it. A config the CLI builds creates these options from their definition, which sets the map.
    for (const std::string &key : config.keys()) {
        ConfigOption *opt = config.option(key);
        if (auto *enums = dynamic_cast<ConfigOptionEnumsGeneric *>(opt))
            enums->keys_map = print_config_def.get(key)->enum_keys_map;
        else if (auto *nullable_enums = dynamic_cast<ConfigOptionEnumsGenericNullable *>(opt))
            nullable_enums->keys_map = print_config_def.get(key)->enum_keys_map;
    }
    return config;
}

// One extruder with two Standard nozzles, filament 1 on nozzle 0 and filament 2 on nozzle 1 ("Nozzle Manual"):
// every toolchange is a nozzle change on the same extruder, which ramms with filament_change_length_nc.
DynamicPrintConfig one_extruder_two_nozzles_config()
{
    DynamicPrintConfig config = two_filament_config();
    config.set_key_value("extruder_max_nozzle_count", new ConfigOptionInts{2});
    config.set_key_value("extruder_nozzle_stats", new ConfigOptionStrings{"Standard#2"});
    config.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(fmmNozzleManual));
    config.set_key_value("filament_nozzle_map", new ConfigOptionInts{0, 1});
    config.set_key_value("filament_volume_map", new ConfigOptionInts{0, 0});
    return config;
}

DynamicPrintConfig with(DynamicPrintConfig config, std::initializer_list<ConfigBase::SetDeserializeItem> items)
{
    config.set_deserialize_strict(items);
    return config;
}

// Two copies of a test mesh, the first on filament 1 and the second on filament 2: one toolchange per layer.
Model two_objects(TestMesh test_mesh, const Vec3d &first, const Vec3d &step)
{
    Model model;
    for (int i = 0; i < 2; ++i) {
        ModelObject *object = model.add_object();
        object->name = "object" + std::to_string(i + 1) + ".stl";
        object->add_volume(mesh(test_mesh));
        object->add_instance();
        object->instances.front()->set_offset(first + step * double(i));
        object->config.set_key_value("extruder", new ConfigOptionInt(i + 1));
        object->ensure_on_bed();
    }
    return model;
}

Model two_cubes() { return two_objects(TestMesh::cube_20x20x20, Vec3d(60., 60., 0.), Vec3d(40., 0., 0.)); }

struct Sliced
{
    std::vector<double> layer_area; // per object: area (mm²) of layer 2's lslices, above the elephant foot compensation
    std::string         tower;      // every wipe tower ToolChangeResult's G-code, in print order
    std::string         gcode;      // the exported G-code's G0-G3 and T lines without comments (when exported)
};

Sliced slice(const Model &model, const DynamicPrintConfig &config, bool export_gcode = false)
{
    Print print;
    print.apply(model, config);
    print.set_status_silent();
    print.set_no_check_flag(true);
    print.process();

    Sliced out;
    for (const PrintObject *object : print.objects()) {
        double area = 0.;
        if (object->layers().size() > 2)
            for (const ExPolygon &expolygon : object->layers()[2]->lslices)
                area += expolygon.area();
        out.layer_area.push_back(area * SCALING_FACTOR * SCALING_FACTOR);
    }
    if (print.has_wipe_tower())
        for (const std::vector<WipeTower::ToolChangeResult> &layer : print.wipe_tower_data().tool_changes)
            for (const WipeTower::ToolChangeResult &tcr : layer)
                out.tower += tcr.gcode;
    if (export_gcode) {
        // Absolute: GCode::do_export creates the parent directory of a relative path, which is "" here.
        const boost::filesystem::path temp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        // A result object, as the CLI passes: GCode::do_export writes to it without a null check.
        GCodeProcessorResult result;
        print.export_gcode(temp.string(), &result, nullptr);
        std::string text;
        {
            std::ifstream in(temp.string());
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        boost::nowide::remove(temp.string().c_str());
        // Motion and tool lines only: comments carry object labels whose ids differ between two prints.
        std::istringstream in(text);
        std::string        line;
        while (std::getline(in, line))
            if (line.size() > 2 && ((line[0] == 'G' && line[1] >= '0' && line[1] <= '3' && line[2] == ' ') || (line[0] == 'T' && std::isdigit(line[1]))))
                out.gcode += line.substr(0, line.find(';')) + "\n";
    }
    return out;
}

// 1-based number of the first line where a and b differ, 0 when they are equal.
size_t first_different_line(const std::string &a, const std::string &b)
{
    std::istringstream in_a(a), in_b(b);
    std::string        line_a, line_b;
    for (size_t n = 1;; ++n) {
        const bool more_a = bool(std::getline(in_a, line_a));
        const bool more_b = bool(std::getline(in_b, line_b));
        if (! more_a && ! more_b)
            return 0;
        if (more_a != more_b || line_a != line_b)
            return n;
    }
}

// Line n (1-based) of text, or "" past its end.
std::string line_at(const std::string &text, size_t n)
{
    std::istringstream in(text);
    std::string        line;
    for (size_t i = 0; i < n && std::getline(in, line); ++i)
        ;
    return n == 0 ? std::string() : line;
}

} // namespace

TEST_CASE("Short filament_shrink: a single value scales every filament", "[ShortFilamentVector]")
{
    // PrintObjectSlice: the region's slices are scaled by 1 / filament_shrink of its wall filament. Unfixed, filament 2
    // read past the end, its slices were scaled away and process() threw SlicingErrors ("empty initial layer").
    const Model              model  = two_cubes();
    const DynamicPrintConfig single = with(two_filament_config(), {{"filament_shrink", "100%"}});
    REQUIRE(single.option<ConfigOptionPercents>("filament_shrink")->values.size() == 1);

    Sliced expected, actual;
    REQUIRE_NOTHROW(expected = slice(model, with(two_filament_config(), {{"filament_shrink", "100%,100%"}})));
    REQUIRE_NOTHROW(actual = slice(model, single));
    REQUIRE(actual.layer_area.size() == 2);
    REQUIRE(expected.layer_area.size() == 2);
    for (size_t i = 0; i < 2; ++i) {
        INFO("object " << i + 1);
        REQUIRE(expected.layer_area[i] > 0.);
        CHECK(actual.layer_area[i] == Approx(expected.layer_area[i]).epsilon(1e-6));
    }
    CHECK(first_different_line(actual.tower, expected.tower) == 0);
}

TEST_CASE("Short filament_shrink: per-filament values stay per filament", "[ShortFilamentVector]")
{
    // 80 % for filament 2 enlarges only cube 2, by 1 / 0.8 in X and Y (area x 1.5625); a fix that applied the first
    // value to every filament would leave both cubes equal.
    const Sliced result = slice(two_cubes(), with(two_filament_config(), {{"filament_shrink", "100%,80%"}}));
    REQUIRE(result.layer_area.size() == 2);
    REQUIRE(result.layer_area[0] > 0.);
    const double ratio = result.layer_area[1] / result.layer_area[0];
    INFO("area ratio cube 2 / cube 1: " << ratio);
    CHECK(ratio > 1.45);
    CHECK(ratio < 1.70);
}

TEST_CASE("Short filament_adhesiveness_category: a single value applies to every filament", "[ShortFilamentVector]")
{
    // Print::_make_wipe_tower copies the categories into the wipe tower, which picks the tower wall filament and the
    // tower blocks by category. Unfixed, the copy had the option's own length, so filament 2's category was read past
    // the end of WipeTower::m_filament_categories.
    const Model              model    = two_cubes();
    const DynamicPrintConfig single   = with(two_filament_config(), {{"filament_adhesiveness_category", "100"}});
    REQUIRE(single.option<ConfigOptionInts>("filament_adhesiveness_category")->values.size() == 1);
    const Sliced actual   = slice(model, single);
    const Sliced expected = slice(model, with(two_filament_config(), {{"filament_adhesiveness_category", "100,100"}}));
    const Sliced other    = slice(model, with(two_filament_config(), {{"filament_adhesiveness_category", "100,200"}}));
    REQUIRE(! expected.tower.empty());
    // The fixture consumes the category: a different category for filament 2 changes the tower.
    CHECK(first_different_line(other.tower, expected.tower) != 0);
    const size_t line = first_different_line(actual.tower, expected.tower);
    INFO("first different tower G-code line " << line << ": actual \"" << line_at(actual.tower, line) << "\", expected \""
                                              << line_at(expected.tower, line) << "\"");
    CHECK(line == 0);
}

TEST_CASE("Short filament_change_length_nc: a single value applies to every nozzle change", "[ShortFilamentVector]")
{
    // WipeTower copies filament_change_length(_nc) into m_filaments_change_length and indexes it by filament id for the
    // ramming of a nozzle (or extruder) change. Every toolchange of this fixture is a nozzle change on one extruder,
    // which uses the _nc vector.
    const Model              model  = two_cubes();
    const DynamicPrintConfig single = with(one_extruder_two_nozzles_config(), {{"filament_change_length_nc", "10"}});
    REQUIRE(single.option<ConfigOptionFloats>("filament_change_length_nc")->values.size() == 1);
    const Sliced actual   = slice(model, single);
    const Sliced expected = slice(model, with(one_extruder_two_nozzles_config(), {{"filament_change_length_nc", "10,10"}}));
    const Sliced longer   = slice(model, with(one_extruder_two_nozzles_config(), {{"filament_change_length_nc", "30,30"}}));
    REQUIRE(! expected.tower.empty());
    // The fixture consumes the _nc change length: a longer one changes the tower.
    CHECK(first_different_line(longer.tower, expected.tower) != 0);
    const size_t line = first_different_line(actual.tower, expected.tower);
    INFO("first different tower G-code line " << line << ": actual \"" << line_at(actual.tower, line) << "\", expected \""
                                              << line_at(expected.tower, line) << "\"");
    CHECK(line == 0);
}

TEST_CASE("Short override_process_overhang_speed: a single value applies to every filament", "[ShortFilamentVector]")
{
    // GCode::get_overhang_degree_corr_speed and is_enable_overhang_speed read the override by filament id: with it,
    // overhang perimeters use the filament's overhang speeds (7 mm/s here) instead of the process ones. Two bridges,
    // one per filament, give overhang perimeters to both filaments. The short vector is the default {false}, as for a
    // 3MF without the key: unfixed, filament 2 read the byte after the one-element vector, which is a leftover
    // allocator byte and in practice non-zero, so filament 2 took the filament overhang speeds although the override
    // is off.
    const Model model = two_objects(TestMesh::bridge, Vec3d(-20., -40., 0.), Vec3d(0., 30., 0.));
    auto        config = [](const char *override_flags) {
        return with(two_filament_config(), {{"override_process_overhang_speed", override_flags},
                                            {"filament_overhang_1_4_speed", "7,7"},
                                            {"filament_overhang_2_4_speed", "7,7"},
                                            {"filament_overhang_3_4_speed", "7,7"},
                                            {"filament_overhang_4_4_speed", "7,7"},
                                            {"filament_overhang_totally_speed", "7,7"}});
    };
    const DynamicPrintConfig single = config("0");
    REQUIRE(single.option<ConfigOptionBoolsNullable>("override_process_overhang_speed")->values.size() == 1);
    const Sliced actual   = slice(model, single, true);
    const Sliced expected = slice(model, config("0,0"), true);
    const Sliced filament = slice(model, config("1,1"), true);
    REQUIRE(! expected.gcode.empty());
    // The fixture consumes the override: the filament overhang speeds give a different G-code.
    CHECK(first_different_line(filament.gcode, expected.gcode) != 0);
    const size_t line = first_different_line(actual.gcode, expected.gcode);
    INFO("first different G-code line " << line << ": actual \"" << line_at(actual.gcode, line) << "\", expected \""
                                        << line_at(expected.gcode, line) << "\"");
    CHECK(line == 0);
}
