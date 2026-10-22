#include <catch2/catch.hpp>

#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "libslic3r/Preset.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/WipeTower.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Print::_make_wipe_tower() reads filament_prime_volume and filament_prime_volume_nc per filament. Neither vector
// is resized to the filament count: a BBL 3MF without the key keeps the single-element default ({45}, {60}) in the
// CLI. A short vector must apply its first value to every filament (get_at semantics) and never be read past its
// end.
//
// Coverage: filament_prime_volume is consumed on every toolchange between two extruders or on a single nozzle
// (the first two cases). filament_prime_volume_nc is consumed only on a change between two nozzles of the same
// extruder (WipeTower::plan_toolchange), so it gets its own one-extruder, two-nozzle case. Both vectors are read on
// every toolchange, so each case keeps the other vector at full length: the first two cases give
// filament_prime_volume_nc two values, the last gives filament_prime_volume two. A revert of either read can fail
// only the cases that shorten its own vector.
//
// The red side depends on undefined behaviour. Without the fix the Release build reads the heap word after the
// one-element vector. glibc happened to leave that word at 0, so the toolchange primed only its fixed minimum.
// Nothing guarantees that value. With -D_GLIBCXX_ASSERTIONS the same read aborts in std::vector::operator[].

namespace {

DynamicPrintConfig two_filament_tower_config(const std::vector<double> &prime_volumes)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    // Size every per-filament option to two filaments, as a complete project config has them. The prime volumes
    // are set below: filament_prime_volume from the argument (a single value is a BBL 3MF without the key) and
    // filament_prime_volume_nc at full length, so only the filament_prime_volume read can pass the vector's end.
    for (const std::string &key : Preset::filament_options()) {
        if (key == "filament_prime_volume" || key == "filament_prime_volume_nc")
            continue;
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
    config.set_key_value("filament_prime_volume", new ConfigOptionFloats(prime_volumes));
    config.set_key_value("filament_prime_volume_nc", new ConfigOptionFloats{60., 60.});
    return config;
}

// One extruder with two Standard nozzles, filament 1 on nozzle 0 and filament 2 on nozzle 1 ("Nozzle Manual"):
// every toolchange is a nozzle change on the same extruder, which primes filament_prime_volume_nc.
// filament_prime_volume is full length here, so only the _nc vector can be short.
DynamicPrintConfig one_extruder_two_nozzles_config(const std::vector<double> &prime_volumes_nc)
{
    DynamicPrintConfig config = two_filament_tower_config({45., 45.});
    config.set_key_value("filament_prime_volume_nc", new ConfigOptionFloats(prime_volumes_nc));
    config.set_key_value("extruder_max_nozzle_count", new ConfigOptionInts{2});
    config.set_key_value("extruder_nozzle_stats", new ConfigOptionStrings{"Standard#2"});
    config.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(fmmNozzleManual));
    config.set_key_value("filament_nozzle_map", new ConfigOptionInts{0, 1});
    config.set_key_value("filament_volume_map", new ConfigOptionInts{0, 0});
    return config;
}

struct TowerPrime
{
    size_t toolchanges = 0;
    double prime_e     = 0.; // filament (mm of E) primed on the tower by the new filament, summed over its toolchanges
};

// Positive E of the moves the new filament makes in one toolchange: after the tool change placeholder,
// up to "; CP TOOLCHANGE END" (a layer finish merged into the same result is not counted).
double toolchange_prime_e(const WipeTower::ToolChangeResult &tcr)
{
    std::istringstream in(tcr.gcode);
    std::string        line;
    bool               changed = false;
    double             e       = 0.;
    while (std::getline(in, line)) {
        if (line.rfind("[change_filament_gcode]", 0) == 0) {
            changed = true;
            continue;
        }
        if (line.rfind("; CP TOOLCHANGE END", 0) == 0)
            break;
        if (! changed || line.size() < 3 || line[0] != 'G' || line[1] < '1' || line[1] > '3' || line[2] != ' ')
            continue;
        const std::string code = line.substr(0, line.find(';'));
        const size_t      pos  = code.find(" E");
        if (pos == std::string::npos || (code.find(" X") == std::string::npos && code.find(" Y") == std::string::npos))
            continue;
        const double value = std::atof(code.c_str() + pos + 2);
        if (value > 0.)
            e += value;
    }
    return e;
}

// Two 20 mm cubes, the first on filament 1 and the second on filament 2: one toolchange per layer.
std::map<int, TowerPrime> prime_by_new_filament(const DynamicPrintConfig &config)
{
    Model model;
    for (int i = 0; i < 2; ++i) {
        ModelObject *object = model.add_object();
        object->name = "cube" + std::to_string(i + 1) + ".stl";
        object->add_volume(mesh(TestMesh::cube_20x20x20));
        object->add_instance();
        object->instances.front()->set_offset(Vec3d(60. + 40. * i, 60., 0.));
        object->config.set_key_value("extruder", new ConfigOptionInt(i + 1));
        object->ensure_on_bed();
    }

    Print print;
    print.apply(model, config);
    print.set_status_silent();
    print.set_no_check_flag(true);
    print.process();
    REQUIRE(print.has_wipe_tower());

    std::map<int, TowerPrime> result;
    for (const std::vector<WipeTower::ToolChangeResult> &layer : print.wipe_tower_data().tool_changes)
        for (const WipeTower::ToolChangeResult &tcr : layer) {
            if (! tcr.is_tool_change || tcr.initial_tool == tcr.new_tool)
                continue;
            TowerPrime &prime = result[tcr.new_tool];
            ++prime.toolchanges;
            prime.prime_e += toolchange_prime_e(tcr);
        }
    return result;
}

const TowerPrime &prime_into(const std::map<int, TowerPrime> &result, int filament)
{
    auto it = result.find(filament);
    REQUIRE(it != result.end());
    REQUIRE(it->second.toolchanges > 0);
    return it->second;
}

} // namespace

TEST_CASE("Wipe tower: a single filament_prime_volume value primes every filament", "[WipeTower][PrimeVolume]")
{
    // Single nozzle: this case consumes filament_prime_volume only. The _nc vector is {60, 60} in both configs, so
    // its read stays in bounds here; the last case covers a short _nc vector.
    const DynamicPrintConfig single       = two_filament_tower_config({45.});
    const DynamicPrintConfig per_filament = two_filament_tower_config({45., 45.});
    REQUIRE(single.option<ConfigOptionFloats>("filament_prime_volume")->values.size() == 1);

    const std::map<int, TowerPrime> actual   = prime_by_new_filament(single);
    const std::map<int, TowerPrime> expected = prime_by_new_filament(per_filament);

    for (int filament : {0, 1}) {
        INFO("toolchanges into filament " << filament + 1);
        const TowerPrime &a = prime_into(actual, filament);
        const TowerPrime &e = prime_into(expected, filament);
        REQUIRE(e.prime_e > 0.);
        CHECK(a.toolchanges == e.toolchanges);
        CHECK(a.prime_e == Approx(e.prime_e).epsilon(1e-4));
    }
}

TEST_CASE("Wipe tower: per-filament filament_prime_volume values stay per filament", "[WipeTower][PrimeVolume]")
{
    // 30 mm³ for filament 1 and 80 mm³ for filament 2: toolchanges into filament 2 prime more than
    // twice as much as toolchanges into filament 1, so neither the first nor the largest value is
    // applied to both.
    const std::map<int, TowerPrime> result = prime_by_new_filament(two_filament_tower_config({30., 80.}));
    const TowerPrime &into_1 = prime_into(result, 0);
    const TowerPrime &into_2 = prime_into(result, 1);
    CHECK(into_2.prime_e / double(into_2.toolchanges) > 2. * into_1.prime_e / double(into_1.toolchanges));
}

TEST_CASE("Wipe tower: a single filament_prime_volume_nc value primes every nozzle change", "[WipeTower][PrimeVolume]")
{
    // Every toolchange is a nozzle change on one extruder, which consumes filament_prime_volume_nc only. The
    // {120, 120} run shows that the fixture consumes the _nc value, so {60} matching {60, 60} is not vacuous.
    const DynamicPrintConfig single = one_extruder_two_nozzles_config({60.});
    REQUIRE(single.option<ConfigOptionFloats>("filament_prime_volume_nc")->values.size() == 1);

    const std::map<int, TowerPrime> actual   = prime_by_new_filament(single);
    const std::map<int, TowerPrime> expected = prime_by_new_filament(one_extruder_two_nozzles_config({60., 60.}));
    const std::map<int, TowerPrime> larger   = prime_by_new_filament(one_extruder_two_nozzles_config({120., 120.}));

    for (int filament : {0, 1}) {
        INFO("nozzle changes into filament " << filament + 1);
        const TowerPrime &a = prime_into(actual, filament);
        const TowerPrime &e = prime_into(expected, filament);
        const TowerPrime &l = prime_into(larger, filament);
        INFO("actual " << a.prime_e << ", expected " << e.prime_e << ", with _nc doubled " << l.prime_e);
        REQUIRE(e.prime_e > 0.);
        // The fixture really consumes filament_prime_volume_nc: doubling it raises the primed E.
        CHECK(l.prime_e > 1.5 * e.prime_e);
        CHECK(a.toolchanges == e.toolchanges);
        CHECK(a.prime_e == Approx(e.prime_e).epsilon(1e-4));
    }
}
