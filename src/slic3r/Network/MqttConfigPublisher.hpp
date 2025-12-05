#ifndef slic3r_Network_MqttConfigPublisher_hpp_
#define slic3r_Network_MqttConfigPublisher_hpp_

#include "libslic3r/ConfigChangeDispatcher.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <boost/any.hpp>

struct mosquitto;

namespace Slic3r {

class DynamicPrintConfig;

// MQTT topic structure for slicer config
// Base: slicer/config/{page}/{group}/{key}
//
// Pages: quality, strength, speed, support, others
// Groups: layer_height, line_width, seam, precision, ironing, wall_generator, advanced, etc.
//
struct ConfigTopicInfo {
    std::string page;
    std::string group;
};

// MQTT config publisher that implements IConfigChangeListener
// Publishes DynamicPrintConfig changes to MQTT broker
//
// Topic format: slicer/config/{page}/{group}/{key}
// Payload format: JSON with value, type, and metadata
//
// Example:
//   Topic: slicer/config/quality/layer_height/layer_height
//   Payload: {
//     "key": "layer_height",
//     "value": 0.2,
//     "type": "float",
//     "meta": {
//       "label": "Layer height",
//       "category": "Quality",
//       "tooltip": "Slicing height for each layer...",
//       "unit": "mm",
//       "min": 0
//     }
//   }
//
// Usage:
//   // At application startup:
//   auto publisher = MqttConfigPublisher::create();
//   publisher->configure("localhost", 1883);
//   publisher->connect();
//   publisher->register_with_dispatcher();
//
//   // Then any call to ConfigChangeDispatcher::instance().notify()
//   // will automatically publish to MQTT
//
class MqttConfigPublisher : public IConfigChangeListener,
                            public std::enable_shared_from_this<MqttConfigPublisher> {
public:
    // Factory method - returns shared_ptr for use with dispatcher
    static std::shared_ptr<MqttConfigPublisher> create();

    ~MqttConfigPublisher() override;

    // Configure and connect
    void configure(const std::string& broker_host, int broker_port = 1883,
                   const std::string& client_id = "corvusprint-config");
    bool connect();
    void disconnect();
    bool is_connected() const;

    // Register with ConfigChangeDispatcher to receive config changes
    void register_with_dispatcher();

    // IConfigChangeListener implementation
    void on_config_change(const std::string& opt_key, const boost::any& value) override;

    // Publish a config value change directly
    void publish_change(const std::string& opt_key, const boost::any& value);

    // Publish entire config (all keys)
    void publish_full_config(const DynamicPrintConfig& config);

    // Publish printer preset config to slicer/config/printer/... topics
    // This publishes all settings from the printer preset
    void publish_printer_config(const DynamicPrintConfig& printer_config, const std::string& preset_name);

    // Publish filament preset config to slicer/config/filament/... topics
    void publish_filament_config(const DynamicPrintConfig& filament_config, const std::string& preset_name, int extruder_idx);

    // Set topic prefix (default: "slicer/")
    void set_topic_prefix(const std::string& prefix) { m_topic_prefix = prefix; }

    // Publish to arbitrary MQTT topic (for preset change notifications etc.)
    bool publish(const std::string& topic, const std::string& payload, bool retained = true);

private:
    MqttConfigPublisher();

    // Non-copyable
    MqttConfigPublisher(const MqttConfigPublisher&) = delete;
    MqttConfigPublisher& operator=(const MqttConfigPublisher&) = delete;

    // Get topic for a config key
    std::string get_topic(const std::string& opt_key) const;

    // Serialize value to JSON
    std::string serialize_value(const std::string& opt_key, const boost::any& value) const;

    // Initialize topic mapping
    void init_topic_map();

    // MQTT callbacks
    static void on_connect_callback(struct mosquitto* mosq, void* userdata, int rc);
    static void on_disconnect_callback(struct mosquitto* mosq, void* userdata, int rc);

    std::string                                     m_broker_host{"localhost"};
    int                                             m_broker_port{1883};
    std::string                                     m_client_id{"corvusprint-config"};
    std::string                                     m_topic_prefix{"slicer/"};
    struct mosquitto*                               m_mosq{nullptr};
    std::atomic<bool>                               m_connected{false};
    bool                                            m_initialized{false};
    std::unordered_map<std::string, ConfigTopicInfo> m_topic_map;
};

// Topic mapping for all config keys
// Returns page/group path for a given config key
namespace ConfigTopics {

// Quality page topics
namespace Quality {
    constexpr const char* PAGE = "quality";

    namespace LayerHeight {
        constexpr const char* GROUP = "layer_height";
        // Keys: layer_height, initial_layer_print_height
    }

    namespace LineWidth {
        constexpr const char* GROUP = "line_width";
        // Keys: line_width, initial_layer_line_width, outer_wall_line_width,
        //       inner_wall_line_width, top_surface_line_width, sparse_infill_line_width,
        //       internal_solid_infill_line_width, support_line_width
    }

    namespace Seam {
        constexpr const char* GROUP = "seam";
        // Keys: seam_position, seam_placement_away_from_overhangs, seam_gap,
        //       seam_slope_conditional, scarf_angle_threshold, seam_slope_entire_loop,
        //       seam_slope_steps, seam_slope_inner_walls, override_filament_scarf_seam_setting,
        //       seam_slope_type, seam_slope_start_height, seam_slope_gap,
        //       seam_slope_min_length, wipe_speed, role_base_wipe_speed
    }

    namespace Precision {
        constexpr const char* GROUP = "precision";
        // Keys: slice_closing_radius, resolution, enable_arc_fitting,
        //       xy_hole_compensation, xy_contour_compensation, enable_circle_compensation,
        //       circle_compensation_manual_offset, elefant_foot_compensation,
        //       precise_outer_wall, precise_z_height
    }

    namespace Ironing {
        constexpr const char* GROUP = "ironing";
        // Keys: ironing_type, ironing_pattern, ironing_speed, ironing_flow,
        //       ironing_spacing, ironing_inset, ironing_direction
    }

    namespace WallGenerator {
        constexpr const char* GROUP = "wall_generator";
        // Keys: wall_generator, wall_transition_angle, wall_transition_filter_deviation,
        //       wall_transition_length, wall_distribution_count, min_bead_width, min_feature_size
    }

    namespace Advanced {
        constexpr const char* GROUP = "advanced";
        // Keys: wall_sequence, is_infill_first, bridge_flow, thick_bridges,
        //       print_flow_ratio, top_solid_infill_flow_ratio, initial_layer_flow_ratio,
        //       top_one_wall_type, top_area_threshold, only_one_wall_first_layer,
        //       detect_overhang_wall, smooth_speed_discontinuity_area, smooth_coefficient,
        //       reduce_crossing_wall, max_travel_detour_distance, avoid_crossing_wall_includes_support,
        //       z_direction_outwall_speed_continuous
    }
}

// Strength page topics
namespace Strength {
    constexpr const char* PAGE = "strength";

    namespace Walls {
        constexpr const char* GROUP = "walls";
        // Keys: wall_loops, embedding_wall_into_infill, detect_thin_wall
    }

    namespace TopBottomShells {
        constexpr const char* GROUP = "top_bottom_shells";
        // Keys: interface_shells, top_surface_pattern, top_shell_layers, top_shell_thickness,
        //       top_color_penetration_layers, bottom_surface_pattern, bottom_shell_layers,
        //       bottom_shell_thickness, bottom_color_penetration_layers,
        //       infill_instead_top_bottom_surfaces, internal_solid_infill_pattern
    }

    namespace SparseInfill {
        constexpr const char* GROUP = "sparse_infill";
        // Keys: sparse_infill_density, fill_multiline, sparse_infill_pattern,
        //       locked_skin_infill_pattern, skin_infill_density, locked_skeleton_infill_pattern,
        //       skeleton_infill_density, infill_lock_depth, skin_infill_depth,
        //       skin_infill_line_width, skeleton_infill_line_width, symmetric_infill_y_axis,
        //       infill_shift_step, infill_rotate_step, sparse_infill_anchor,
        //       sparse_infill_anchor_max, filter_out_gap_fill
    }

    namespace Advanced {
        constexpr const char* GROUP = "advanced";
        // Keys: infill_wall_overlap, infill_direction, bridge_angle,
        //       minimum_sparse_infill_area, infill_combination,
        //       detect_narrow_internal_solid_infill, ensure_vertical_shell_thickness,
        //       detect_floating_vertical_shell
    }
}

// Speed page topics
namespace Speed {
    constexpr const char* PAGE = "speed";

    namespace InitialLayer {
        constexpr const char* GROUP = "initial_layer";
        // Keys: initial_layer_speed, initial_layer_infill_speed
    }

    namespace OtherLayers {
        constexpr const char* GROUP = "other_layers";
        // Keys: outer_wall_speed, inner_wall_speed, small_perimeter_speed,
        //       small_perimeter_threshold, sparse_infill_speed, internal_solid_infill_speed,
        //       vertical_shell_speed, top_surface_speed, enable_overhang_speed,
        //       overhang_1_4_speed, overhang_2_4_speed, overhang_3_4_speed,
        //       overhang_4_4_speed, overhang_totally_speed, enable_height_slowdown,
        //       slowdown_start_height, slowdown_start_speed, slowdown_start_acc,
        //       slowdown_end_height, slowdown_end_speed, slowdown_end_acc,
        //       bridge_speed, gap_infill_speed, support_speed, support_interface_speed
    }

    namespace Travel {
        constexpr const char* GROUP = "travel";
        // Keys: travel_speed
    }

    namespace Acceleration {
        constexpr const char* GROUP = "acceleration";
        // Keys: default_acceleration, travel_acceleration, initial_layer_travel_acceleration,
        //       initial_layer_acceleration, outer_wall_acceleration, inner_wall_acceleration,
        //       top_surface_acceleration, sparse_infill_acceleration,
        //       accel_to_decel_enable, accel_to_decel_factor
    }

    namespace Jerk {
        constexpr const char* GROUP = "jerk";
        // Keys: default_jerk, outer_wall_jerk, inner_wall_jerk, infill_jerk,
        //       top_surface_jerk, initial_layer_jerk, travel_jerk
    }
}

// Support page topics
namespace Support {
    constexpr const char* PAGE = "support";

    namespace General {
        constexpr const char* GROUP = "general";
        // Keys: enable_support, support_type, support_style, support_threshold_angle,
        //       support_on_build_plate_only, support_critical_regions_only,
        //       support_remove_small_overhang
    }

    namespace Raft {
        constexpr const char* GROUP = "raft";
        // Keys: raft_layers, raft_contact_distance
    }

    namespace Filament {
        constexpr const char* GROUP = "filament";
        // Keys: support_filament, support_interface_filament, support_interface_not_for_body
    }

    namespace Advanced {
        constexpr const char* GROUP = "advanced";
        // Keys: raft_first_layer_density, raft_first_layer_expansion, tree_support_wall_count,
        //       support_top_z_distance, support_bottom_z_distance, support_base_pattern,
        //       support_base_pattern_spacing, support_angle, support_interface_top_layers,
        //       support_interface_bottom_layers, support_interface_pattern,
        //       support_interface_spacing, support_bottom_interface_spacing, support_expansion,
        //       support_object_xy_distance, top_z_overrides_xy_distance,
        //       support_object_first_layer_gap, bridge_no_support, max_bridge_length,
        //       independent_support_layer_height
    }

    namespace TreeSupport {
        constexpr const char* GROUP = "tree_support";
        // Keys: tree_support_branch_distance, tree_support_branch_diameter,
        //       tree_support_branch_angle, tree_support_branch_diameter_angle
    }
}

// Others page topics
namespace Others {
    constexpr const char* PAGE = "others";

    namespace BedAdhesion {
        constexpr const char* GROUP = "bed_adhesion";
        // Keys: skirt_loops, skirt_height, skirt_distance, brim_type,
        //       brim_width, brim_object_gap
    }

    namespace PrimeTower {
        constexpr const char* GROUP = "prime_tower";
        // Keys: enable_prime_tower, prime_tower_skip_points, prime_tower_enable_framework,
        //       prime_tower_width, prime_tower_max_speed, prime_tower_brim_width,
        //       prime_tower_infill_gap, prime_tower_rib_wall, prime_tower_extra_rib_length,
        //       prime_tower_rib_width, prime_tower_fillet_wall
    }

    namespace FlushOptions {
        constexpr const char* GROUP = "flush_options";
        // Keys: flush_into_infill, flush_into_objects, flush_into_support
    }

    namespace SpecialMode {
        constexpr const char* GROUP = "special_mode";
        // Keys: slicing_mode, print_sequence, spiral_mode, spiral_mode_smooth,
        //       spiral_mode_max_xy_smoothing, timelapse_type, fuzzy_skin,
        //       fuzzy_skin_point_distance, fuzzy_skin_thickness
    }

    namespace Advanced {
        constexpr const char* GROUP = "advanced";
        // Keys: enable_wrapping_detection, interlocking_beam,
        //       mmu_segmented_region_interlocking_depth, interlocking_beam_width,
        //       interlocking_orientation, interlocking_beam_layer_count,
        //       interlocking_depth, interlocking_boundary_avoidance,
        //       sparse_infill_filament, solid_infill_filament, wall_filament
    }

    namespace GcodeOutput {
        constexpr const char* GROUP = "gcode_output";
        // Keys: reduce_infill_retraction, gcode_add_line_number, exclude_object,
        //       filename_format, post_process, process_notes
    }
}

} // namespace ConfigTopics

} // namespace Slic3r

#endif // slic3r_Network_MqttConfigPublisher_hpp_
